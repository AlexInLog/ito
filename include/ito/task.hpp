#pragma once

#include <coroutine>

namespace ito
{
    class executor;

    class task
    {
    public:
        struct promise_type // NOLINT (readability-identifier-naming.StructCase)
        {
            task get_return_object() { return task{std::coroutine_handle<promise_type>::from_promise(*this)}; }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void return_void() { }

            void unhandled_exception() { }
        };

        ~task() noexcept
        {
            if (m_h) m_h.destroy();
        }

        friend class ito::executor;

    private:
        explicit task(std::coroutine_handle<promise_type> h)
            : m_h{h}
        {
        }

        bool done() const { return (!m_h || m_h.done()); }

        bool run()
        {
            if (done()) return false;

            m_h.resume();
            return m_h.done();
        }


    private:
        std::coroutine_handle<promise_type> m_h{};
    };
} // namespace ito
