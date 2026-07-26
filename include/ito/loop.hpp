#pragma once


#include <ito/coro.hpp>
#include <ito/details/utils/finally.hpp>
#include <ito/details/utils/raii_coroutine_handle.hpp>
#include <ito/details/utils/trackable.hpp>
#include <ito/exceptions.hpp>
#include <ito/task.hpp>

#include <concepts>
#include <coroutine>
#include <deque>
#include <functional>
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

    struct trackable_view_coro_handle_executor
    {
        details::utils::trackable<details::utils::raii_coroutine_handle<>>::weak_view handle;

        void operator()() const
        {
            if (const auto* ptr = handle.get())
                ptr->get().resume();
        }
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
            auto h    = static_cast<details::utils::raii_coroutine_handle<>>(std::move(coro).detach());
            auto pair = details::utils::trackable<details::utils::raii_coroutine_handle<>>::create(std::move(h));

            m_queue.emplace_back(std::in_place_type_t<details::trackable_view_coro_handle_executor>{}, std::move(pair.second));

            return ito::task<T>{std::move(pair.first)};
        }

        template<typename Fn>
            requires std::invocable<std::decay_t<Fn>&&>
        void call_soon(Fn&& callback)
        {
            m_queue.emplace_back(std::in_place_type_t<std::function<void()>>{}, std::forward<Fn>(callback));
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

            while (!h.done() && !m_queue.empty())
            {
                const auto _ = details::utils::finally{[this]() noexcept { m_queue.pop_front(); }};
                std::visit([](auto&& v) { std::forward<decltype(v)>(v)(); }, std::move(m_queue.front()));
            }
        }

    private:
        using variant_t = std::variant<details::coro_handle_executor, details::trackable_view_coro_handle_executor, std::function<void()>>;
        std::deque<variant_t> m_queue{};
    };
} // namespace ito
