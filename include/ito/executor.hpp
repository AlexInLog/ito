#pragma once

#include <ito/coro.hpp>

#include <utility>

namespace ito
{
    class executor
    {
    public:
        executor() = default;

        template<typename T>
        T run(ito::coro<T>&& coro)
        {
            return std::move(coro).run();
        }
    };
} // namespace ito
