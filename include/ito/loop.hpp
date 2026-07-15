#pragma once

#include <ito/coro.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/exceptions.hpp>

#include <utility>

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
            h.get().resume();

            return h->get_result();
        }

        // static loop& current()
        // {
        //     if (auto loop = try_current()) return *loop;
        //     throw exceptions::invalid_loop_state{"no loop is currently running on this thread"};
        // }
        // static loop* try_current() noexcept { return current_impl(); }

    private:
    };
} // namespace ito
