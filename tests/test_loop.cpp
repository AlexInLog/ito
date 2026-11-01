#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <coroutine>
#include <stdexcept>


TEST_CASE("loop bounds self to current")
{
    ito::loop loop{};
    REQUIRE_THROWS_AS(ito::loop::current(), ito::exceptions::invalid_loop_state);

    loop.run_until_complete([&]() -> ito::coro<> {
        REQUIRE(&loop == &ito::loop::current());
        co_return;
    }());

    REQUIRE_THROWS_AS(ito::loop::current(), ito::exceptions::invalid_loop_state);
}

TEST_CASE("can't run loop inside loop")
{
    ito::loop loop{};
    loop.run_until_complete([]() -> ito::coro<> {
        REQUIRE_THROWS_AS(
            ito::loop{}.run_until_complete([]() -> ito::coro<> {
                co_return;
            }()),
            ito::exceptions::invalid_loop_state
        );
        co_return;
    }());
}

TEST_CASE("can't run same loop inside loop")
{
    ito::loop loop{};
    loop.run_until_complete([&]() -> ito::coro<> {
        REQUIRE_THROWS_AS(
            loop.run_until_complete([]() -> ito::coro<> {
                co_return;
            }()),
            ito::exceptions::invalid_loop_state
        );
        co_return;
    }());
}

TEST_CASE("call_soon relatively to run_until_complete")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    SECTION("no-op coro")
    {
        loop.call_soon([&]() { mock.call(1); });

        {
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

            loop.run_until_complete([&]() -> ito::coro<> {
                loop.call_soon([&]() { mock.call(2); });
                co_return;
            }());
        }

        loop.call_soon([&]() { mock.call(3); });

        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);
        loop.run_until_complete([&]() -> ito::coro<> {
            co_return;
        }());
    }

    SECTION("suspending coro")
    {
        loop.call_soon([&]() { mock.call(1); });

        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<> {
                loop.call_soon([&]() { mock.call(2); });
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

                co_await std::suspend_always{};
                co_return;
            }()),
            ito::exceptions::empty_value
        );

        loop.call_soon([&]() { mock.call(3); });
    }
}

TEST_CASE("run_until_complete that throws before its coroutine starts leaves no stale resume of it behind")
{
    ito::loop loop{};
    call_mock mock{};

    // the queue isn't empty, so run_until_complete queues its coroutine's first resume behind this callback;
    // the callback throws first, and the exception destroys that coroutine before its queued resume ever runs
    loop.call_soon([]() { throw std::runtime_error{"callback failed"}; });
    REQUIRE_THROWS_AS(
        loop.run_until_complete([&]() -> ito::coro<> {
            mock.call(1); // no expectation set: this coroutine must never run
            co_return;
        }()),
        std::runtime_error
    );

    // the loop is reused: its next run must skip the destroyed coroutine's queued resume rather than resume freed memory
    REQUIRE(loop.run_until_complete([]() -> ito::coro<int> {
        co_return 42;
    }()) == 42);
}
