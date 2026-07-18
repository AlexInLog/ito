#pragma once

#include <ito/details/utils/error_or_optional.hpp>
#include <ito/loop.hpp>

#include <coroutine>
#include <exception>

namespace ito::async
{
    template<typename T>
    class future
    {
    public:
        void set_result(const T& v)
        {
            m_value.set_result(v);
            this->schedule_continuation();
        }

        void set_result(T&& v)
        {
            m_value.set_result(std::move(v));
            this->schedule_continuation();
        }

        void set_exception(const std::exception_ptr& err)
        {
            m_value.set_exception(err);
            this->schedule_continuation();
        }

        [[nodiscard]] bool is_ready() const { return this->m_value.is_ready(); }

        auto operator co_await() &
        {
            struct awaitable
            {
                future<T>* self{};

                constexpr bool await_ready() noexcept { return self->m_value.is_ready(); }
                auto           await_suspend(std::coroutine_handle<> h) noexcept { self->m_continuation = h; }
                T              await_resume() { return self->m_value.get_result(); }
            };
            return awaitable{this};
        }

    private:
        void schedule_continuation() const
        {
            if (!m_continuation) return;

            ito::loop::current().call_soon(m_continuation);
        }

    private:
        ito::details::utils::error_or_optional<T> m_value{};
        std::coroutine_handle<>                   m_continuation{};
    };
} // namespace ito::async
