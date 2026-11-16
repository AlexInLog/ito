#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ito/async/future.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <chrono>
#include <optional>
#include <vector>

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
                co_await ito::async::sleep_for(std::chrono::milliseconds(2));
                mock.call(20);
            }
            mock.call(-2);
        };
        auto coro_2 = [&]() -> ito::coro<void> {
            mock.call(3);
            co_await ito::async::sleep_for(std::chrono::milliseconds(3));
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
            const auto last = NAMED_REQUIRE_CALL(mock, call(-2)).IN_SEQUENCE(s);

            while (!last->is_satisfied())
            {
                co_await ito::async::sleep_for(std::chrono::milliseconds(10));
            }
        }

        REQUIRE_CALL(mock, call(-1)).IN_SEQUENCE(s);
        mock.call(-1);

        co_return;
    };
    loop.run_until_complete(create_coro());
}

// A coroutine's `continuation` is resumed inline from its final_suspend, never through the loop's queue. That's only
// safe because whoever is stored as the continuation also owns the awaited frame, so it can't be destroyed while
// that frame still exists. These cases destroy the awaiter while the awaited frame is suspended to pin that down.
TEST_CASE("destroying an awaiter destroys the frame it awaits")
{
    ito::loop l{};
    call_mock mock{};

    auto sleeping_child = [&]() -> ito::coro<void> {
        mock.call(2);
        co_await ito::async::sleep_for(std::chrono::milliseconds(5));
        mock.call(-2);
    };

    const auto cancel_after_start = [&](ito::coro<void> awaiter) {
        l.run_until_complete([&]() -> ito::coro<> {
            std::optional<ito::task<void>> task{};
            task.emplace(l.create_task(std::move(awaiter)));
            {
                REQUIRE_CALL(mock, call(1));
                REQUIRE_CALL(mock, call(2));
                co_await ito::async::sleep_for(std::chrono::milliseconds(1));
            }

            // the awaiter is suspended in `co_await` on the child, which is suspended in its sleep: destroying the
            // awaiter must destroy the child too, so neither `mock.call(-2)` nor `mock.call(-1)` ever fires
            task.reset();
            co_await ito::async::sleep_for(std::chrono::milliseconds(10));
        }());
    };

    SECTION("awaiting a coro")
    {
        cancel_after_start([&]() -> ito::coro<void> {
            mock.call(1);
            co_await sleeping_child();
            mock.call(-1);
        }());
    }
    SECTION("awaiting a task")
    {
        cancel_after_start([&]() -> ito::coro<void> {
            mock.call(1);
            auto child = l.create_task(sleeping_child());
            co_await std::move(child);
            mock.call(-1);
        }());
    }
}

TEST_CASE("destroying a task while it sleeps cancels it")
{
    ito::loop loop{};
    call_mock mock{};

    auto sleeper = [&]() -> ito::coro<void> {
        mock.call(1);
        co_await ito::async::sleep_for(std::chrono::milliseconds(5));
        mock.call(-1);
    };

    loop.run_until_complete([&]() -> ito::coro<> {
        std::optional<ito::task<void>> task{};
        task.emplace(ito::loop::current().create_task(sleeper()));

        {
            REQUIRE_CALL(mock, call(1));
            co_await ito::async::sleep_for(std::chrono::milliseconds(1));
        }

        // `task` is suspended inside its sleep; destroying it must drop its timer entry's resume,
        // so `mock.call(-1)` never fires once that deadline passes
        task.reset();
        co_await ito::async::sleep_for(std::chrono::milliseconds(10));
        co_return;
    }());
}

TEST_CASE("a cancelled sleep doesn't keep an otherwise idle loop sleeping")
{
    ito::loop loop{};

    auto sleeper = []() -> ito::coro<void> {
        co_await ito::async::sleep_for(std::chrono::seconds(5));
    };
    auto [promise, f] = ito::async::promise<int>::create();

    const auto start = std::chrono::steady_clock::now();
    REQUIRE_THROWS_AS(
        loop.run_until_complete([&]() -> ito::coro<int> {
            {
                auto task = ito::loop::current().create_task(sleeper());
                co_await ito::async::sleep_for(std::chrono::milliseconds(1));
            }

            // nothing can ever resolve `f`, and the only pending timer belongs to the task cancelled above,
            // so the loop must give up right away instead of sleeping until that dead deadline
            co_return co_await std::move(f);
        }()),
        ito::exceptions::empty_value
    );
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
}

TEST_CASE("sleeps with the same deadline resume in the order they started")
{
    ito::loop        loop{};
    std::vector<int> order{};
    const auto       deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);

    auto sleeper = [](std::chrono::steady_clock::time_point deadline, int id, std::vector<int>* order) -> ito::coro<> {
        co_await ito::async::sleep_until(deadline);
        order->push_back(id);
    };

    loop.run_until_complete([&]() -> ito::coro<> {
        auto first  = loop.create_task(sleeper(deadline, 1, &order));
        auto second = loop.create_task(sleeper(deadline, 2, &order));
        auto third  = loop.create_task(sleeper(deadline, 3, &order));
        co_await std::move(first);
        co_await std::move(second);
        co_await std::move(third);
    }());

    REQUIRE(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("a task destroyed after its timer fired but before it resumed never resumes")
{
    ito::loop                      loop{};
    bool                           resumed = false;
    std::optional<ito::task<void>> sleeper{};

    loop.run_until_complete([&]() -> ito::coro<> {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5);
        sleeper.emplace(loop.create_task([](std::chrono::steady_clock::time_point deadline, bool* resumed) -> ito::coro<> {
            co_await ito::async::sleep_until(deadline);
            *resumed = true;
        }(deadline, &resumed)));

        auto [done_promise, done] = ito::async::promise<>::create();
        std::optional<ito::scheduled_callback> destroy{};
        std::optional<ito::scheduled_callback> finish{};

        // runs after `sleeper` has started and suspended: waits out the deadline, so the next tick starts by queueing
        // `sleeper`'s frame behind `destroy`, which then destroys the task while its frame is queued
        const auto wait = loop.schedule([&]() noexcept {
            while (std::chrono::steady_clock::now() < deadline)
            {
            }
            destroy.emplace(loop.schedule([&]() noexcept {
                sleeper.reset();
                finish.emplace(loop.schedule([&]() noexcept { done_promise.set_result(); }));
            }));
        });
        co_await std::move(done);
    }());

    REQUIRE_FALSE(sleeper.has_value());
    REQUIRE_FALSE(resumed);
}

TEST_CASE("tasks queued or sleeping can outlive their loop")
{
    // destroying them after the loop must not touch it: the loop's queues unlinked them when it died (ASan checks)
    std::optional<ito::task<void>> sleeping{};
    std::optional<ito::task<void>> not_started{};
    {
        ito::loop loop{};
        loop.run_until_complete([&]() -> ito::coro<> {
            sleeping.emplace(loop.create_task([]() -> ito::coro<> {
                co_await ito::async::sleep_for(std::chrono::hours(1));
            }()));

            // lets `sleeping` start and register its timer
            auto [tick_promise, tick] = ito::async::promise<>::create();
            const auto resolve_tick   = loop.schedule([&]() noexcept { tick_promise.set_result(); });
            co_await std::move(tick);

            not_started.emplace(loop.create_task([]() -> ito::coro<> {
                co_return;
            }()));
        }());
    }
    sleeping.reset();
    not_started.reset();
}
