#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <chrono>

TEST_CASE("sleep_* blocks until the deadline, then resumes")
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
                co_await awaitable();
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
            check([&]() { return ito::async::sleep_until(std::chrono::steady_clock::now() + duration); });
        }

        SECTION("sleep_for")
        {
            check([&]() { return ito::async::sleep_for(duration); });
        }

        SECTION("loop.sleep_until")
        {
            check([&]() { return loop.sleep_until(std::chrono::steady_clock::now() + duration); });
        }

        SECTION("loop.sleep_for")
        {
            check([&]() { return loop.sleep_for(duration); });
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

TEST_CASE("sleep while another active tasks is active")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    auto create_coro = [&]() -> ito::coro<void> {
        auto coro_1 = [&]() -> ito::coro<void> {
            mock.call(2);
            for (size_t i = 0; i < 3; ++i)
            {
                co_await ito::async::sleep_for(std::chrono::milliseconds(1));
                mock.call(20);
            }
            mock.call(-2);
        };
        auto coro_2 = [&]() -> ito::coro<void> {
            mock.call(3);
            co_await ito::async::sleep_for(std::chrono::milliseconds(2));
            mock.call(-3);
        };

        auto task_1 = ito::loop::current().create_task(coro_1());
        auto task_2 = ito::loop::current().create_task(coro_2());

        {
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
            mock.call(1);
        }
        {
            REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(20)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(-3)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(20)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(20)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(-2)).IN_SEQUENCE(s);

            co_await ito::async::sleep_for(std::chrono::milliseconds(5));
        }

        REQUIRE_CALL(mock, call(-1)).IN_SEQUENCE(s);
        mock.call(-1);

        co_return;
    };
    loop.run_until_complete(create_coro());
}
