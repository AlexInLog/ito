#pragma once

#include <ito/details/utils/overloaded.hpp>
#include <ito/exceptions.hpp>

#include <exception>
#include <variant>

namespace ito::details::utils
{
    struct void_t
    {
    };

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
    // with error_or_optional_base::is_ready()).
    template<typename Storage>
    [[noreturn]] void rethrow_or_throw_empty(const Storage& value)
    {
        if (const auto err = std::get_if<2>(&value))
            std::rethrow_exception(*err);

        throw_empty_value();
    }

    template<typename T>
    class error_or_optional_base
    {
    public:
        void set_exception(const std::exception_ptr& err)
        {
            ensure_not_set();
            m_value.template emplace<2>(err);
        }

        [[nodiscard]] bool is_ready() const { return !m_value.valueless_by_exception() && m_value.index() != 0; }

    protected:
        void set_result_impl(T&& v)
        {
            ensure_not_set();
            m_value.template emplace<1>(std::move(v));
        }

        void set_result_impl(const T& v)
        {
            ensure_not_set();
            m_value.template emplace<1>(v);
        }

        // TODO: clear state
        T&& get_result_impl()
        {
            // Deliberately not std::visit(): visiting dispatches through a function table, which neither
            // compiler manages to fold away, while this is the hot path of every awaited future.
            if (const auto value = std::get_if<1>(&m_value)) [[likely]]
                return std::move(*value);

            rethrow_or_throw_empty(m_value);
        };

    private:
        void ensure_not_set()
        {
            if (is_ready()) [[unlikely]]
                throw_value_is_set();
        }

    private:
        std::variant<std::monostate, T, std::exception_ptr> m_value{};
    };

    template<typename T>
    class error_or_optional : private error_or_optional_base<T>
    {
    public:
        void set_result(const T& v) { this->set_result_impl(v); }
        void set_result(T&& v) { this->set_result_impl(std::move(v)); }
        T&&  get_result() { return this->get_result_impl(); }

        using error_or_optional_base<T>::is_ready;
        using error_or_optional_base<T>::set_exception;
    };

    template<>
    class error_or_optional<void> : private error_or_optional_base<void_t>
    {
    public:
        void set_result() { this->set_result_impl(void_t{}); }
        void get_result() { this->get_result_impl(); }

        using error_or_optional_base<void_t>::is_ready;
        using error_or_optional_base<void_t>::set_exception;
    };


} // namespace ito::details::utils
