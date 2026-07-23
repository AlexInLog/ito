#include <catch2/catch_template_test_macros.hpp>

#include <ito/coro.hpp>
#include <ito/loop.hpp>

TEST_CASE("create task")
{
    ito::loop l{};

    SECTION("create task inside coro")
    {
        const auto res = l.run_until_complete([&l]() -> ito::coro<int> {
            auto inner = []() -> ito::coro<int> {
                co_return 3;
            }();

            auto task = l.create_task(std::move(inner));
            co_return (co_await std::move(task)) + 1;
        }());

        REQUIRE(res == 4);
    }
}
