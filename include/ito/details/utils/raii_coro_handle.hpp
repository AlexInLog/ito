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

        explicit raii_coroutine_handle(raii_coroutine_handle<TPromise>&& other) noexcept
            : raii_coroutine_handle_base{std::move(other).detach()}
        {
        }

        raii_coroutine_handle(const raii_coroutine_handle<TPromise>&) = delete;

        // std::coroutine_handle<TPromise> operator*() const { return std::coroutine_handle<TPromise>::from_address(m_h.address()); }

        std::coroutine_handle<TPromise> detach() &&
        {
            return std::coroutine_handle<TPromise>::from_address(std::exchange(m_h, {}).address());
        }
    };
} // namespace ito::details::utils
