#pragma once

#include "ito/details/utils/raii_coro_handle.hpp"

#include <ito/details/utils/overloaded.hpp>
#include <ito/exceptions.hpp>

#include <coroutine>
#include <exception>
#include <utility>
#include <variant>

namespace ito
{
    class loop;
    namespace internal
    {
        struct void_t
        {
        };

        struct base_promise_type
        {
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
                    details::utils::overloaded{
                        [](const std::monostate&) -> T&& { throw ito::exceptions::empty_value{"value of promise_type is not set"}; },
                        [](T&& v) -> T&& { return std::move(v); },
                        [](const std::exception_ptr& e) -> T&& { std::rethrow_exception(e); }
                    },
                    std::move(m_value)
                );
            };

        public:
            void               unhandled_exception() { m_value.template emplace<2>(std::current_exception()); }
            [[nodiscard]] bool is_ready() const { return m_value.index() > 0; }
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
    class [[nodiscard("ito::coro object can't be discarded")]] coro
    {
    public:
        friend class ito::loop;

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
                details::utils::raii_coroutine_handle<promise_type> _h;

                constexpr bool await_ready() noexcept { return _h->is_ready(); }
                auto           await_suspend(std::coroutine_handle<> h) noexcept
                {
                    _h->continuation = h;
                    return _h.get();
                }
                T await_resume() { return _h->get_result(); }
            };
            return awaitable{std::move(m_h)};
        }

    private:
        explicit coro(std::coroutine_handle<promise_type> h)
            : m_h{h}
        {
        }

        details::utils::raii_coroutine_handle<promise_type> detach() &&
        {
            if (!m_h)
            {
                throw exceptions::invalid_coro_handle_state{"no coroutine handle when trying to detach coro"};
            }

            return std::move(m_h);
        }

    private:
        details::utils::raii_coroutine_handle<promise_type> m_h{};
    };
} // namespace ito
