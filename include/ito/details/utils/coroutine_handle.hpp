#pragma once

#include <coroutine>
#include <utility>

namespace ito::details::utils
{
    template<typename T = void>
    class coroutine_handle
    {
    public:
        explicit coroutine_handle(std::coroutine_handle<T> h)
            : m_handle(std::move(h))
        {
        }
        coroutine_handle(const coroutine_handle&) = default;
        coroutine_handle(coroutine_handle&& o) noexcept
            : m_handle(std::exchange(o.m_handle, {}))
        {
        }
        coroutine_handle& operator=(const coroutine_handle&) = default;
        coroutine_handle& operator=(coroutine_handle&& o) noexcept
        {
            m_handle = std::exchange(o.m_handle, {});
            return *this;
        }
        ~coroutine_handle() noexcept = default;

        std::coroutine_handle<T>*       operator->() { return &m_handle; }
        const std::coroutine_handle<T>* operator->() const { return &m_handle; }

        explicit operator bool() const { return m_handle; }

    private:
        std::coroutine_handle<T> m_handle{};
    };
} // namespace ito::details::utils
