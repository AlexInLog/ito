#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <coroutine>
#include <functional>
#include <utility>


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
        loop.call_soon([&]() noexcept { mock.call(1); });

        {
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

            loop.run_until_complete([&]() -> ito::coro<> {
                loop.call_soon([&]() noexcept { mock.call(2); });
                co_return;
            }());
        }

        loop.call_soon([&]() noexcept { mock.call(3); });

        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);
        loop.run_until_complete([&]() -> ito::coro<> {
            co_return;
        }());
    }

    SECTION("suspending coro")
    {
        loop.call_soon([&]() noexcept { mock.call(1); });

        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<> {
                loop.call_soon([&]() noexcept { mock.call(2); });
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

                co_await std::suspend_always{};
                co_return;
            }()),
            ito::exceptions::empty_value
        );

        loop.call_soon([&]() noexcept { mock.call(3); });
    }
}

template<typename Fn>
concept call_soon_accepts = requires(ito::loop& loop, Fn fn) { loop.call_soon(std::move(fn)); };

TEST_CASE("call_soon only accepts callbacks that can't throw")
{
    // a callback has nobody awaiting it, so a throwing one is rejected at compile time rather than unwinding
    // whichever run_until_complete() happens to be running
    STATIC_REQUIRE(call_soon_accepts<decltype([]() noexcept {})>);
    STATIC_REQUIRE_FALSE(call_soon_accepts<decltype([]() {})>);
    STATIC_REQUIRE_FALSE(call_soon_accepts<std::function<void()>>);

    // coroutine handles are queued as-is: resuming one never throws, since its promise captures the body's exceptions
    STATIC_REQUIRE(call_soon_accepts<std::coroutine_handle<>>);
}
