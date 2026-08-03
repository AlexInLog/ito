#pragma once

#include <ito/details/utils/coroutine_handle.hpp>
#include <ito/details/utils/error_or_optional.hpp>
#include <ito/details/utils/trackable.hpp>
#include <ito/loop.hpp>

#include <coroutine>
#include <exception>
#include <utility>

namespace ito::exceptions
{
    struct broken_future final : public ito_exception
    {
        using ito_exception::ito_exception;
    };
} // namespace ito::exceptions

namespace ito::async
{
    namespace details
    {
        template<typename T = void>
        struct future_state
        {
            ito::details::utils::error_or_optional<T> value{};
            ito::details::utils::coroutine_handle<>   continuation{};
        };

        // Kept in its own function so that the awaitable steps below stay small enough to be inlined
        // into the awaiting coroutine: constructing and throwing the exception is most of their code,
        // while it only ever runs when the promise is already gone.
        [[noreturn]] inline void throw_broken_future()
        {
            throw ito::exceptions::broken_future{"no associated future state"};
        }

        template<typename T = void>
        class promise_base
        {
        public:
            ~promise_base() noexcept
            {
                const auto ptr = m_value.get();
                if (!ptr) [[likely]]
                    return;

                notify_broken_future(ptr);
            }

            // The move constructor is intentionally left defaulted: all state that the destructor
            // above cares about (`m_value->continuation`) lives inside the `trackable` member, whose
            // own move constructor already re-points the associated `weak_view`/clears the source
            // correctly. There is nothing left for this class to additionally manage on move.
            promise_base(const promise_base&)            = delete;
            promise_base(promise_base&&) noexcept        = default;
            promise_base& operator=(const promise_base&) = delete;
            promise_base& operator=(promise_base&&)      = delete;

            template<typename... TT>
            void set_result_impl(TT&&... v)
            {
                if (const auto ptr = m_value.get())
                {
                    auto prepared = this->prepare_scheduling_continuation();
                    ptr->value.set_result(std::forward<TT>(v)...);
                    prepared();
                }
            }

            void set_exception(const std::exception_ptr& err)
            {
                if (const auto ptr = m_value.get())
                {
                    auto prepared = this->prepare_scheduling_continuation();
                    ptr->value.set_exception(err);
                    prepared();
                }
            }

            [[nodiscard]] bool is_ready() const
            {
                const auto ptr = m_value.get();
                return !ptr || ptr->value.is_ready();
            }

        protected:
            explicit promise_base(ito::details::utils::trackable<details::future_state<T>>::weak_view&& value)
                : m_value{std::move(value)}
            {
            }

        private:
            // The broken-future notification is kept out of ~promise_base(): it only runs when the
            // promise dies with a coroutine still waiting on it, and inlining it (call_soon() plus the
            // try/catch) is what stops the destructor itself from being inlined into its caller.
            static void notify_broken_future(details::future_state<T>* ptr) noexcept
            {
                if (!ptr->value.is_ready()) [[unlikely]]
                {
                    // set_exception()/make_exception_ptr() can throw (e.g. std::bad_alloc); swallow it
                    // rather than let it escape this noexcept destructor and terminate() — the
                    // continuation, if any, still gets scheduled below and observes an empty value
                    // instead of broken_future in this vanishingly rare case
                    try
                    {
                        ptr->value.set_exception(
                            std::make_exception_ptr(ito::exceptions::broken_future{"promise object destroyed unresolved"})
                        );
                    }
                    catch (...) // NOLINT(bugprone-empty-catch)
                    {
                    }
                }

                if (!ptr->continuation) [[likely]]
                    return;

                if (const auto loop = ito::loop::try_current()) [[likely]]
                {
                    // call_soon() can throw (e.g. std::bad_alloc from the queue); swallow it
                    // rather than let it escape this noexcept destructor and terminate()
                    try
                    {
                        loop->call_soon(std::move(ptr->continuation).detach());
                    }
                    catch (...) // NOLINT(bugprone-empty-catch)
                    {
                        // deliberately empty: this destructor must stay noexcept, and there is
                        // nothing sensible to do with a failed best-effort notification here
                    }
                }
            }
            [[nodiscard]] auto prepare_scheduling_continuation()
            {
                // we are doing it as lambda to try to catch loop BEFORE actual value changes so crash would happen BEFORE
                const auto ptr = m_value.get();
                return [loop = ptr && ptr->continuation ? &ito::loop::current() : nullptr, ptr]() {
                    if (loop)
                        loop->call_soon(std::move(ptr->continuation).detach());
                };
            }

        private:
            ito::details::utils::trackable<details::future_state<T>>::weak_view m_value{};
        };
    } // namespace details

    template<typename T = void>
    class promise;

    template<typename T = void>
    class future
    {
    public:
        friend class promise<T>;

        auto operator co_await() &&
        {
            struct awaitable
            {
                ito::details::utils::trackable<details::future_state<T>> obj;

                explicit awaitable(ito::details::utils::trackable<details::future_state<T>>&& obj)
                    : obj{std::move(obj)}
                {
                }

                ~awaitable() noexcept { obj->continuation = ito::details::utils::coroutine_handle<>{}; }

                awaitable(const awaitable&)            = delete;
                awaitable& operator=(const awaitable&) = delete;

                awaitable(awaitable&& o) noexcept = delete;
                awaitable& operator=(awaitable&&) = delete;

                constexpr bool await_ready() noexcept { return obj->value.is_ready(); }

                auto await_suspend(std::coroutine_handle<> h) noexcept { obj->continuation = ito::details::utils::coroutine_handle<>{h}; }

                T await_resume() { return obj->value.get_result(); }
            };
            return awaitable{std::move(m_obj)};
        }

    private:
        explicit future(ito::details::utils::trackable<details::future_state<T>>&& obj)
            : m_obj{std::move(obj)}
        {
        }

        ito::details::utils::trackable<details::future_state<T>> m_obj;
    };

    template<typename T>
    class promise final : private details::promise_base<T>
    {
    public:
        [[nodiscard]] static std::pair<promise<T>, future<T>> create()
        {
            auto [obj, view] = ito::details::utils::trackable<details::future_state<T>>::create();
            return {promise<T>{std::move(view)}, future<T>{std::move(obj)}};
        }

        void set_result(const T& v) { this->set_result_impl(v); }
        void set_result(T&& v) { this->set_result_impl(std::move(v)); }

        using details::promise_base<T>::set_exception;
        using details::promise_base<T>::is_ready;

    private:
        explicit promise(ito::details::utils::trackable<details::future_state<T>>::weak_view&& value)
            : details::promise_base<T>{std::move(value)}
        {
        }
    };

    template<>
    class promise<void> final : private details::promise_base<void>
    {
    public:
        [[nodiscard]] static std::pair<promise<void>, future<void>> create()
        {
            auto [obj, view] = ito::details::utils::trackable<details::future_state<void>>::create();
            return {promise<void>{std::move(view)}, future<void>{std::move(obj)}};
        }

        void set_result() { this->set_result_impl(); }

        using details::promise_base<void>::set_exception;
        using details::promise_base<void>::is_ready;

    private:
        explicit promise(ito::details::utils::trackable<details::future_state<void>>::weak_view&& value)
            : details::promise_base<void>{std::move(value)}
        {
        }
    };

} // namespace ito::async
