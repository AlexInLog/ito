#pragma once

#include <algorithm>
#include <type_traits>
namespace ito::utils
{
    template<class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;


    template<typename Fn>
        requires (std::is_nothrow_invocable_v<Fn>)
    class finally
    {
    public:
        explicit finally(Fn&& fn)
            : m_fn{std::move(fn)}
        {
        }
        explicit finally(const Fn& fn)
            : m_fn{fn}
        {
        }

        finally(const finally&) = delete;
        finally(finally&&) noexcept = delete;

        ~finally() noexcept { std::move(m_fn)(); }

    private:
        Fn m_fn;
    };

    template<typename T>
    finally(const T&) -> finally<T>;
} // namespace ito::utils
