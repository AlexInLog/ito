#pragma once


#include <ito/coro.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/details/utils/trackable.hpp>
#include <ito/exceptions.hpp>
#include <ito/task.hpp>

#include <chrono>
#include <concepts>
#include <coroutine>
#include <deque>
#include <functional>
#include <thread>
#include <utility>
#include <variant>

namespace ito::exceptions
{
    struct invalid_loop_state final : public ito_exception
    {
        using ito_exception::ito_exception;
    };
} // namespace ito::exceptions

namespace ito::details
{
    struct coro_handle_executor
    {
        std::coroutine_handle<> handle;

        void operator()() const
        {
            if (handle)
                handle.resume();
        }
    };

    struct trackable_view_raii_coro_handle_executor
    {
        details::utils::trackable<details::utils::raii_coroutine_handle<>>::weak_view handle;

        void operator()() const
        {
            if (const auto* ptr = handle.get())
                ptr->get().resume();
        }
    };

    struct trackable_view_coro_handle_executor
    {
        details::utils::trackable<std::coroutine_handle<>>::weak_view handle;

        void operator()() const
        {
            if (const auto* ptr = handle.get())
                ptr->resume();
        }
    };


    class timer_queue
    {
        struct entry
        {
            std::chrono::steady_clock::time_point deadline;
            trackable_view_coro_handle_executor   view;
        };

        static constexpr bool later(const entry& a, const entry& b) { return a.deadline > b.deadline; }

    public:
        void push(std::chrono::steady_clock::time_point deadline, details::utils::trackable<std::coroutine_handle<>>::weak_view view)
        {
            m_entries.push_back(entry{.deadline = deadline, .view = {std::move(view)}});
            std::push_heap(m_entries.begin(), m_entries.end(), later);
        }

        [[nodiscard]] bool empty() const { return m_entries.empty(); }

        [[nodiscard]] std::chrono::steady_clock::time_point next_deadline() const { return m_entries.front().deadline; }

        [[nodiscard]] trackable_view_coro_handle_executor pop_earliest()
        {
            std::pop_heap(m_entries.begin(), m_entries.end(), later);
            auto _ = utils::finally{[&]() noexcept { m_entries.pop_back(); }};
            return std::move(m_entries.back().view);
        }

    private:
        std::vector<entry> m_entries{};
    };

} // namespace ito::details

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

            run_until_complete_impl(h.template get<void>());

            return h->get_result();
        }

        template<typename T>
        ito::task<T> create_task(ito::coro<T>&& coro)
        {
            auto h           = static_cast<details::utils::raii_coroutine_handle<>>(std::move(coro).detach());
            auto [obj, view] = details::utils::trackable<details::utils::raii_coroutine_handle<>>::create(std::move(h));

            m_queue.emplace_back(std::in_place_type_t<details::trackable_view_raii_coro_handle_executor>{}, std::move(view));

            return ito::task<T>{std::move(obj)};
        }

        template<typename Fn>
            requires std::invocable<std::decay_t<Fn>&&>
        void call_soon(Fn&& callback)
        {
            m_queue.emplace_back(std::in_place_type_t<std::function<void()>>{}, std::forward<Fn>(callback));
        }


        [[nodiscard]] static auto sleep_until(std::chrono::steady_clock::time_point deadline)
        {
            class awaitable
            {
            public:
                explicit awaitable(std::chrono::steady_clock::time_point deadline) noexcept
                    : m_deadline(deadline)
                {
                }

                [[nodiscard]] bool await_ready() const noexcept { return std::chrono::steady_clock::now() >= m_deadline; }

                void await_suspend(std::coroutine_handle<> h)
                {
                    auto [obj, view] = details::utils::trackable<std::coroutine_handle<>>::create(h);
                    m_handle.emplace(std::move(obj));
                    loop::current().m_timers.push(m_deadline, std::move(view));
                }

                static constexpr void await_resume() noexcept { }

            private:
                std::chrono::steady_clock::time_point                             m_deadline;
                std::optional<details::utils::trackable<std::coroutine_handle<>>> m_handle{};
            };

            return awaitable{deadline};
        }

        template<typename Rep, typename Period>
        [[nodiscard]] static auto sleep_for(std::chrono::duration<Rep, Period> duration)
        {
            return sleep_until(std::chrono::steady_clock::now() + duration);
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
                m_queue.emplace_back(std::in_place_type_t<details::coro_handle_executor>{}, h);
            else
                h.resume();

            while (!h.done() && (!m_queue.empty() || !m_timers.empty()))
            {
                if (!m_timers.empty())
                {
                    const auto now = std::chrono::steady_clock::now();
                    while (!m_timers.empty() && m_timers.next_deadline() <= now)
                        m_queue.emplace_back(std::in_place_type_t<details::trackable_view_coro_handle_executor>{}, m_timers.pop_earliest());
                }

                if (m_queue.empty())
                {
                    std::this_thread::sleep_until(m_timers.next_deadline());
                    continue;
                }

                const auto _ = details::utils::finally{[this]() noexcept { m_queue.pop_front(); }};
                std::visit([](auto&& v) { std::forward<decltype(v)>(v)(); }, std::move(m_queue.front()));
            }
        }

    private:
        using variant_t = std::variant<
            details::coro_handle_executor,
            details::trackable_view_raii_coro_handle_executor,
            details::trackable_view_coro_handle_executor,
            std::function<void()>>;
        std::deque<variant_t> m_queue{};
        details::timer_queue  m_timers{};
    };
} // namespace ito
