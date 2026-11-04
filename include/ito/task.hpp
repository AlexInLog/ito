#pragma once

#include <ito/coro.hpp>
#include <ito/details/utils/anchored.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/fwd.hpp>

namespace ito
{
    // An exception escaping the task's coroutine is stored in its promise and rethrown by `co_await`. If the task is
    // destroyed without being awaited, that exception is dropped silently: nothing logs or reports it.
    template<typename T>
    class [[nodiscard("ito::task can't be discarded")]] task final
    {
    private:
        task(details::utils::raii_coroutine_handle<> h, details::utils::anchor anchor) noexcept
            : m_h{std::move(h)}
            , m_anchor{std::move(anchor)}
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
                // the frame and its anchor move together: the task's queued start may still be pending when it's awaited
                details::utils::raii_coroutine_handle<> _h;
                details::utils::anchor                  _anchor;

            private:
                typename coro<T>::promise_type& cast() { return _h.get<typename coro<T>::promise_type>().promise(); }

            public:
                constexpr bool await_ready() noexcept { return cast().is_ready(); }

                auto await_suspend(std::coroutine_handle<> h) noexcept
                {
                    cast().continuation = h;
                    return;
                }

                T await_resume() { return cast().get_result(); }
            };

            if (m_h) [[likely]]
                return awaitable{std::move(m_h), std::move(m_anchor)};

            details::throw_empty_coro();
        }

    private:
        // Owns the frame; m_anchor is what the queued start is anchored to, so destroying the task cancels that start.
        details::utils::raii_coroutine_handle<> m_h;
        details::utils::anchor                  m_anchor;
    };
} // namespace ito
