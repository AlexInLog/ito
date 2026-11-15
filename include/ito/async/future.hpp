#pragma once

#include <ito/details/utils/outcome.hpp>
#include <ito/details/utils/trackable.hpp>
#include <ito/fwd.hpp>
#include <ito/loop.hpp>

#include <concepts>
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
            ito::details::utils::outcome<T> value{};
            // The awaiting frame's ready-queue node. This state lives in the awaitable, in that same frame, so the
            // pointer can't outlive the frame. Null until a coroutine awaits, and again once its resume is queued.
            ito::details::intrusive_ready_node* continuation{};

            // Queues the awaiting frame and clears `continuation` in the same step. That's what keeps a frame from
            // being queued twice: once its node has been popped and run, the node no longer says whether it's
            // waiting on this future, so "is it already linked?" can't stand in for this. Callers check that a
            // continuation is set: it keeps this out of the resolve path's inlining budget twice.
            void schedule_continuation(ito::loop& loop) noexcept
            {
                ito::details::loop_internals::schedule(loop, *std::exchange(continuation, nullptr));
            }
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
            // own move constructor already re-points the associated `weak_ref`/clears the source
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
                    // looked up before the value changes, so a missing loop throws before anything is set
                    const auto loop = ptr->continuation ? &ito::loop::current() : nullptr;
                    ptr->value.set_result(std::forward<TT>(v)...);
                    if (loop)
                        ptr->schedule_continuation(*loop);
                }
            }

            void set_exception(const std::exception_ptr& err)
            {
                if (const auto ptr = m_value.get())
                {
                    const auto loop = ptr->continuation ? &ito::loop::current() : nullptr;
                    ptr->value.set_exception(err);
                    if (loop)
                        ptr->schedule_continuation(*loop);
                }
            }

            // True once there's nothing left for this promise to deliver: a result/exception was set, or the
            // future side is already gone (in which case whether anything was ever set is no longer known).
            [[nodiscard]] bool is_settled() const
            {
                const auto ptr = m_value.get();
                return !ptr || ptr->value.is_settled();
            }

        protected:
            explicit promise_base(ito::details::utils::trackable<details::future_state<T>>::weak_ref&& value)
                : m_value{std::move(value)}
            {
            }

        private:
            // The broken-future notification is kept out of ~promise_base(): it only runs when the
            // promise dies with a coroutine still waiting on it, and inlining it (building the exception
            // plus the try/catch) is what stops the destructor itself from being inlined into its caller.
            static void notify_broken_future(details::future_state<T>* ptr) noexcept
            {
                if (!ptr->value.is_settled()) [[unlikely]]
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
                    ptr->schedule_continuation(*loop);
            }

        private:
            ito::details::utils::trackable<details::future_state<T>>::weak_ref m_value{};
        };
    } // namespace details

    template<typename T = void>
    class future
    {
        // A named member rather than a class local to operator co_await(): a local class can't have the member
        // template await_suspend() needs.
        struct awaitable
        {
            ito::details::utils::trackable<details::future_state<T>> obj;

            explicit awaitable(ito::details::utils::trackable<details::future_state<T>>&& obj)
                : obj{std::move(obj)}
            {
            }

            ~awaitable() noexcept { obj->continuation = nullptr; }

            awaitable(const awaitable&)            = delete;
            awaitable& operator=(const awaitable&) = delete;

            awaitable(awaitable&& o) noexcept = delete;
            awaitable& operator=(awaitable&&) = delete;

            constexpr bool await_ready() noexcept { return obj->value.is_ready(); }

            // Typed handle: resolving the promise queues the awaiting frame's own node, which only ito coroutines have.
            template<std::derived_from<ito::details::base_promise_type> Promise>
            void await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                obj->continuation = &h.promise();
            }

            T await_resume() { return obj->value.get_result(); }
        };

    public:
        friend class promise<T>;

        auto operator co_await() && { return awaitable{std::move(m_obj)}; }

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
        using details::promise_base<T>::is_settled;

    private:
        explicit promise(ito::details::utils::trackable<details::future_state<T>>::weak_ref&& value)
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
        using details::promise_base<void>::is_settled;

    private:
        explicit promise(ito::details::utils::trackable<details::future_state<void>>::weak_ref&& value)
            : details::promise_base<void>{std::move(value)}
        {
        }
    };

} // namespace ito::async
