#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/async/future.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <array>
#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>


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

TEST_CASE("schedule relatively to run_until_complete")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    SECTION("no-op coro")
    {
        const auto first = loop.schedule([&]() noexcept { mock.call(1); });
        // queued by the coroutine but held out here, so it outlives the coroutine and runs during the next run
        std::optional<ito::scheduled_callback> second{};

        {
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

            loop.run_until_complete([&]() -> ito::coro<> {
                second.emplace(loop.schedule([&]() noexcept { mock.call(2); }));
                co_return;
            }());
        }

        const auto third = loop.schedule([&]() noexcept { mock.call(3); });

        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);
        loop.run_until_complete([&]() -> ito::coro<> {
            co_return;
        }());
    }

    SECTION("suspending coro")
    {
        const auto first = loop.schedule([&]() noexcept { mock.call(1); });

        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<> {
                const auto second = loop.schedule([&]() noexcept { mock.call(2); });
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

                co_await std::suspend_always{};
                co_return;
            }()),
            ito::exceptions::empty_value
        );

        const auto never_run = loop.schedule([&]() noexcept { mock.call(3); });
    }
}

template<typename Fn>
concept schedule_accepts = requires(ito::loop& loop, Fn fn) { loop.schedule(std::move(fn)); };

TEST_CASE("schedule only accepts callbacks that can't throw")
{
    // a callback has nobody awaiting it, so a throwing one is rejected at compile time rather than unwinding
    // whichever run_until_complete() happens to be running
    STATIC_REQUIRE(schedule_accepts<decltype([]() noexcept { })>);
    STATIC_REQUIRE_FALSE(schedule_accepts<decltype([]() { })>);
    STATIC_REQUIRE_FALSE(schedule_accepts<std::function<void()>>);

    // callbacks are stored move-only, so they can own move-only state instead of referencing it
    STATIC_REQUIRE(schedule_accepts<decltype([p = std::unique_ptr<int>{}]() noexcept { })>);

    // a raw coroutine handle is rejected: resuming can throw, and a frame is only ever queued through its promise's
    // node, which unlinks itself if the frame is destroyed while queued
    STATIC_REQUIRE_FALSE(schedule_accepts<std::coroutine_handle<>>);
}

TEST_CASE("scheduled callbacks and what they capture")
{
    SECTION("a callback owning a promise resolves it")
    {
        // the coroutine can only finish if the callback resolved its future; otherwise the loop runs dry
        // and run_until_complete() throws empty_value
        ito::loop loop{};
        REQUIRE_NOTHROW(loop.run_until_complete([&]() -> ito::coro<> {
            auto [promise, future] = ito::async::promise<>::create();
            const auto resolve     = loop.schedule([p = std::move(promise)]() mutable noexcept { p.set_result(); });
            co_await std::move(future);
        }()));
    }

    SECTION("a callback is cancelled when its scheduled_callback dies before it runs")
    {
        ito::loop loop{};
        bool      ran = false;
        loop.run_until_complete([&]() -> ito::coro<> {
            const auto callback = loop.schedule([&]() noexcept { ran = true; });
            co_return; // `callback` dies with this coroutine's locals, before the loop reaches it
        }());

        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());
        REQUIRE_FALSE(ran);
    }

    auto token = std::make_shared<int>(0);

    SECTION("small callback stored inline")
    {
        SECTION("is destroyed right after it runs, while its scheduled_callback still lives")
        {
            ito::loop  loop{};
            const auto callback = loop.schedule([token]() noexcept { ++*token; });
            loop.run_until_complete([]() -> ito::coro<> {
                co_return;
            }());

            REQUIRE(*token == 1);
            REQUIRE(token.use_count() == 1);
        }
        SECTION("is destroyed with its scheduled_callback if it never runs, even one that outlives the loop")
        {
            std::optional<ito::scheduled_callback> callback{};
            {
                ito::loop loop{};
                callback.emplace(loop.schedule([token]() noexcept { ++*token; }));
            }
            REQUIRE(*token == 0);
            REQUIRE(token.use_count() == 2);

            callback.reset();
            REQUIRE(token.use_count() == 1);
        }
    }

    SECTION("big callback stored on the heap")
    {
        std::array<int, 16> padding{};

        SECTION("is destroyed right after it runs, while its scheduled_callback still lives")
        {
            ito::loop  loop{};
            const auto callback = loop.schedule([token, padding]() noexcept { *token += 1 + padding.front(); });
            loop.run_until_complete([]() -> ito::coro<> {
                co_return;
            }());

            REQUIRE(*token == 1);
            REQUIRE(token.use_count() == 1);
        }
        SECTION("is destroyed with its scheduled_callback if it never runs, even one that outlives the loop")
        {
            std::optional<ito::scheduled_callback> callback{};
            {
                ito::loop loop{};
                callback.emplace(loop.schedule([token, padding]() noexcept { *token += 1 + padding.front(); }));
            }
            REQUIRE(*token == 0);
            REQUIRE(token.use_count() == 2);

            callback.reset();
            REQUIRE(token.use_count() == 1);
        }
    }
}

