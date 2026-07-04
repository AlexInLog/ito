#pragma once

#include <stdexcept>

namespace ito::exceptions
{
    struct ito_exception : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct empty_value final : public ito_exception
    {
        using ito_exception::ito_exception;
    };

    struct invalid_coro_handle_state final : public ito_exception
    {
        using ito_exception::ito_exception;
    };

} // namespace ito::exceptions
