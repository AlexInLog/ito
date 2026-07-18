#pragma once

#include <ito/details/utils/error_or_optional.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/exceptions.hpp>

#include <coroutine>
#include <exception>
#include <utility>

namespace ito
{
    class loop;
    namespace details
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
        struct typed_promise_type
            : public details::base_promise_type
            , protected ito::details::utils::error_or_optional<T>
        {
            void unhandled_exception() { this->set_exception(std::current_exception()); }

            using ito::details::utils::error_or_optional<T>::get_result;
            using ito::details::utils::error_or_optional<T>::is_ready;
        };

        template<typename T>
        struct promise_type : public typed_promise_type<T>
        {
            void return_value(T&& v) { this->set_result(std::move(v)); }
            void return_value(const T& v) { this->set_result(v); }
        };

        template<>
        struct promise_type<void> : public typed_promise_type<void>
        {
            void return_void() { this->set_result(); }
        };
    } // namespace details

    template<typename T = void>
    class [[nodiscard("ito::coro object can't be discarded")]] coro
    {
    public:
        friend class ito::loop;

        struct promise_type : public details::promise_type<T>
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
