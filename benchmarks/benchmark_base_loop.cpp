#include "ito/coro.hpp"
#include "ito/loop.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("no-op loop checks")
{
    BENCHMARK("call no-op coro")
    {
        return ito::loop{}.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
    }; // BENCHMARK("call no-op coro")
    BENCHMARK("call no-op coro as child")
    {
        return ito::loop{}.run_until_complete([]() -> ito::coro<> {
            co_await []() -> ito::coro<> {
                co_return;
            }();
            co_return;
        }());
    }; // BENCHMARK("call no-op coro as child")
}
