#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/lifetime.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <coroutine>
#include <memory>
#include <stdexcept>

TEST_CASE("Validate base coroutine checks")
{
    auto owner = std::make_unique<trompeloeil::deathwatched<call_mock>>();
    auto mock  = owner.get(); // use raw unowned pointer

    {
        REQUIRE_DESTRUCTION(*mock);

        auto make_coro = [&, c = std::move(owner)](auto...) -> ito::coro<int> {
            c->call(0);
            co_return 42;
        };

        SECTION("create and destruct coro")
        {
            auto inner      = std::make_unique<trompeloeil::deathwatched<call_mock>>();
            auto inner_mock = inner.get(); // use raw unowned pointer

            {
                REQUIRE_DESTRUCTION(*inner_mock);
                {
                    auto coro = make_coro(std::move(inner));
                }
            }
        }

        SECTION("create and run task")
        {
            auto inner      = std::make_unique<trompeloeil::deathwatched<call_mock>>();
            auto inner_mock = inner.get(); // use raw unowned pointer

            auto coro = make_coro(std::move(inner));
            REQUIRE_CALL(*mock, call(0));

            {
                REQUIRE_DESTRUCTION(*inner_mock);
                const auto res = ito::loop{}.run_until_complete(std::move(coro));
                REQUIRE(res == 42);
            }

            SECTION("execute same coro twice")
            {
                REQUIRE_THROWS_AS(
                    ito::loop{}.run_until_complete(std::move(coro)), // NOLINT (bugprone-use-after-move)
                    ito::exceptions::invalid_coro_handle_state
                );
            }
        }

        SECTION("run task from parent")
        {
            auto       coro       = make_coro();
            const auto child_coro = [&]() -> ito::coro<int> {
                co_return co_await std::move(coro) + 2;
            };

            REQUIRE_CALL(*mock, call(0));
            const auto res = ito::loop{}.run_until_complete(child_coro());
            REQUIRE(res == 44);

            SECTION("execute same original coro twice via executing new child")
            {
                REQUIRE_THROWS_AS(ito::loop{}.run_until_complete(child_coro()), ito::exceptions::invalid_coro_handle_state);
            }
        }
        SECTION("run task from parent of another type")
        {
            auto       coro       = make_coro();
            const auto child_coro = [&]() -> ito::coro<std::string> {
                co_return std::to_string(co_await std::move(coro));
            };

            REQUIRE_CALL(*mock, call(0));
            const auto res = ito::loop{}.run_until_complete(child_coro());
            REQUIRE(res == "42");
        }
        SECTION("run task from parent of void")
        {
            auto       coro = make_coro();
            int        child_res{};
            const auto child_coro = [&]() -> ito::coro<void> {
                child_res = co_await std::move(coro);
                co_return;
            };

            REQUIRE_CALL(*mock, call(0));
            ito::loop{}.run_until_complete(child_coro());
            REQUIRE(child_res == 42);
        }
    }
}

TEST_CASE("return move_only value")
{
    auto owner = std::make_unique<trompeloeil::deathwatched<call_mock>>();
    {
        auto mock      = owner.get();
        auto make_task = [&]() -> ito::coro<std::unique_ptr<trompeloeil::deathwatched<call_mock>>> {
            co_return std::move(owner);
        };

        auto task = make_task();
        auto r    = ito::loop{}.run_until_complete(std::move(task));
        REQUIRE(mock == r.get());
        REQUIRE_DESTRUCTION(*mock);
        r.reset();
    }
}

TEST_CASE("return-copy object")
{
    copy_count_tracker t{};

    auto make_task = [&]() -> ito::coro<copy_count_tracker> {
        co_return t;
    };

    trompeloeil::sequence s{};
    REQUIRE_CALL(t.impl(), copy_constructor()).TIMES(1).IN_SEQUENCE(s);
    REQUIRE_CALL(t.impl(), move_constructor()).TIMES(1).IN_SEQUENCE(s);

    [[maybe_unused]] auto _ = ito::loop{}.run_until_complete(make_task());
}

