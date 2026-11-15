#pragma once

#include <ito/coro.hpp>
#include <ito/details/ready_node.hpp>
#include <ito/details/utils/anchored.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/details/utils/intrusive_queue.hpp>
#include <ito/details/utils/move_only_callback.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/exceptions.hpp>
#include <ito/fwd.hpp>
#include <ito/task.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ito::details
{
    // A queued coroutine resume. Never a raw handle: the frame it resumes may be destroyed while it's queued, so the
    // handle is anchored to that frame (an anchor lives in or next to it) and get() returns null once it's gone,
    // turning the resume into a no-op. Resuming never throws: a coroutine body's exceptions are captured by its promise.
    using queued_resume = utils::anchored<std::coroutine_handle<>>;

    // Coroutine entries the loop can queue.
    template<typename T>
    concept loop_handle = std::same_as<T, queued_resume>;

    // Callbacks must not throw (move_only_callback only accepts nothrow-invocable callables): nobody awaits a
    // callback, so its exception would otherwise unwind whichever run_until_complete() happens to be running,
    // destroying an unrelated coroutine.
    template<typename Fn>
    concept loop_callback = std::constructible_from<utils::move_only_callback, Fn&&>;

    class timer_queue
    {
        struct entry
        {
            std::chrono::steady_clock::time_point deadline;
            queued_resume                         resume;
        };

        static constexpr bool later(const entry& a, const entry& b) { return a.deadline > b.deadline; }

    public:
        void push(std::chrono::steady_clock::time_point deadline, queued_resume resume)
        {
            m_entries.push_back(entry{.deadline = deadline, .resume = std::move(resume)});
            std::push_heap(m_entries.begin(), m_entries.end(), later);
        }

        [[nodiscard]] bool empty() const { return m_entries.empty(); }

        [[nodiscard]] std::chrono::steady_clock::time_point next_deadline() const { return m_entries.front().deadline; }

        // Read separately from pop_earliest() so the resume can be moved out first and the entry dropped only once
        // that succeeded: if moving it into the ready queue fails to allocate, the timer stays in place.
        [[nodiscard]] queued_resume& earliest() { return m_entries.front().resume; }

        void pop_earliest()
        {
            std::pop_heap(m_entries.begin(), m_entries.end(), later);
            m_entries.pop_back();
        }

        // Only the earliest entries matter for how long the loop may sleep, so cancelled ones deeper in the
        // heap are left in place until they surface here.
        void drop_cancelled_earliest()
        {
            while (!m_entries.empty() && !m_entries.front().resume.get()) pop_earliest();
        }

    private:
        std::vector<entry> m_entries{};
    };

    struct loop_internals;
} // namespace ito::details

namespace ito
{
    class loop
    {
        friend struct details::loop_internals;

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
                        auto [resume, anchor] = details::queued_resume::create(h);
                        frame_anchor.emplace(std::move(anchor));
                        loop::current().m_timers.push(deadline, std::move(resume));
                    }

                    static constexpr void await_resume() noexcept { }

                    std::chrono::steady_clock::time_point deadline;
                    // lives in the sleeping coroutine's frame, so its queued resume dies with that frame
                    std::optional<details::utils::anchor> frame_anchor{};
                };

                return awaitable{.deadline = m_deadline};
            }

        private:
            std::chrono::steady_clock::time_point m_deadline;
        };

    public:
        loop() = default;

        template<typename T>
        T run_until_complete(ito::coro<T>&& coro)
        {
            [[maybe_unused]] const auto locked = lock();

            details::utils::raii_coroutine_handle<typename ito::coro<T>::promise_type> h = std::move(coro).detach();
            run_until_complete_impl(h.template get<void>(), h.promise());
            return h->get_result();
        }

        template<typename T>
        ito::task<T> create_task(ito::coro<T>&& coro)
        {
            auto h = std::move(coro).detach();
            m_queue.push_back(h.promise());
            return ito::task<T>{static_cast<details::utils::raii_coroutine_handle<>>(std::move(h))};
        }

        // A callback may run after the coroutine that queued it has finished (later in this run, or in a later
        // run): own what it touches, e.g. capture a promise by move, rather than referencing a coroutine's locals.
        template<typename Fn>
            requires details::loop_handle<std::remove_cvref_t<Fn>> || details::loop_callback<Fn>
        void call_soon([[maybe_unused]] Fn&& callback)
        {
            // if constexpr (details::loop_handle<std::remove_cvref_t<Fn>>)
            //     m_queue.emplace_back(std::forward<Fn>(callback));
            // else
            //     m_queue.emplace_back(std::in_place_type_t<details::utils::move_only_callback>{}, std::forward<Fn>(callback));
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
        void run_until_complete_impl(std::coroutine_handle<> h, ito::details::base_promise_type& p)
        {
            // If the loop throws while `p` is still queued, unwinding destroys `h`'s frame, and the node in its promise
            // unlinks itself, so a later run of this loop never sees it.
            if (!m_queue.empty())
                m_queue.push_back(p);
            else
                h.resume();

            while (!h.done() && (!m_queue.empty() || !m_timers.empty()))
            {
                // if (!m_timers.empty())
                // {
                //     const auto now = std::chrono::steady_clock::now();
                //     while (!m_timers.empty() && m_timers.next_deadline() <= now)
                //     {
                //         // emplace_back has no effect if it throws, so the resume only leaves the heap once it's queued
                //         m_queue.emplace_back(std::in_place_type_t<details::queued_resume>{}, std::move(m_timers.earliest()));
                //         m_timers.pop_earliest();
                //     }
                //
                //     if (m_queue.empty())
                //     {
                //         m_timers.drop_cancelled_earliest();
                //         if (!m_timers.empty())
                //             std::this_thread::sleep_until(m_timers.next_deadline());
                //         continue;
                //     }
                // }

                // Runs only the entries ready when this tick started (including the timers just moved in);
                // anything they schedule waits for the next tick, so timers are checked once per tick.
                // Stops as soon as `h` is done: the rest of the tick stays queued for a later run.
                m_queue.consume([&h](details::intrusive_ready_node& node) noexcept {
                    node.run();
                    return !h.done();
                });
            }
        }

    private:
        details::utils::intrusive_queue<details::intrusive_ready_node> m_queue{};
        details::timer_queue                                           m_timers{};
    };
} // namespace ito

namespace ito::details
{
    struct loop_internals
    {
        // Queues `node` to run on a later tick. Can't fail: queuing only links the node.
        static void schedule(loop& l, intrusive_ready_node& node) noexcept { l.m_queue.push_back(node); }
    };
} // namespace ito::details
