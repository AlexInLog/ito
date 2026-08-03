#pragma once

#include <ito/details/utils/coroutine_handle.hpp>
#include <ito/details/utils/error_or_optional.hpp>
#include <ito/details/utils/trackable.hpp>
#include <ito/loop.hpp>

#include <coroutine>
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
                if (!m_value->continuation) [[likely]]
                    return;

                notify_broken_future();
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
                auto prepared = this->prepare_scheduling_continuation();
                m_value->value.set_result(std::forward<TT>(v)...);
                prepared();
            }

            void set_exception(const std::exception_ptr& err)
            {
                auto prepared = this->prepare_scheduling_continuation();
                m_value->value.set_exception(err);
                prepared();
            }

            [[nodiscard]] bool is_ready() const { return m_value->value.is_ready(); }

        protected:
            explicit promise_base(ito::details::utils::trackable<details::future_state<T>>&& value)
                : m_value{std::move(value)}
            {
            }

        private:
            // The broken-future notification is kept out of ~promise_base(): it only runs when the
            // promise dies with a coroutine still waiting on it, and inlining it (call_soon() plus the
            // try/catch) is what stops the destructor itself from being inlined into its caller.
            void notify_broken_future() noexcept
            {
                if (const auto loop = ito::loop::try_current()) [[likely]]
                {
                    // call_soon() can throw (e.g. std::bad_alloc from the queue); swallow it
                    // rather than let it escape this noexcept destructor and terminate()
                    try
                    {
                        loop->call_soon(std::move(m_value->continuation).detach());
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
                return [loop = m_value->continuation ? &ito::loop::current() : nullptr, this]() {
                    if (loop)
                        loop->call_soon(std::move(m_value->continuation).detach());
                };
            }

        private:
            ito::details::utils::trackable<details::future_state<T>> m_value{};
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
                ito::details::utils::trackable<details::future_state<T>>::weak_view view;

                explicit awaitable(ito::details::utils::trackable<details::future_state<T>>::weak_view&& view)
                    : view{std::move(view)}
                {
                }

                ~awaitable() noexcept
                {
                    if (const auto ptr = view.get())
                        ptr->continuation = ito::details::utils::coroutine_handle<>{};
                }

                awaitable(const awaitable&)            = delete;
                awaitable& operator=(const awaitable&) = delete;

                awaitable(awaitable&& o) noexcept = delete;
                awaitable& operator=(awaitable&&) = delete;

                constexpr bool await_ready() noexcept
                {
                    const auto ptr = view.get();
                    return !ptr || ptr->value.is_ready();
                }

                auto await_suspend(std::coroutine_handle<> h)
                {
                    if (const auto ptr = view.get()) [[likely]]
                        ptr->continuation = ito::details::utils::coroutine_handle<>{h};
                    else
                        details::throw_broken_future();
                }

                T await_resume()
                {
                    if (const auto ptr = view.get()) [[likely]]
                        return ptr->value.get_result();

                    details::throw_broken_future();
                }
            };
            return awaitable{std::move(m_view)};
        }

    private:
        explicit future(ito::details::utils::trackable<details::future_state<T>>::weak_view&& view)
            : m_view{std::move(view)}
        {
        }

        ito::details::utils::trackable<details::future_state<T>>::weak_view m_view;
    };

    template<typename T>
    class promise final : private details::promise_base<T>
    {
    public:
        [[nodiscard]] static std::pair<promise<T>, future<T>> create()
        {
            auto [obj, view] = ito::details::utils::trackable<details::future_state<T>>::create();
            return {promise<T>{std::move(obj)}, future<T>{std::move(view)}};
        }

        void set_result(const T& v) { this->set_result_impl(v); }
        void set_result(T&& v) { this->set_result_impl(std::move(v)); }

        using details::promise_base<T>::set_exception;

    private:
        explicit promise(ito::details::utils::trackable<details::future_state<T>>&& value)
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
            return {promise<void>{std::move(obj)}, future<void>{std::move(view)}};
        }

        void set_result() { this->set_result_impl(); }

        using details::promise_base<void>::set_exception;

    private:
        explicit promise(ito::details::utils::trackable<details::future_state<void>>&& value)
            : details::promise_base<void>{std::move(value)}
        {
        }
    };

} // namespace ito::async
