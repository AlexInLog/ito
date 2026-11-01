#pragma once

#include <ito/coro.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/details/utils/move_only_callback.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/details/utils/trackable.hpp>
#include <ito/exceptions.hpp>
#include <ito/fwd.hpp>
#include <ito/task.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <deque>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ito::details
{
    // Coroutine entries the loop can queue. Always views, never raw handles: the frame they resume may be destroyed
    // while they're queued, and the view then turns the resume into a no-op. Resuming never throws: a coroutine
    // body's exceptions are captured by its promise.
    template<typename T>
    concept loop_handle = std::same_as<T, utils::trackable<utils::raii_coroutine_handle<>>::weak_view>
                       || std::same_as<T, utils::trackable<std::coroutine_handle<>>::weak_view>;

    // Callbacks must not throw (move_only_callback only accepts nothrow-invocable callables): nobody awaits a
    // callback, so its exception would otherwise unwind whichever run_until_complete() happens to be running,
    // destroying an unrelated coroutine.
    template<typename Fn>
    concept loop_callback = std::constructible_from<utils::move_only_callback, Fn&&>;

    struct loop_entry_executor
    {
        void operator()(const details::utils::trackable<details::utils::raii_coroutine_handle<>>::weak_view& handle) const
        {
            if (const auto* ptr = handle.get())
                ptr->get().resume();
        }

        void operator()(const details::utils::trackable<std::coroutine_handle<>>::weak_view& handle) const
        {
            if (const auto* ptr = handle.get())
                ptr->resume();
        }
        void operator()(utils::move_only_callback& callback) const { callback(); }
    };

    class timer_queue
    {
        struct entry
        {
            std::chrono::steady_clock::time_point                         deadline;
            details::utils::trackable<std::coroutine_handle<>>::weak_view view;
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

        [[nodiscard]] details::utils::trackable<std::coroutine_handle<>>::weak_view pop_earliest()
        {
            std::pop_heap(m_entries.begin(), m_entries.end(), later);
            const auto _ = utils::finally{[this]() noexcept { m_entries.pop_back(); }};
            return std::move(m_entries.back().view);
        }

        // Only the earliest entries matter for how long the loop may sleep, so cancelled ones deeper in the
        // heap are left in place until they surface here.
        void drop_cancelled_earliest()
        {
            while (!m_entries.empty() && !m_entries.front().view.get())
            {
                std::pop_heap(m_entries.begin(), m_entries.end(), later);
                m_entries.pop_back();
            }
        }

    private:
        std::vector<entry> m_entries{};
    };
} // namespace ito::details

namespace ito
{
    class loop
    {
        [[noreturn]] static inline void throw_loop_already_running()
        {
            throw exceptions::invalid_loop_state{"loop::run_until_complete() called from within an already-running loop on this thread"};
        }

        [[noreturn]] static inline void throw_no_active_loop()
        {
            throw exceptions::invalid_loop_state{"no loop is currently running on this thread"};
        }

        auto lock()
        {
            auto& current_loop_ptr = current_impl();
            if (!current_loop_ptr) [[likely]]
            {
                current_loop_ptr = this;
                return details::utils::finally{[]() noexcept { current_impl() = nullptr; }};
            }
            throw_loop_already_running();
        }

        static loop*& current_impl()
        {
            static thread_local loop* s_loop{};
            return s_loop;
        }

    public:
        class sleep_awaitable
        {
        public:
            explicit sleep_awaitable(std::chrono::steady_clock::time_point deadline) noexcept
                : m_deadline(deadline)
            {
            }

            auto operator co_await()
            {
                struct awaitable
                {
                    [[nodiscard]] bool await_ready() const noexcept { return std::chrono::steady_clock::now() >= deadline; }

                    void await_suspend(std::coroutine_handle<> h)
                    {
                        auto [obj, view] = details::utils::trackable<std::coroutine_handle<>>::create(h);
                        handle.emplace(std::move(obj));
                        loop::current().m_timers.push(deadline, std::move(view));
                    }

                    static constexpr void await_resume() noexcept { }

                    std::chrono::steady_clock::time_point                             deadline;
                    std::optional<details::utils::trackable<std::coroutine_handle<>>> handle{};
                };

                return awaitable{.deadline = m_deadline};
            }

        private:
            std::chrono::steady_clock::time_point m_deadline;
        };

    public:
        loop() = default;

        using loop_entry_t = std::variant<
            details::utils::trackable<details::utils::raii_coroutine_handle<>>::weak_view,
            details::utils::trackable<std::coroutine_handle<>>::weak_view,
            details::utils::move_only_callback>;

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

            m_queue.emplace_back(
                std::in_place_type_t<details::utils::trackable<details::utils::raii_coroutine_handle<>>::weak_view>{},
                std::move(view)
            );

            return ito::task<T>{std::move(obj)};
        }

        // A callback may run after the coroutine that queued it has finished (later in this run, or in a later
        // run): own what it touches, e.g. capture a promise by move, rather than referencing a coroutine's locals.
        template<typename Fn>
            requires details::loop_handle<std::remove_cvref_t<Fn>> || details::loop_callback<Fn>
        void call_soon(Fn&& callback)
        {
            if constexpr (details::loop_handle<std::remove_cvref_t<Fn>>)
                m_queue.emplace_back(std::forward<Fn>(callback));
            else
                m_queue.emplace_back(std::in_place_type_t<details::utils::move_only_callback>{}, std::forward<Fn>(callback));
        }

        [[nodiscard]] static auto sleep_until(std::chrono::steady_clock::time_point deadline) { return sleep_awaitable{deadline}; }

        template<typename Rep, typename Period>
        [[nodiscard]] static auto sleep_for(std::chrono::duration<Rep, Period> duration)
        {
            return sleep_until(std::chrono::steady_clock::now() + duration);
        }

        static loop& current()
        {
            const auto l = try_current();
            if (l) [[likely]]
                return *l;

            throw_no_active_loop();
        }
        static loop* try_current() noexcept { return current_impl(); }

    private:
        void run_until_complete_impl(std::coroutine_handle<> h)
        {
            // Queued as a view rather than a raw handle: if the loop throws before reaching it (queued entries can't
            // throw, so this means an allocation failure in the loop itself), the exception destroys `h`'s frame along
            // with `tracked`, and a later run of this loop skips the stale entry instead of resuming it.
            std::optional<details::utils::trackable<std::coroutine_handle<>>> tracked{};
            if (!m_queue.empty())
            {
                auto [obj, view] = details::utils::trackable<std::coroutine_handle<>>::create(h);
                tracked.emplace(std::move(obj));
                m_queue.emplace_back(
                    std::in_place_type_t<details::utils::trackable<std::coroutine_handle<>>::weak_view>{},
                    std::move(view)
                );
            }
            else
                h.resume();

            while (!h.done() && (!m_queue.empty() || !m_timers.empty()))
            {
                if (!m_timers.empty())
                {
                    const auto now = std::chrono::steady_clock::now();
                    while (!m_timers.empty() && m_timers.next_deadline() <= now)
                        m_queue.emplace_back(
                            std::in_place_type_t<details::utils::trackable<std::coroutine_handle<>>::weak_view>{},
                            m_timers.pop_earliest()
                        );

                    if (m_queue.empty())
                    {
                        m_timers.drop_cancelled_earliest();
                        if (!m_timers.empty())
                            std::this_thread::sleep_until(m_timers.next_deadline());
                        continue;
                    }
                }

                // Runs only the entries ready when this tick started (including the timers just moved in);
                // anything they schedule waits for the next tick, so timers are checked once per tick.
                for (auto n = m_queue.size(); n > 0 && !h.done(); --n)
                {
                    const auto _ = details::utils::finally{[this]() noexcept { m_queue.pop_front(); }};
                    std::visit(details::loop_entry_executor{}, m_queue.front());
                }
            }
        }

    private:
        std::deque<loop_entry_t> m_queue{};
        details::timer_queue     m_timers{};
    };
} // namespace ito
