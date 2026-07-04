#pragma once

#include <ito/task.hpp>

namespace ito
{
    class executor
    {
    public:
        executor() = default;

        void run(ito::task& task) {
            task.run();
        }

    private:
    };
} // namespace ito
