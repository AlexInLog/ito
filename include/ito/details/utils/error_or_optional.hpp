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

        T&& get_result_impl()
        {
            return std::visit(
                ito::details::utils::overloaded{
                    [](const std::monostate&) -> T&& { throw ito::exceptions::empty_value{"empty value"}; },
                    [](T&& v) -> T&& { return std::move(v); },
                    [](const std::exception_ptr& e) -> T&& { std::rethrow_exception(e); }
                },
                std::move(m_value)
            );
        };

    private:
        void ensure_not_set()
        {
            if (is_ready()) [[unlikely]]
                throw ito::exceptions::value_is_set{"value is just set"};
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
