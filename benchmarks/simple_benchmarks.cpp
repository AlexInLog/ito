#include <benchmark/benchmark.h>
#include <ito/async/future.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <vector>

namespace
{
    ito::coro<int> return_value(int value)
    {
        co_return value;
    }

    ito::coro<int> nested_chain(int depth)
    {
        if (depth == 0)
            co_return 0;
        co_return 1 + co_await nested_chain(depth - 1);
    }

    // Where each side of the ping-pong leaves the promise that wakes it.
    struct mailbox
    {
        std::optional<ito::async::promise<int>> wake{};
    };

    // void wake(mailbox& box, int value)
    // {
    //     if (box.wake)
    //     {
    //         auto promise = std::move(*box.wake);
    //         box.wake.reset();
    //         promise.set_result(value);
    //     }
    // }

    // Each hop wakes the other side and suspends until it's woken back: one queued resume per hop, no callbacks.
    // ito::coro<> ping_pong_side(mailbox& mine, mailbox& other, std::int64_t hops)
    // {
    //     for (std::int64_t i = 0; i < hops; ++i)
    //     {
    //         auto [promise, future] = ito::async::promise<int>::create();
    //         mine.wake.emplace(std::move(promise));
    //         wake(other, 0);
    //         co_await std::move(future);
    //     }
    //     // the other side is still waiting for this side's last hop
    //     wake(other, 0);
    // }
} // namespace

static void bm_creation_of_coro(benchmark::State& state)
{
    for (auto _ : state)
    {
        benchmark::DoNotOptimize([]() -> ito::coro<> {
            co_return;
        }());
    }
}
BENCHMARK(bm_creation_of_coro);

static void bm_call_noop_coro(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
    }
}
BENCHMARK(bm_call_noop_coro);

static void bm_call_soon_before_noop_coro(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        loop.call_soon([]() noexcept { });
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
    }
}
BENCHMARK(bm_call_soon_before_noop_coro);

static void bm_call_noop_coro_as_child(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        loop.run_until_complete([]() -> ito::coro<> {
            co_await []() -> ito::coro<> {
                co_return;
            }();
            co_return;
        }());
    }
}
BENCHMARK(bm_call_noop_coro_as_child);

static void bm_call_coro_calling_function(benchmark::State& state)
{
    ito::loop        loop{};
    std::vector<int> vec(static_cast<std::size_t>(state.max_iterations));
    std::iota(vec.begin(), vec.end(), 0);

    std::size_t i = 0;
    for (auto _ : state)
    {
        auto lambda = [&vec, i]() {
            return vec[i];
        };
        benchmark::DoNotOptimize(loop.run_until_complete([lambda]() -> ito::coro<int> {
            co_return lambda();
        }()));
        ++i;
    }
}
BENCHMARK(bm_call_coro_calling_function);

static void bm_resolve_future_before_await(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(loop.run_until_complete([]() -> ito::coro<int> {
            auto [promise, f] = ito::async::promise<int>::create();
            promise.set_result(10);
            co_return co_await std::move(f);
        }()));
    }
}
BENCHMARK(bm_resolve_future_before_await);

// static void bm_resolve_future_inside_signal(benchmark::State& state)
// {
//     ito::loop loop{};
//     for (auto _ : state)
//     {
//         benchmark::DoNotOptimize(loop.run_until_complete([&loop]() -> ito::coro<int> {
//             auto [promise, f] = ito::async::promise<int>::create();
//             loop.call_soon([&]() noexcept { promise.set_result(10); });
//             co_return co_await std::move(f);
//         }()));
//     }
// }
// BENCHMARK(bm_resolve_future_inside_signal);

static void bm_start_task_inside_coro_and_await(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(loop.run_until_complete([&loop]() -> ito::coro<int> {
            auto task = loop.create_task([]() -> ito::coro<int> {
                co_return 2;
            }());
            co_return co_await std::move(task);
        }()));
    }
}
BENCHMARK(bm_start_task_inside_coro_and_await);

static void bm_start_two_tasks_inside_coro_and_await_second(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(loop.run_until_complete([&loop]() -> ito::coro<int> {
            auto task   = loop.create_task([]() -> ito::coro<int> {
                co_return 2;
            }());
            auto task_2 = loop.create_task([]() -> ito::coro<int> {
                co_return 3;
            }());

            co_return co_await std::move(task_2);
        }()));
    }
}
BENCHMARK(bm_start_two_tasks_inside_coro_and_await_second);

// Many tasks queued at once, then awaited: a deep ready queue.
static void bm_fan_out_tasks(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(loop.run_until_complete([](ito::loop& loop, std::size_t count) -> ito::coro<int> {
            std::vector<ito::task<int>> tasks{};
            tasks.reserve(count);
            for (std::size_t i = 0; i < count; ++i) tasks.push_back(loop.create_task(return_value(1)));

            int sum = 0;
            for (auto& task : tasks) sum += co_await std::move(task);
            co_return sum;
        }(loop, static_cast<std::size_t>(state.range(0)))));
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(bm_fan_out_tasks)->Arg(16)->Arg(1024);

// Two tasks handing control back and forth through futures, inside one run: the cost of one reschedule.
// static void bm_ping_pong_tasks(benchmark::State& state)
// {
//     ito::loop loop{};
//     for (auto _ : state)
//     {
//         loop.run_until_complete([](ito::loop& loop, std::int64_t hops) -> ito::coro<> {
//             mailbox ping{};
//             mailbox pong{};
//             auto    first  = loop.create_task(ping_pong_side(ping, pong, hops));
//             auto    second = loop.create_task(ping_pong_side(pong, ping, hops));
//             co_await std::move(first);
//             co_await std::move(second);
//         }(loop, state.range(0)));
//     }
//     state.SetItemsProcessed(state.iterations() * state.range(0) * 2);
// }
// BENCHMARK(bm_ping_pong_tasks)->Arg(16)->Arg(1024);

// Tasks destroyed before they start: their queued starts are dead entries the loop still has to get past to reach
// the task queued after them.
static void bm_cancel_tasks_before_start(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(loop.run_until_complete([](ito::loop& loop, std::size_t count) -> ito::coro<int> {
            {
                std::vector<ito::task<int>> tasks{};
                tasks.reserve(count);
                for (std::size_t i = 0; i < count; ++i) tasks.push_back(loop.create_task(return_value(1)));
            }
            co_return co_await loop.create_task(return_value(1));
        }(loop, static_cast<std::size_t>(state.range(0)))));
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(bm_cancel_tasks_before_start)->Arg(16)->Arg(1024);

// Many callbacks queued in one tick, drained before the coroutine queued behind them.
static void bm_call_soon_burst(benchmark::State& state)
{
    ito::loop    loop{};
    const auto   count   = state.range(0);
    std::int64_t counter = 0;
    for (auto _ : state)
    {
        for (std::int64_t i = 0; i < count; ++i) loop.call_soon([&counter]() noexcept { ++counter; });
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
    }
    benchmark::DoNotOptimize(counter);
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(bm_call_soon_burst)->Arg(16)->Arg(1024);

// A chain of coroutines each awaiting the next: frame allocation and symmetric transfer, no queue involved.
static void bm_nested_coro_chain(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state) benchmark::DoNotOptimize(loop.run_until_complete(nested_chain(static_cast<int>(state.range(0)))));
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(bm_nested_coro_chain)->Arg(16);
