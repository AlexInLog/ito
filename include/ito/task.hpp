#pragma once

#include <ito/coro.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/details/utils/trackable.hpp>

namespace ito
{
    class loop;

    template<typename T>
    class task
    {
    private:
        using handle_type = details::utils::trackable<details::utils::raii_coroutine_handle<typename coro<T>::promise_type>>;
        explicit task(handle_type h)
            : m_h{std::move(h)}
        {
        }

    public:
        friend class ito::loop;

        auto operator co_await() &&
        {
            if (!m_h.get())
            {
                throw exceptions::invalid_coro_handle_state{"no coroutine handle when trying to co_await task"};
            }

            struct awaitable
            {
                handle_type _h;

                constexpr bool await_ready() noexcept { return _h.get()->is_ready(); }

                auto await_suspend(std::coroutine_handle<> h) noexcept
                {
                    _h.get()->continuation = h;
                    return;
                }

                T await_resume() { return _h.get()->get_result(); }
            };
            return awaitable{std::move(m_h)};
        }

    private:
        handle_type m_h;
    };
} // namespace ito
