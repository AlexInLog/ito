#pragma once

#include <coroutine>
#include <utility>

namespace ito::details::utils
{
    class raii_coroutine_handle_base
    {
    protected:
        explicit raii_coroutine_handle_base(std::coroutine_handle<>&& h)
            : m_h{h}
        {
        }

    public:
        raii_coroutine_handle_base(const raii_coroutine_handle_base&) = delete;
        raii_coroutine_handle_base(raii_coroutine_handle_base&& other) noexcept
            : m_h{std::exchange(other.m_h, {})}
        {
        }

        raii_coroutine_handle_base& operator=(const raii_coroutine_handle_base&)     = delete;
        raii_coroutine_handle_base& operator=(raii_coroutine_handle_base&&) noexcept = delete;

        ~raii_coroutine_handle_base() noexcept
        {
            if (m_h) m_h.destroy();
        }

        operator bool() const { return !!m_h; }

    protected:
        std::coroutine_handle<> m_h{};
    };

    template<typename TPromise>
    class raii_coroutine_handle final : public raii_coroutine_handle_base
    {
    public:
        explicit raii_coroutine_handle(std::coroutine_handle<>&& h)
            : raii_coroutine_handle_base(std::move(h))
        {
        }

        TPromise*                       operator->() const { return &(**this).promise(); }
        std::coroutine_handle<TPromise> operator*() const { return std::coroutine_handle<TPromise>::from_address(m_h.address()); }

        std::coroutine_handle<TPromise> detach() &&
        {
            return std::coroutine_handle<TPromise>::from_address(std::exchange(m_h, {}).address());
        }
    };
} // namespace ito::details::utils
