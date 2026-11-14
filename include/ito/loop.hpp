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
    // A sleeping coroutine's entry in the timer heap: its frame's ready-queue node, anchored to that frame (the anchor
    // lives in the sleep's awaitable), so get() returns null once the frame is gone. Heap entries move during sifts,
    // which a node linked into an intrusive list couldn't follow, hence the anchor instead.
    using queued_resume = utils::anchored<ready_node*>;

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
} // namespace ito::details

namespace ito::details
{
    struct loop_internals;
} // namespace ito::details

namespace ito
{
    // A callback queued with loop::call_soon(). It owns the callback and its queue node inline, so queuing allocates
    // nothing beyond what move_only_callback needs. Destroying it cancels the callback if it hasn't run yet, so keep it
    // alive until then; moving it keeps its place in the queue. It may outlive the loop: the loop unlinks it on
    // destruction, and the callback then never runs.
    class [[nodiscard("destroying a scheduled_callback cancels it")]] scheduled_callback final : private details::ready_node
    {
        template<typename Fn>
        scheduled_callback(details::utils::intrusive_queue<details::ready_node>& queue, Fn&& callback)
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
        details::utils::move_only_callback m_callback;
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
            // A named member rather than a class local to operator co_await(): a local class can't have the member
            // template await_suspend() needs.
            struct awaitable
            {
                [[nodiscard]] bool await_ready() const noexcept { return std::chrono::steady_clock::now() >= deadline; }

                // Typed handle: the timer queues the sleeping frame's own node, which only ito coroutines have.
                template<std::derived_from<details::base_promise_type> Promise>
                void await_suspend(std::coroutine_handle<Promise> h)
                {
                    auto [resume, anchor] = details::queued_resume::create(static_cast<details::ready_node*>(&h.promise()));
                    frame_anchor.emplace(std::move(anchor));
                    loop::current().m_timers.push(deadline, std::move(resume));
                }

                static constexpr void await_resume() noexcept { }

                std::chrono::steady_clock::time_point deadline;
                // lives in the sleeping coroutine's frame, so its timer entry stops pointing at the frame once it's gone
                std::optional<details::utils::anchor> frame_anchor{};
            };

        public:
            explicit sleep_awaitable(std::chrono::steady_clock::time_point deadline) noexcept
                : m_deadline(deadline)
            {
            }

            auto operator co_await() { return awaitable{.deadline = m_deadline}; }

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

            run_until_complete_impl(h.template get<void>(), *h.promise());

            return h->get_result();
        }

        template<typename T>
        ito::task<T> create_task(ito::coro<T>&& coro)
        {
            auto h = std::move(coro).detach();
            m_queue.push_back(*h.promise());
            return ito::task<T>{static_cast<details::utils::raii_coroutine_handle<>>(std::move(h))};
        }

        // Runs `callback` on a later tick, unless the returned scheduled_callback is destroyed first. The callback is
        // cancelled together with whatever owns its scheduled_callback, so it may reference that owner's state, e.g.
        // a coroutine's locals when the scheduled_callback is one of them.
        template<typename Fn>
            requires details::loop_callback<Fn>
        [[nodiscard]] scheduled_callback call_soon(Fn&& callback)
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
        // `root` is h's promise: the frame this run waits for.
        void run_until_complete_impl(std::coroutine_handle<> h, details::ready_node& root)
        {
            if (!m_queue.empty())
                m_queue.push_back(root);
            else
                h.resume();

            while (!h.done() && (!m_queue.empty() || !m_timers.empty()))
            {
                if (!m_timers.empty())
                {
                    const auto now = std::chrono::steady_clock::now();
                    while (!m_timers.empty() && m_timers.next_deadline() <= now)
                    {
                        if (auto* const node = m_timers.earliest().get()) // null once the sleeping frame is gone
                            m_queue.push_back(**node);
                        m_timers.pop_earliest();
                    }

                    if (m_queue.empty())
                    {
                        m_timers.drop_cancelled_earliest();
                        if (!m_timers.empty())
                            std::this_thread::sleep_until(m_timers.next_deadline());
                        continue;
                    }
                }

                // TODO: run only the entries ready when this tick started (a batch spliced out of m_queue), so that
                // anything they schedule waits for the next tick and timers are checked once per tick.
                m_queue.pop_front().run();
            }
        }

    private:
        details::utils::intrusive_queue<details::ready_node> m_queue{};
        details::timer_queue                                 m_timers{};
    };
} // namespace ito

namespace ito::details
{
    // The library's own awaitables queue a frame through this rather than through a public loop API.
    struct loop_internals
    {
        // Queues `node` to run on a later tick. Can't fail: queuing only links the node.
        static void schedule(loop& l, ready_node& node) noexcept { l.m_queue.push_back(node); }
    };
} // namespace ito::details
