#pragma once

#include <ito/coro.hpp>
#include <ito/details/ready_node.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/details/utils/intrusive_queue.hpp>
#include <ito/details/utils/move_only_callback.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/exceptions.hpp>
#include <ito/fwd.hpp>
#include <ito/task.hpp>

#include <chrono>
#include <concepts>
#include <coroutine>
#include <thread>
#include <type_traits>
#include <utility>

namespace ito::details
{
    // Callbacks must not throw (move_only_callback only accepts nothrow-invocable callables): nobody awaits a
    // callback, so its exception would otherwise unwind whichever run_until_complete() happens to be running,
    // destroying an unrelated coroutine.
    template<typename Fn>
    concept loop_callback = std::constructible_from<utils::move_only_callback, Fn&&>;

    // One pending sleep. It lives in the sleep's awaitable, in the sleeping frame, and points at that frame's promise:
    // a frame destroys the awaitable, unlinking the timer, before its promise, so the pointer never dangles. Cancelling
    // a sleep is just destroying its frame.
    struct timer_node : utils::intrusive_queue_node
    {
        explicit timer_node(std::chrono::steady_clock::time_point deadline) noexcept
            : deadline{deadline}
        {
        }

        static bool earlier(const timer_node& a, const timer_node& b) noexcept { return a.deadline < b.deadline; }

        std::chrono::steady_clock::time_point deadline;
        // the sleeping frame's promise, queued once the deadline passes; set when the frame suspends
        intrusive_ready_node* frame{};
    };

    struct loop_internals;
} // namespace ito::details

namespace ito
{

    // A callback queued with loop::schedule(). It owns the callback and its queue node inline, so queuing allocates
    // nothing beyond what move_only_callback needs. Destroying it cancels the callback if it hasn't run yet, so keep it
    // alive until then; moving it keeps its place in the queue. It may outlive the loop: the loop unlinks it on
    // destruction, and the callback then never runs.
    class [[nodiscard("destroying a scheduled_callback cancels it")]] scheduled_callback final : private details::intrusive_ready_node
    {
        template<typename Fn>
        scheduled_callback(details::utils::intrusive_queue<details::intrusive_ready_node>& queue, Fn&& callback)
            : m_callback{std::forward<Fn>(callback)}
        {
            queue.push_back(*this);
        }

        // Runs a moved-out copy, so the captures are released as soon as the callback returns rather than when the
        // scheduled_callback dies, and the callback may destroy its own scheduled_callback while it runs.
        void run() noexcept override
        {
            auto callback = std::move(m_callback);
            callback();
        }

    public:
        friend class loop;

        scheduled_callback(scheduled_callback&&) noexcept = default;
        ~scheduled_callback() noexcept                    = default;

        scheduled_callback(const scheduled_callback&)            = delete;
        scheduled_callback& operator=(const scheduled_callback&) = delete;
        scheduled_callback& operator=(scheduled_callback&&)      = delete;

    private:
        // mutable for the same reason as the node's links: run() moves it out even from a `const auto` handle
        mutable details::utils::move_only_callback m_callback;
    };

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

            // A named member rather than a class local to operator co_await(): a local class can't have the member
            // template await_suspend() needs. Returned as a prvalue, so it's never moved once its timer is linked.
            class awaitable
            {
            public:
                explicit awaitable(std::chrono::steady_clock::time_point deadline) noexcept
                    : m_timer{deadline}
                {
                }

                [[nodiscard]] bool await_ready() const noexcept { return std::chrono::steady_clock::now() >= m_timer.deadline; }

                // Typed handle: the timer queues the sleeping frame's own node, which only ito coroutines have.
                template<std::derived_from<details::base_promise_type> Promise>
                void await_suspend(std::coroutine_handle<Promise> h)
                {
                    auto& l       = loop::current(); // throws before anything is linked
                    m_timer.frame = &h.promise();
                    l.m_timers.push_back_sorted(m_timer, details::timer_node::earlier);
                }

                static constexpr void await_resume() noexcept { }

            private:
                details::timer_node m_timer;
            };

            auto operator co_await() const noexcept { return awaitable{m_deadline}; }

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

        // Runs `callback` on a later tick, unless the returned scheduled_callback is destroyed first. The callback is
        // cancelled together with whatever owns its scheduled_callback, so it may reference that owner's state, e.g.
        // a coroutine's locals when the scheduled_callback is one of them.
        template<typename Fn>
            requires details::loop_callback<Fn>
        scheduled_callback schedule(Fn&& callback)
        {
            return scheduled_callback{m_queue, std::forward<Fn>(callback)};
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
                if (!m_timers.empty())
                {
                    const auto now = std::chrono::steady_clock::now();
                    while (!m_timers.empty() && m_timers.front().deadline <= now) m_queue.push_back(*m_timers.pop_front().frame);

                    // Nothing moved, so m_timers is still non-empty. A cancelled sleep has already unlinked its timer, so
                    // the front deadline belongs to a frame that's still waiting.
                    if (m_queue.empty())
                    {
                        std::this_thread::sleep_until(m_timers.front().deadline);
                        continue;
                    }
                }

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
        // sorted by deadline, earliest first; equal deadlines fire in the order they were set
        details::utils::intrusive_queue<details::timer_node> m_timers{};
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
