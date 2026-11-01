#pragma once

#include <stdexcept>

namespace ito::exceptions
{
    struct ito_exception : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ITO_EXCEPTION(name)                  \
    struct name final : public ito_exception \
    {                                        \
        using ito_exception::ito_exception;  \
    };

    ITO_EXCEPTION(value_is_set);
    ITO_EXCEPTION(empty_value);
    ITO_EXCEPTION(invalid_coro_handle_state);
    ITO_EXCEPTION(invalid_loop_state);

#undef ITO_EXCEPTION
} // namespace ito::exceptions
