#pragma once

#include <ito/details/utils/error_or_optional.hpp>
#include <ito/loop.hpp>

#include <coroutine>
#include <exception>

namespace ito::exceptions
{
    struct future_just_awaited final : public ito_exception
    {
        using ito_exception::ito_exception;
    };
} // namespace ito::exceptions

namespace ito::async
{
    namespace details
    {
        template<typename T = void>
        class future_base
        {
        public:
            future_base()           = default;
            ~future_base() noexcept = default;

            future_base(const future_base&)            = delete;
            future_base& operator=(const future_base&) = delete;
            future_base(future_base&&)                 = delete;
            future_base& operator=(future_base&&)      = delete;

            template<typename... TT>
            void set_result_impl(TT&&... v)
            {
                auto prepared = this->prepare_scheduling_continuation();
                m_value.set_result(std::forward<TT>(v)...);
                prepared();
            }

            void set_exception(const std::exception_ptr& err)
            {
                auto prepared = this->prepare_scheduling_continuation();
                m_value.set_exception(err);
                prepared();
            }

            [[nodiscard]] bool is_ready() const { return this->m_value.is_ready(); }

            auto operator co_await() &
            {
                if (this->m_continuation) throw ito::exceptions::future_just_awaited{"future is just was awaited before"};

                struct awaitable
                {
                    future_base<T>* self{};

                    constexpr bool await_ready() noexcept { return self->m_value.is_ready(); }
                    auto           await_suspend(std::coroutine_handle<> h) noexcept { self->m_continuation = h; }
                    T              await_resume() { return self->m_value.get_result(); }
                };
                return awaitable{this};
            }

        private:
            [[nodiscard]] auto prepare_scheduling_continuation() const
            {
                return [loop = m_continuation ? &ito::loop::current() : nullptr, this]() {
                    if (loop) loop->call_soon(m_continuation);
                };
            }

        private:
            ito::details::utils::error_or_optional<T> m_value{};
            std::coroutine_handle<>                   m_continuation{};
        };

    } // namespace details

    template<typename T = void>
    class future final : private details::future_base<T>
    {
    public:
        void set_result(const T& v) { this->set_result_impl(v); }
        void set_result(T&& v) { this->set_result_impl(std::move(v)); }

        using details::future_base<T>::set_exception;
        using details::future_base<T>::is_ready;
        using details::future_base<T>::operator co_await;
    };

    template<>
    class future<void> final : private details::future_base<void>
    {
    public:
        void set_result() { this->set_result_impl(); }

        using details::future_base<void>::set_exception;
        using details::future_base<void>::is_ready;
        using details::future_base<void>::operator co_await;
    };

} // namespace ito::async
