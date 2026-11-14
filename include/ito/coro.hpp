#pragma once

#include <ito/details/ready_node.hpp>
#include <ito/details/utils/outcome.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/exceptions.hpp>
#include <ito/fwd.hpp>

#include <coroutine>
#include <exception>
#include <utility>

namespace ito
{
    namespace details
    {
        [[noreturn]] inline void throw_empty_coro()
        {
            throw exceptions::invalid_coro_handle_state{"no coroutine handle when trying to co_await coro"};
        }

        // The promise is its frame's node in the loop's ready queue: a suspended frame waits on one thing at a time.
        struct base_promise_type : public ready_node
        {
            base_promise_type() noexcept                           = default;
            base_promise_type(const base_promise_type&)            = delete;
            base_promise_type(base_promise_type&&)                 = delete;
            base_promise_type& operator=(const base_promise_type&) = delete;
            base_promise_type& operator=(base_promise_type&&)      = delete;

            std::coroutine_handle<> continuation = std::noop_coroutine();

            static constexpr std::suspend_always initial_suspend() noexcept { return {}; }

            [[nodiscard]] auto final_suspend() const noexcept
            {
                struct awaitable
                {
                    std::coroutine_handle<> continuation{};

                    static constexpr bool await_ready() noexcept { return false; }
                    [[nodiscard]] auto    await_suspend(std::coroutine_handle<>) const noexcept { return continuation; }
                    static constexpr void await_resume() noexcept { }
                };
                return awaitable{continuation};
            }

        protected:
            // Polymorphic (run() is virtual) but only ever destroyed as the final coro<T>::promise_type, by the frame,
            // so this and the promise bases below have protected non-virtual destructors.
            ~base_promise_type() noexcept = default;
        };

        template<typename T>
        struct typed_promise_type
            : public details::base_promise_type
            , protected ito::details::utils::outcome<T>
        {
            void unhandled_exception() { this->replace_with_exception(std::current_exception()); }

            using ito::details::utils::outcome<T>::get_result;
            using ito::details::utils::outcome<T>::is_ready;

        protected:
            typed_promise_type() noexcept  = default;
            ~typed_promise_type() noexcept = default;

        public:
            typed_promise_type(const typed_promise_type&)            = delete;
            typed_promise_type(typed_promise_type&&)                 = delete;
            typed_promise_type& operator=(const typed_promise_type&) = delete;
            typed_promise_type& operator=(typed_promise_type&&)      = delete;
        };

        template<typename T>
        struct promise_type : public typed_promise_type<T>
        {
            void return_value(T&& v) { this->set_result(std::move(v)); }
            void return_value(const T& v) { this->set_result(v); }

        protected:
            promise_type() noexcept  = default;
            ~promise_type() noexcept = default;

        public:
            promise_type(const promise_type&)            = delete;
            promise_type(promise_type&&)                 = delete;
            promise_type& operator=(const promise_type&) = delete;
            promise_type& operator=(promise_type&&)      = delete;
        };

        template<>
        struct promise_type<void> : public typed_promise_type<void>
        {
            void return_void() { this->set_result(); }

        protected:
            promise_type() noexcept  = default;
            ~promise_type() noexcept = default;

        public:
            promise_type(const promise_type&)            = delete;
            promise_type(promise_type&&)                 = delete;
            promise_type& operator=(const promise_type&) = delete;
            promise_type& operator=(promise_type&&)      = delete;
        };
    } // namespace details

    template<typename T>
    class [[nodiscard("ito::coro object can't be discarded")]] coro final
    {
    public:
        friend class ito::loop;

        struct promise_type final : public details::promise_type<T>
        {
            coro get_return_object() { return coro{std::coroutine_handle<promise_type>::from_promise(*this)}; }

            // Overridden here, in the concrete promise type, because from_promise() needs the coroutine's actual
            // promise object: calling it on a base subobject would be UB.
            void run() noexcept override { std::coroutine_handle<promise_type>::from_promise(*this).resume(); }
        };

        auto operator co_await() &&
        {
            struct awaitable
            {
                details::utils::raii_coroutine_handle<promise_type> _h;

                constexpr bool await_ready() noexcept { return _h->is_ready(); }
                auto           await_suspend(std::coroutine_handle<> h) noexcept
                {
                    _h->continuation = h;
                    return _h.get();
                }
                T await_resume() { return _h->get_result(); }
            };

            return awaitable{std::move(*this).detach()};
        }

    private:
        explicit coro(std::coroutine_handle<promise_type> h)
            : m_h{h}
        {
        }

        details::utils::raii_coroutine_handle<promise_type> detach() &&
        {
            if (m_h) [[likely]]
                return std::move(m_h);

            details::throw_empty_coro();
        }

    private:
        details::utils::raii_coroutine_handle<promise_type> m_h{};
    };
} // namespace ito
