#pragma once

#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/utils.hpp>

#include <coroutine>
#include <utility>

namespace ito
{
    class loop
    {
    public:
        loop() = default;

        template<typename T>
        T run_until_complete(ito::coro<T>&& coro)
        {
            auto& current_loop_ptr = current_impl();
            if (current_loop_ptr)
            {
                throw exceptions::invalid_loop_state{"loop::run() called from within an already-running loop on this thread"};
            }

            current_loop_ptr                 = this;
            const auto free_current_loop_ptr = utils::finally{[]() noexcept { current_impl() = nullptr; }};

            std::coroutine_handle<typename ito::coro<T>::promise_type> h = std::move(coro).detach();
            if (!h.done())
            {
                h.resume();
            }

            const auto _ = utils::finally{[&h]() noexcept { h.destroy(); }};
            return h.promise().get_result();
        }

        // static loop& current()
        // {
        //     if (auto loop = try_current()) return *loop;
        //     throw exceptions::invalid_loop_state{"no loop is currently running on this thread"};
        // }
        // static loop* try_current() noexcept { return current_impl(); }

    private:
        static loop*& current_impl()
        {
            static thread_local loop* s_loop{};
            return s_loop;
        }
    };
} // namespace ito
