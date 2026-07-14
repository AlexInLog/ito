#pragma once

#include <type_traits>
#include <utility>

namespace ito::details::utils
{
    template<typename Fn>
        requires (std::is_nothrow_invocable_v<Fn>)
    class finally
    {
    public:
        finally& operator=(const finally&) = delete;
        finally& operator=(finally&&)      = delete;

        explicit finally(Fn&& fn)
            : m_fn{std::move(fn)}
        {
        }
        explicit finally(const Fn& fn)
            : m_fn{fn}
        {
        }

        finally(const finally&)     = delete;
        finally(finally&&) noexcept = delete;

        ~finally() noexcept { std::move(m_fn)(); }

    private:
        [[no_unique_address]] Fn m_fn;
    };

    template<typename T>
    finally(const T&) -> finally<T>;
} // namespace ito::details::utils
