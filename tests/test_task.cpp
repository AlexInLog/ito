#include "common.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <ito/async/future.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <optional>

TEST_CASE("base task logic")
{
    ito::loop             l{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    auto inner_coro = [&mock](int index = 0) -> ito::coro<int> {
        mock.call(index);
        co_return index + 1;
    };

    SECTION("create task inside coro and await")
    {
        const auto res = l.run_until_complete([&]() -> ito::coro<int> {
            auto inner = inner_coro(0);

            auto task = l.create_task(std::move(inner));

            int temp = -1;
            {
                REQUIRE_CALL(mock, call(0));
                temp = (co_await std::move(task));
            }
            co_return temp + 1;
        }());

        REQUIRE(res == 2);
    }
    SECTION("create two tasks inside coro and await second")
    {
        const auto res = l.run_until_complete([&]() -> ito::coro<int> {
            auto inner_1 = inner_coro(0);
            auto inner_2 = inner_coro(2);

            auto task_1 = l.create_task(std::move(inner_1));
            auto task_2 = l.create_task(std::move(inner_2));

            int temp = -1;
            {
                REQUIRE_CALL(mock, call(0)).IN_SEQUENCE(s);
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
                temp = (co_await std::move(task_2));
            }
            co_return temp + 1;
        }());

        REQUIRE(res == 4);
    }
    SECTION("create two tasks inside coro and await second while first destroyed before await")
    {
        const auto res = l.run_until_complete([&]() -> ito::coro<int> {
            auto inner_1 = inner_coro(0);
            auto inner_2 = inner_coro(2);

            {
                [[maybe_unused]] auto task_1 = l.create_task(std::move(inner_1));
            }
            auto task_2 = l.create_task(std::move(inner_2));

            int temp = -1;
            {
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
                temp = (co_await std::move(task_2));
            }
            co_return temp + 1;
        }());

        REQUIRE(res == 4);
    }
    SECTION("create three tasks inside coro and await third while second destroyed before inside first")
    {
        const auto res = l.run_until_complete([&]() -> ito::coro<int> {
            std::optional<ito::task<int>> task_2{};

            auto coro_make = [&](std::optional<ito::task<int>>* task_2) -> ito::coro<void> {
                task_2->reset();
                mock.call(-1);
                co_return;
            };
            auto inner_1 = coro_make(&task_2);

            auto inner_2 = inner_coro(1);
            auto inner_3 = inner_coro(2);

            auto task_1 = l.create_task(std::move(inner_1));
            task_2.emplace(l.create_task(std::move(inner_2)));
            auto task_3 = l.create_task(std::move(inner_3));

            int temp = -1;
            {
                REQUIRE_CALL(mock, call(-1)).IN_SEQUENCE(s);
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
                temp = (co_await std::move(task_3));
            }
            co_return temp + 1;
        }());

        REQUIRE(res == 4);
    }
    SECTION("destroy task while it's suspended awaiting a future, then resolve the future")
    {
        ito::async::future<int> f{};

        auto inner = [&]() -> ito::coro<int> {
            mock.call(-1);
            const int v = co_await f;
            mock.call(v);
            co_return v + 1;
        };

        std::optional<ito::task<int>> task{};

        l.run_until_complete([&]() -> ito::coro<void> {
            task.emplace(l.create_task(inner()));

            // hop through the loop once so `task` actually starts and suspends on `co_await f`,
            // rather than being cancelled before it ever ran
            ito::async::future<> tick{};
            l.call_soon([&]() { tick.set_result(); });
            {
                REQUIRE_CALL(mock, call(-1));
                co_await tick;
            }

            // `task`'s coroutine is currently suspended inside `co_await f`; destroying it here
            // must unregister it from `f` rather than leaving `f` pointing at freed coroutine state
            task.reset();
            co_return;
        }());

        // if `f` still held the destroyed coroutine's handle as its continuation, this would
        // resume freed memory instead of being a harmless no-op (and `mock.call` would never
        // fire, since nothing set up a REQUIRE_CALL for it)
        REQUIRE_NOTHROW(f.set_result(42));
    }
}
