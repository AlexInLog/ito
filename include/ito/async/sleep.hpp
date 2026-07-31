#pragma once

#include <ito/loop.hpp>

#include <chrono>

namespace ito::async
{
    template<typename Rep, typename Period>
    inline auto sleep_for(std::chrono::duration<Rep, Period> duration)
    {
        return ito::loop::sleep_for(duration);
    }

    inline auto sleep_until(std::chrono::steady_clock::time_point time_point)
    {
        return ito::loop::sleep_until(time_point);
    }
} // namespace ito::async
