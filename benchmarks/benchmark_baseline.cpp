#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>

TEST_CASE("baseline")
{
    BENCHMARK("unuqie_ptr")
    {
        return std::make_unique<int>(0);
    }; // BENCHMARK("unuqie_ptr")

    BENCHMARK("small std::function")
    {
        int v{};
        return std::function{[v](double c) { return c + v; }};
    }; // BENCHMARK("small std::function")

    BENCHMARK("big std::function")
    {
        std::array<int, 100> arr{};
        return std::function{[arr](double c) { return c + arr[0]; }};
    }; // BENCHMARK("big std::function")
}
