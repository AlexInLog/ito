#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/mock.hpp>

#include <coroutine>

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

class call_mock
{
    MAKE_MOCK(call, auto(size_t)->void);
};

TEST_CASE("call_soon relatively to run_until_complete")
{
    ito::loop loop{};
    call_mock mock{};

    SECTION("no-op coro")
    {
        loop.call_soon([&]() { mock.call(1); });

        REQUIRE_CALL(mock, call(1));

        loop.run_until_complete([&]() -> ito::coro<> {
            loop.call_soon([&]() { mock.call(2); });
            co_return;
        }());

        loop.call_soon([&]() { mock.call(3); });
    }

    SECTION("suspending coro")
    {
        loop.call_soon([&]() { mock.call(1); });

        REQUIRE_CALL(mock, call(1));

        REQUIRE_THROWS_AS(loop.run_until_complete([&]() -> ito::coro<> {
            loop.call_soon([&]() { mock.call(2); });
            REQUIRE_CALL(mock, call(2));

            co_await std::suspend_always{};
            co_return;
        }()), ito::exceptions::empty_value);

        loop.call_soon([&]() { mock.call(3); });
    }
}