TEST_CASE("return-move object")
{
    copy_count_tracker t{};

    auto make_task = [&]() -> ito::coro<copy_count_tracker> {
        co_return std::move(t);
    };

    trompeloeil::sequence s{};
    REQUIRE_CALL(t.impl(), move_constructor()).TIMES(2).IN_SEQUENCE(s);

    [[maybe_unused]] auto _ = ito::loop{}.run_until_complete(make_task());
}

TEST_CASE("propogate exception")
{
    struct custom_error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };
    auto make_task = []() -> ito::coro<> {
        throw custom_error{"my error"};
        co_return;
    };

    auto task = make_task();

    REQUIRE_THROWS_AS(ito::loop{}.run_until_complete(std::move(task)), custom_error);
}

TEST_CASE("coro co_await suspend_always")
{
    auto make_task = []() -> ito::coro<> {
        co_await std::suspend_always{};
        co_return;
    };

    auto task = make_task();
    REQUIRE_THROWS_AS(ito::loop{}.run_until_complete(std::move(task)), ito::exceptions::empty_value);
}

TEST_CASE("nested co_await releases child coro exactly once")
{
    auto owner = std::make_unique<trompeloeil::deathwatched<call_mock>>();
    auto mock  = owner.get();

    auto make_child = [&]() -> ito::coro<std::unique_ptr<trompeloeil::deathwatched<call_mock>>> {
        co_return std::move(owner);
    };

    auto child = make_child();

    auto parent = [&]() -> ito::coro<std::unique_ptr<trompeloeil::deathwatched<call_mock>>> {
        co_return co_await std::move(child);
    };

    auto result = ito::loop{}.run_until_complete(parent());
    REQUIRE(mock == result.get());

    // Ресурс дошёл до нас живым: фрейм ребёнка не утащил его с собой при
    // разрушении (это была бы утечка), и до этого момента он не был
    // уничтожен преждевременно (это было бы use-after-free выше).
    REQUIRE_DESTRUCTION(*mock);
    result.reset();

    // child уже "выпит" первым co_await. Раньше это было UB (resume
    // коллапс на final_suspend), теперь должно быть контролируемое
    // исключение -- и, что важно, без повторного вызова .destroy()
    // на уже уничтоженном фрейме (иначе тут будет double-free).
    auto parent2 = [&]() -> ito::coro<std::unique_ptr<trompeloeil::deathwatched<call_mock>>> {
        co_return co_await std::move(child);
    };
    REQUIRE_THROWS_AS(ito::loop{}.run_until_complete(parent2()), ito::exceptions::invalid_coro_handle_state);
}


TEST_CASE("nested co_await frees the child coroutine frame (no leak)")
{
    auto make_child = [](auto...) -> ito::coro<int> {
        co_return 42;
    };

    auto parent = [&]() -> ito::coro<int> {
        auto owner = std::make_unique<trompeloeil::deathwatched<call_mock>>();
        auto mock  = owner.get();
        auto child = make_child(std::move(owner));
        int  res{};
        {
            REQUIRE_DESTRUCTION(*mock);
            res = co_await std::move(child); // временный child, снаружи им никто не владеет
        }
        co_return res;
    };

    const auto res = ito::loop{}.run_until_complete(parent());
    REQUIRE(res == 42);
}

TEST_CASE("nested co_await frees the child coroutine frame (no leak) even if suspended")
{
    auto make_child = [](auto...) -> ito::coro<int> {
        co_await std::suspend_always{};
        co_return 42;
    };

    auto parent = [&]() -> ito::coro<int> {
        auto owner = std::make_unique<trompeloeil::deathwatched<call_mock>>();
        auto mock  = owner.get();
        auto child = make_child(std::move(owner));
        int  res{};
        {
            REQUIRE_DESTRUCTION(*mock);
            res = co_await std::move(child); // временный child, снаружи им никто не владеет
        }
        co_return res;
    };

    REQUIRE_THROWS_AS(ito::loop{}.run_until_complete(parent()), ito::exceptions::empty_value);
}
