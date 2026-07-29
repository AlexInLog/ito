#include <benchmark/benchmark.h>

#include <functional>

static void bm_baseline_unique_ptr(benchmark::State& state)
{
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(std::make_unique<int>(0));
    }
}
BENCHMARK(bm_baseline_unique_ptr);

static void bm_small_std_function(benchmark::State& state)
{
    int v{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(std::function{[v](double c) { return c + v; }});
    }
}
BENCHMARK(bm_small_std_function);

static void bm_big_std_function(benchmark::State& state)
{
    std::array<int, 100> arr{};
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(std::function{[arr](double c) { return c + arr[0]; }});
    }
}
BENCHMARK(bm_big_std_function);