TEST_CASE("scheduling order")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    SECTION("a finished task resumes its awaiter inline, ahead of work the task scheduled")
    {
        // held out here: the callback must outlive `child`, which finishes before it runs
        std::optional<ito::scheduled_callback> from_child{};
        auto                                   child = [&]() -> ito::coro<int> {
            from_child.emplace(loop.schedule([&]() noexcept { mock.call(2); }));
            co_return 1;
        };

        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

        loop.run_until_complete([&]() -> ito::coro<> {
            auto task = loop.create_task(child());
            mock.call(co_await std::move(task));

            // give the callback queued by `child` a chance to run before this coroutine finishes
            auto [tick_promise, tick] = ito::async::promise<>::create();
            const auto resolve_tick   = loop.schedule([&]() noexcept { tick_promise.set_result(); });
            co_await std::move(tick);
        }());
    }
    SECTION("resolving a promise schedules its awaiter behind already-queued work, never inline")
    {
        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);

        loop.run_until_complete([&]() -> ito::coro<> {
            auto [promise, f] = ito::async::promise<>::create();
            std::optional<ito::scheduled_callback> nested{};
            const auto                             outer = loop.schedule([&]() noexcept {
                nested.emplace(loop.schedule([&]() noexcept { mock.call(2); }));
                promise.set_result();
                mock.call(1);
            });
            co_await std::move(f);
            mock.call(3);
        }());
    }
}

TEST_CASE("a timer that expires during a tick resumes on the next tick, after work queued during that tick")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
    REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
    REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);
    REQUIRE_CALL(mock, call(4)).IN_SEQUENCE(s);

    loop.run_until_complete([&]() -> ito::coro<> {
        const auto                             deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        std::optional<ito::scheduled_callback> nested{};
        const auto                             busy         = loop.schedule([&]() noexcept {
            while (std::chrono::steady_clock::now() < deadline)
            {
            }
            mock.call(1);
        });
        const auto                             queue_nested = loop.schedule([&]() noexcept {
            mock.call(2);
            nested.emplace(loop.schedule([&]() noexcept { mock.call(3); }));
        });
        co_await ito::async::sleep_until(deadline);
        mock.call(4);
    }());
}

TEST_CASE("run_until_complete stops as soon as its coroutine is done, leaving the rest of the tick queued")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    // held out here, so they outlive the coroutine and `queued_behind` can still run in the next run
    std::optional<ito::scheduled_callback> resolve{};
    std::optional<ito::scheduled_callback> queued_behind{};

    loop.run_until_complete([&]() -> ito::coro<> {
        auto [promise, future] = ito::async::promise<>::create();
        resolve.emplace(loop.schedule([&]() noexcept {
            // queues this coroutine, then `queued_behind` after it, both for the next tick
            promise.set_result();
            queued_behind.emplace(loop.schedule([&]() noexcept { mock.call(1); }));
        }));
        co_await std::move(future);
    }());

    // the coroutine finished first in its tick, so `queued_behind` hasn't run; it runs first in the next run
    REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
    REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
    loop.run_until_complete([&]() -> ito::coro<> {
        mock.call(2);
        co_return;
    }());
}

TEST_CASE("a running callback may destroy scheduled callbacks")
{
    ito::loop loop{};
    auto      token = std::make_shared<int>(0);

    SECTION("its own: it keeps running, and its captures are released once it returns")
    {
        std::optional<ito::scheduled_callback> self{};
        self.emplace(loop.schedule([&self, token]() noexcept {
            self.reset();
            ++*token;
        }));
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());

        REQUIRE_FALSE(self.has_value());
        REQUIRE(*token == 1);
        REQUIRE(token.use_count() == 1);
    }
    SECTION("one still queued behind it, which then never runs")
    {
        std::optional<ito::scheduled_callback> second{};
        const auto                             first = loop.schedule([&second]() noexcept { second.reset(); });
        second.emplace(loop.schedule([token]() noexcept { ++*token; }));
        loop.run_until_complete([]() -> ito::coro<> {
            co_return;
        }());

        REQUIRE(*token == 0);
        REQUIRE(token.use_count() == 1);
    }
}

TEST_CASE("moving a queued scheduled_callback keeps its place in the queue")
{
    ito::loop        loop{};
    std::vector<int> order{};

    std::optional<ito::scheduled_callback> first{};
    std::optional<ito::scheduled_callback> second{};
    std::optional<ito::scheduled_callback> third{};
    first.emplace(loop.schedule([&order]() noexcept { order.push_back(1); }));
    second.emplace(loop.schedule([&order]() noexcept { order.push_back(2); }));
    third.emplace(loop.schedule([&order]() noexcept { order.push_back(3); }));

    // moved after `third` was queued: it must still run between `first` and `third`
    std::optional<ito::scheduled_callback> moved{};
    moved.emplace(std::move(*second));
    second.reset();

    loop.run_until_complete([]() -> ito::coro<> {
        co_return;
    }());
    REQUIRE(order == std::vector<int>{1, 2, 3});
}
