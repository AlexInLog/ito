#pragma once

#include <ito/details/utils/void.hpp>
#include <ito/exceptions.hpp>
#include <ito/fwd.hpp>

#include <exception>
#include <type_traits>
#include <variant>

namespace ito::details::utils
{
    // The failure paths live in their own functions: constructing and throwing the exception is most of
    // the code of the accessors below, and having it there is what keeps those accessors from being
    // inlined into their callers, even though it only ever runs when something went wrong.
    [[noreturn]] inline void throw_empty_value()
    {
        throw ito::exceptions::empty_value{"empty value"};
    }

    [[noreturn]] inline void throw_value_is_set()
    {
        throw ito::exceptions::value_is_set{"value is already set"};
    }

    // Reports why a result could not be handed out: rethrows the stored exception, or reports the
    // still-empty state (which is also how a valueless_by_exception state is reported, consistently
    // with outcome::is_ready()).
    template<typename Storage>
    [[noreturn]] void rethrow_or_throw_empty(const Storage& value)
    {
        if (const auto err = std::get_if<2>(&value)) [[likely]]
            std::rethrow_exception(*err);

        throw_empty_value();
    }

    template<typename T>
    class outcome
    {
    public:
        using stored_t = std::conditional_t<std::is_void_v<T>, void_t, T>;

        void set_exception(const std::exception_ptr& err)
        {
            ensure_not_set();
            m_value.template emplace<2>(err);
        }

        [[nodiscard]] bool is_ready() const { return !m_value.valueless_by_exception() && m_value.index() != 0; }

        void set_result()
            requires (std::is_void_v<T>)
        {
            ensure_not_set();
            m_value.template emplace<1>(void_t{});
        }

        void set_result(stored_t&& v)
            requires (!std::is_void_v<T>)
        {
            ensure_not_set();
            m_value.template emplace<1>(std::move(v));
        }

        void set_result(const stored_t& v)
            requires (!std::is_void_v<T>)
        {
            ensure_not_set();
            m_value.template emplace<1>(v);
        }

        // TODO: clear state
        std::add_rvalue_reference_t<T> get_result()
        {
            // Deliberately not std::visit(): visiting dispatches through a function table, which neither
            // compiler manages to fold away, while this is the hot path of every awaited future.
            if (const auto value = std::get_if<1>(&m_value)) [[likely]]
            {
                if constexpr (std::is_void_v<T>)
                    return;
                else
                    return std::move(*value);
            }

            rethrow_or_throw_empty(m_value);
        };

    private:
        void ensure_not_set()
        {
            if (is_ready()) [[unlikely]]
                throw_value_is_set();
        }

    private:
        std::variant<std::monostate, stored_t, std::exception_ptr> m_value{};
    };
} // namespace ito::details::utils
