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

    const auto check_for_duration = [&](const std::chrono::milliseconds duration) {
        auto check = [&](auto awaitable) {
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

            const auto start = std::chrono::steady_clock::now();
            loop.run_until_complete([&mock, &awaitable]() -> ito::coro<> {
                mock.call(1);
                co_await std::move(awaitable);
                mock.call(2);
                co_return;
            }());
            if (duration > duration.zero())
                REQUIRE(std::chrono::steady_clock::now() - start >= duration);
            else
                REQUIRE(std::chrono::steady_clock::now() - start < -duration);
        };

        SECTION("sleep_until")
        {
            check(loop.sleep_until(std::chrono::steady_clock::now() + duration));
        }

        SECTION("sleep_for")
        {
            check(loop.sleep_for(duration));
        }
    };

    SECTION("positive duration")
    {
        check_for_duration(std::chrono::milliseconds(5));
    }

    SECTION("negative duration doesn't block")
    {
        check_for_duration(-std::chrono::milliseconds(5));
    }
}
