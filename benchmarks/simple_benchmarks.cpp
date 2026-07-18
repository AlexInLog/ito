#include <ito/async/future.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>

#include <numeric>

TEST_CASE("no-op loop checks")
{
    BENCHMARK("creation of coro")
    {
        return []() -> ito::coro<> {
            co_return;
        }();
    }; // BENCHMARK("creation of coro")

    BENCHMARK_ADVANCED("call no-op coro")(Catch::Benchmark::Chronometer meter)
    {
        ito::loop loop{};
        meter.measure([&loop]() {
            return loop.run_until_complete([]() -> ito::coro<> {
                co_return;
            }());
        });
    };

    BENCHMARK_ADVANCED("call_soon before no-op coro")(Catch::Benchmark::Chronometer meter)
    {
        ito::loop loop{};
        meter.measure([&loop]() {
            loop.call_soon([]() { });
            return loop.run_until_complete([]() -> ito::coro<> {
                co_return;
            }());
        });
    };

    BENCHMARK_ADVANCED("call no-op coro as child")(Catch::Benchmark::Chronometer meter)
    {
        ito::loop loop{};
        meter.measure([&loop]() {
            return loop.run_until_complete([]() -> ito::coro<> {
                co_await []() -> ito::coro<> {
                    co_return;
                }();
                co_return;
            }());
        });
    };

    BENCHMARK_ADVANCED("call coro calling function")(Catch::Benchmark::Chronometer meter)
    {
        std::vector<int> vec(meter.runs(), 0);
        std::iota(vec.begin(), vec.end(), meter.runs());
        ito::loop loop{};
        meter.measure([&](int i) {
            auto lambda = [&vec, i]() {
                return vec[i];
            };
            return loop.run_until_complete([lambda]() -> ito::coro<int> {
                co_return lambda();
            }());
        });
    };
}

TEST_CASE("future")
{
    BENCHMARK_ADVANCED("resolve future before await")(Catch::Benchmark::Chronometer meter)
    {
        ito::loop loop{};
        meter.measure([&loop]() {
            return loop.run_until_complete([]() -> ito::coro<int> {
                ito::async::future<int> f{};
                f.set_result(10);
                co_return co_await f;
            }());
        });
    };

    BENCHMARK_ADVANCED("resolve future inside signal")(Catch::Benchmark::Chronometer meter)
    {
        ito::loop loop{};
        meter.measure([&loop]() {
            return loop.run_until_complete([&loop]() -> ito::coro<int> {
                ito::async::future<int> f{};
                loop.call_soon([&]() { f.set_result(10); });
                co_return co_await f;
            }());
        });
    };
}
