#include <benchmark/benchmark.h>
#include <ito/async/future.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>

#include <numeric>
#include <vector>

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
        loop.call_soon([]() { });
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

static void bm_resolve_future_inside_signal(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(loop.run_until_complete([&loop]() -> ito::coro<int> {
            auto [promise, f] = ito::async::promise<int>::create();
            loop.call_soon([&]() { promise.set_result(10); });
            co_return co_await std::move(f);
        }()));
    }
}
BENCHMARK(bm_resolve_future_inside_signal);

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
