#include "common.hpp"
#include "ito/coro.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/sequence.hpp>

#include <chrono>


TEST_CASE("loop sleep_* blocks until the deadline, then resumes")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    {
        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

        const auto start = std::chrono::steady_clock::now();
        loop.run_until_complete([&loop, &mock]() -> ito::coro<> {
            mock.call(1);
            co_await loop.sleep_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(5));
            mock.call(2);
            co_return;
        }());
        REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(5));
    }
}

TEST_CASE("loop sleep_* with a past deadline resolves without blocking")
{
    ito::loop  loop{};
    const auto start = std::chrono::steady_clock::now();

    loop.run_until_complete([&loop]() -> ito::coro<> {
        co_await loop.sleep_until(std::chrono::steady_clock::now() - std::chrono::hours(1));
        co_return;
    }());

    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(5));
}
