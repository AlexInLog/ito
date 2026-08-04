#pragma once

#include <coroutine>
#include <utility>

namespace ito::details::utils
{
    template<typename T = void>
    class coroutine_handle
    {
    public:
        coroutine_handle() = default;
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
        // Deliberately non-owning: unlike raii_coroutine_handle, this type never calls .destroy() on
        // the wrapped handle. It only exists to give a plain std::coroutine_handle<T> move-clears-source
        // semantics (see the move constructor above), not to manage the coroutine frame's lifetime.
        ~coroutine_handle() noexcept = default;

        std::coroutine_handle<T>*       operator->() { return &m_handle; }
        const std::coroutine_handle<T>* operator->() const { return &m_handle; }

        [[nodiscard]] std::coroutine_handle<T> get() const { return m_handle; }

        [[nodiscard]] std::coroutine_handle<T> detach() && { return std::exchange(m_handle, {}); }

        explicit operator bool() const { return static_cast<bool>(m_handle); }

    private:
        std::coroutine_handle<T> m_handle{};
    };
} // namespace ito::details::utils
