#pragma once

#include <ito/exceptions.hpp>

#include <cassert>

namespace ito::details
{
    [[noreturn]] inline void throw_ensure(const char* reason)
    {
        throw ito::exceptions::invalid_condition{reason};
    }
} // namespace ito::details

#define ITO_ENSURE(condition)                                   \
    do                                                          \
    {                                                           \
        if (!(condition))                                       \
            ::ito::details::throw_ensure(#condition " failed"); \
    } while (false)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ITO_ASSERT(condition) assert(condition)
