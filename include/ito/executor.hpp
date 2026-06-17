#pragma once

#include <ito/task.hpp>

namespace ito
{
    class Executor
    {
    public:
        Executor() = default;

        void run(ito::Task& task) {
            task.run();
        }

    private:
    };
} // namespace ito
