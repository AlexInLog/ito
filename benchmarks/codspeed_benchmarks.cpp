#include <benchmark/benchmark.h>
#include <ito/async/future.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>

#include <functional>
#include <memory>

// ── Baseline references ──────────────────────────────────────────────────────

static void BM_unique_ptr(benchmark::State& state)
{
    for (auto _ : state)
    {
        auto ptr = std::make_unique<int>(0);
        benchmark::DoNotOptimize(ptr);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_unique_ptr);

static void BM_small_std_function(benchmark::State& state)
{
    for (auto _ : state)
    {
        int  v{};
        auto fn = std::function{[v](double c) { return c + v; }};
        benchmark::DoNotOptimize(fn);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_small_std_function);

static void BM_big_std_function(benchmark::State& state)
{
    for (auto _ : state)
    {
        std::array<int, 100> arr{};
        auto                 fn = std::function{[arr](double c) { return c + arr[0]; }};
        benchmark::DoNotOptimize(fn);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_big_std_function);

// ── Loop / coro ──────────────────────────────────────────────────────────────

static void BM_creation_of_coro(benchmark::State& state)
{
    for (auto _ : state)
    {
        auto coro = []() -> ito::coro<> {
            co_return;
        }();
        benchmark::DoNotOptimize(coro);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_creation_of_coro);

static void BM_call_no_op_coro(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_call_no_op_coro);

static void BM_call_soon_before_no_op_coro(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        loop.call_soon([]() { });
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_call_soon_before_no_op_coro);

static void BM_call_no_op_coro_as_child(benchmark::State& state)
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
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_call_no_op_coro_as_child);

static void BM_call_coro_calling_function(benchmark::State& state)
{
    ito::loop loop{};
    int       i = 0;
    for (auto _ : state)
    {
        auto lambda = [i]() {
            return i;
        };
        auto result = loop.run_until_complete([lambda]() -> ito::coro<int> {
            co_return lambda();
        }());
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
        ++i;
    }
}
BENCHMARK(BM_call_coro_calling_function);

// ── Future ───────────────────────────────────────────────────────────────────

static void BM_resolve_future_before_await(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        auto result = loop.run_until_complete([]() -> ito::coro<int> {
            ito::async::future<int> f{};
            f.set_result(10);
            co_return co_await f;
        }());
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_resolve_future_before_await);

static void BM_resolve_future_inside_signal(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        auto result = loop.run_until_complete([&loop]() -> ito::coro<int> {
            ito::async::future<int> f{};
            loop.call_soon([&]() { f.set_result(10); });
            co_return co_await f;
        }());
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_resolve_future_inside_signal);

// ── Task ─────────────────────────────────────────────────────────────────────

static void BM_start_task_inside_coro_and_await(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        auto result = loop.run_until_complete([&loop]() -> ito::coro<int> {
            auto task = loop.create_task([]() -> ito::coro<int> {
                co_return 2;
            }());
            co_return co_await std::move(task);
        }());
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_start_task_inside_coro_and_await);

static void BM_start_2_tasks_inside_coro_and_await_second(benchmark::State& state)
{
    ito::loop loop{};
    for (auto _ : state)
    {
        auto result = loop.run_until_complete([&loop]() -> ito::coro<int> {
            auto task   = loop.create_task([]() -> ito::coro<int> {
                co_return 2;
            }());
            auto task_2 = loop.create_task([]() -> ito::coro<int> {
                co_return 3;
            }());

            co_return co_await std::move(task_2);
        }());
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_start_2_tasks_inside_coro_and_await_second);

BENCHMARK_MAIN();
