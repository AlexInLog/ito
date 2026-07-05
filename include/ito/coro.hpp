#pragma once

#include <ito/exceptions.hpp>
#include <ito/utils.hpp>

#include <coroutine>
#include <exception>
#include <utility>
#include <variant>

namespace ito
{
    class executor;
    namespace internal
    {
        struct void_t
        {
        };

        struct base_promise_type
        {
            std::coroutine_handle<> continuation = std::noop_coroutine();

            constexpr std::suspend_always initial_suspend() const noexcept { return {}; }

            auto final_suspend() const noexcept
            {
                struct awaitable
                {
                    std::coroutine_handle<> continuation{};

                    constexpr bool await_ready() const noexcept { return false; }
                    auto           await_suspend(std::coroutine_handle<>) const noexcept { return continuation; }
                    constexpr void await_resume() const noexcept { }
                };
                return awaitable{continuation};
            }
        };

        template<typename T>
        struct typed_promise_type : public internal::base_promise_type
        {
        private:
            std::variant<std::monostate, T, std::exception_ptr> m_value{};

        protected:
            void set_value(T&& v) { m_value.template emplace<1>(std::move(v)); }
            void set_value(const T& v) { m_value.template emplace<1>(v); }

            T&& get_result_impl()
            {
                return std::visit(
                    utils::overloaded{
                        [](const std::monostate&) -> T&& { throw ito::exceptions::empty_value{"value of promise_type is not set"}; },
                        [](T&& v) -> T&& { return std::move(v); },
                        [](const std::exception_ptr& e) -> T&& { std::rethrow_exception(e); }
                    },
                    std::move(m_value)
                );
            };

        public:
            void unhandled_exception() { m_value.template emplace<2>(std::current_exception()); }
        };

        template<typename T>
        struct promise_type : public typed_promise_type<T>
        {
            void return_value(T&& v) { typed_promise_type<T>::set_value(std::move(v)); }
            void return_value(const T& v) { typed_promise_type<T>::set_value(v); }
            T&&  get_result() { return this->get_result_impl(); }
        };

        template<>
        struct promise_type<void> : public typed_promise_type<void_t>
        {
            void return_void() { typed_promise_type<void_t>::set_value(void_t{}); }
            void get_result() { this->get_result_impl(); }
        };
    } // namespace internal

    template<typename T = void>
    class coro
    {
    public:
        ~coro() noexcept
        {
            if (m_h) m_h.destroy();
        }

        coro(coro&& other) noexcept
            : m_h(std::exchange(other.m_h, {}))
        {
        }

        coro(const coro&)                = delete;
        coro& operator=(const coro&)     = delete;
        coro& operator=(coro&&) noexcept = delete;

        friend class ito::executor;

        struct promise_type : public internal::promise_type<T>
        {
            coro get_return_object() { return coro{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        };

        auto operator co_await() &&
        {
            if (!m_h)
            {
                throw exceptions::invalid_coro_handle_state{"no coroutine handle when trying to co_await coro"};
            }

            struct awaitable
            {
                std::coroutine_handle<promise_type> _h{};

                ~awaitable() noexcept { _h.destroy(); }

                constexpr bool await_ready() const noexcept { return false; }
                auto           await_suspend(std::coroutine_handle<> h) noexcept
                {
                    _h.promise().continuation = h;
                    return _h;
                }
                T await_resume() { return _h.promise().get_result(); }
            };
            return awaitable{std::exchange(m_h, {})};
        }

    private:
        explicit coro(std::coroutine_handle<promise_type> h)
            : m_h{h}
        {
        }

        T run() &&
        {
            if (!m_h)
            {
                throw exceptions::invalid_coro_handle_state{"no coroutine handle when trying to run coro"};
            }
            
            m_h.resume();
            const auto _ = utils::finally{[this]() noexcept { std::exchange(m_h, {}).destroy(); }};
            return m_h.promise().get_result();
        }

    private:
        std::coroutine_handle<promise_type> m_h{};
    };
} // namespace ito
