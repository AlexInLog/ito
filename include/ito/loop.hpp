#pragma once

#include <ito/coro.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/exceptions.hpp>

#include <concepts>
#include <coroutine>
#include <deque>
#include <functional>
#include <utility>

namespace ito::exceptions
{
    struct invalid_loop_state final : public ito_exception
    {
        using ito_exception::ito_exception;
    };
} // namespace ito::exceptions

namespace ito
{
    class loop
    {
        auto lock()
        {
            auto& current_loop_ptr = current_impl();
            if (current_loop_ptr)
            {
                throw exceptions::invalid_loop_state{
                    "loop::run_until_complete() called from within an already-running loop on this thread"
                };
            }

            current_loop_ptr = this;
            return details::utils::finally{[]() noexcept { current_impl() = nullptr; }};
        }

        static loop*& current_impl()
        {
            static thread_local loop* s_loop{};
            return s_loop;
        }

    public:
        loop() = default;

        template<typename T>
        T run_until_complete(ito::coro<T>&& coro)
        {
            [[maybe_unused]] const auto locked = lock();

            details::utils::raii_coroutine_handle<typename ito::coro<T>::promise_type> h = std::move(coro).detach();

            run_until_complete_impl(h.get());

            return h->get_result();
        }

        template<typename Fn>
            requires std::invocable<std::decay_t<Fn>&&>
        void call_soon(Fn&& callback)
        {
            m_queue.emplace_back(std::forward<Fn>(callback));
        }

        static loop& current()
        {
            const auto l = try_current();
            if (!l) [[unlikely]]
                throw exceptions::invalid_loop_state{"no loop is currently running on this thread"};

            return *l;
        }
        static loop* try_current() noexcept { return current_impl(); }

    private:
        void run_until_complete_impl(std::coroutine_handle<> h)
        {
            if (!m_queue.empty())
                m_queue.emplace_back([&h]() { h.resume(); });
            else
                h.resume();

            while (!h.done() && !m_queue.empty())
            {
                const auto _ = details::utils::finally{[this]() noexcept { m_queue.pop_front(); }};
                std::move(m_queue.front())();
            }
        }

    private:
        std::deque<std::function<void()>> m_queue{};
    };
} // namespace ito
