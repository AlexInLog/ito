#pragma once

#include <ito/coro.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/details/utils/trackable.hpp>

#include <coroutine>

namespace ito
{
    class loop;

    template<typename T>
    class [[nodiscard("ito::task can't be discarded")]] task
    {
    private:
        using handle_type = details::utils::trackable<details::utils::raii_coroutine_handle<>>;
        explicit task(handle_type h)
            : m_h{std::move(h)}
        {
        }

    public:
        friend class ito::loop;

        task(task&&) noexcept = default;
        ~task() noexcept      = default;

        task& operator=(const task&) = delete;
        task& operator=(task&&)      = delete;
        task(const task&)            = delete;

        auto operator co_await() &&
        {
            struct awaitable
            {
                handle_type _h;

                std::coroutine_handle<typename coro<T>::promise_type> cast() { return _h.get().get<typename coro<T>::promise_type>(); }

                constexpr bool await_ready() noexcept { return cast().promise().is_ready(); }

                auto await_suspend(std::coroutine_handle<> h) noexcept
                {
                    cast().promise().continuation = h;
                    return;
                }

                T await_resume() { return cast().promise().get_result(); }
            };

            if (m_h.get()) [[likely]]
                return awaitable{std::move(m_h)};

            details::throw_empty_coro();
        }

    private:
        handle_type m_h;
    };
} // namespace ito
