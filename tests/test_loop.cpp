#include <catch2/catch_test_macros.hpp>

#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>

TEST_CASE("can't run loop inside loop")
{
    ito::loop loop{};
    loop.run([]() -> ito::coro<> {
        REQUIRE_THROWS_AS(
            ito::loop{}.run([]() -> ito::coro<> {
                co_return;
            }()),
            ito::exceptions::invalid_loop_state
        );
        co_return;
    }());
}
