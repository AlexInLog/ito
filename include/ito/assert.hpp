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

// NOLINTNEXTLINE()
#define ITO_ENSURE(condition) \
    if (!(condition))         \
        throw_ensure(#condition " failed");

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ITO_ASSERT(condition) assert(condition);
