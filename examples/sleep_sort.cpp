// Sleep sort: every number becomes a task that sleeps for a time proportional to its value and then reports itself,
// so the numbers come out in order. A joke algorithm, but it shows tasks and timers interleaving on one thread.

#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

namespace
{
    ito::coro<> report_after_sleeping(int value, std::vector<int>& sorted)
    {
        co_await ito::async::sleep_for(std::chrono::milliseconds{value * 100});
        std::cout << value << ' ' << std::flush;
        sorted.push_back(value);
    }

    // `values` is taken by value on purpose: a coroutine's reference parameters aren't copied into its frame, so a
    // reference could outlive what it refers to once the coroutine suspends.
    // cppcheck-suppress passedByValue
    ito::coro<std::vector<int>> sleep_sort(std::vector<int> values)
    {
        std::vector<int>             sorted{};
        std::vector<ito::task<void>> tasks{};
        tasks.reserve(values.size());
        // cppcheck-suppress useStlAlgorithm
        for (const int value : values) tasks.push_back(ito::loop::current().create_task(report_after_sleeping(value, sorted)));

        // the tasks already run on their own; awaiting them, in any order, just waits until all of them are done
        for (auto& task : tasks) co_await std::move(task);
        co_return sorted;
    }
} // namespace

int main()
{
    try
    {
        ito::loop  loop{};
        const auto sorted = loop.run_until_complete(sleep_sort({5, 1, 4, 2, 8, 3, 0, 7, 6, 5, 5}));
        std::cout << '\n';
        return std::ranges::is_sorted(sorted) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << "failed: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
