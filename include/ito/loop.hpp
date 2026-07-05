#pragma once

#include <ito/coro.hpp>

#include <utility>

namespace ito
{
    class loop
    {
    public:
        loop() = default;

        template<typename T>
        T run(ito::coro<T>&& coro)
        {
            return std::move(coro).run();
        }
    };
} // namespace ito
