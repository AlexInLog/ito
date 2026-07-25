#pragma once

#include <coroutine>
#include <type_traits>
#include <utility>

namespace ito::details::utils
{
    class raii_coroutine_handle_base
    {
    protected:
        explicit raii_coroutine_handle_base(std::coroutine_handle<> h)
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

        explicit operator bool() const { return !!m_h; }

    protected:
        [[nodiscard]] std::coroutine_handle<> get_impl() const & { return m_h; }
        [[nodiscard]] std::coroutine_handle<> detach_impl() && { return std::exchange(m_h, {}); }

    private:
        std::coroutine_handle<> m_h{};
    };

    template<typename TPromise = void>
    class raii_coroutine_handle final : private raii_coroutine_handle_base
    {
    public:
        explicit raii_coroutine_handle(std::coroutine_handle<>&& h)
            : raii_coroutine_handle_base(std::move(h))
        {
        }

        explicit operator raii_coroutine_handle<>() && {
            return raii_coroutine_handle<>{std::move(*this).detach_impl()};
        }

        using raii_coroutine_handle_base::operator bool;

        TPromise* operator->() const { return &get().promise(); }

        template<typename TargetType = TPromise>
            requires (std::is_void_v<TargetType> || std::is_void_v<TPromise> || std::same_as<TargetType, TPromise>)
        [[nodiscard]] std::coroutine_handle<TargetType> get() const
        {
            return std::coroutine_handle<TargetType>::from_address(get_impl().address());
        }
    };
} // namespace ito::details::utils
