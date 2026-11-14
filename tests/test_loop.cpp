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
        const auto first = loop.call_soon([&]() noexcept { mock.call(1); });
        // queued by the coroutine but held out here, so it outlives the coroutine and runs during the next run
        std::optional<ito::scheduled_callback> second{};

        {
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

            loop.run_until_complete([&]() -> ito::coro<> {
                second.emplace(loop.call_soon([&]() noexcept { mock.call(2); }));
                co_return;
            }());
        }

        const auto third = loop.call_soon([&]() noexcept { mock.call(3); });

        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);
        loop.run_until_complete([&]() -> ito::coro<> {
            co_return;
        }());
    }

    SECTION("suspending coro")
    {
        const auto first = loop.call_soon([&]() noexcept { mock.call(1); });

        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);

        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<> {
                const auto second = loop.call_soon([&]() noexcept { mock.call(2); });
                REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

                co_await std::suspend_always{};
                co_return;
            }()),
            ito::exceptions::empty_value
        );

        const auto never_run = loop.call_soon([&]() noexcept { mock.call(3); });
    }
}

template<typename Fn>
concept call_soon_accepts = requires(ito::loop& loop, Fn fn) { loop.call_soon(std::move(fn)); };

TEST_CASE("call_soon only accepts callbacks that can't throw")
{
    // a callback has nobody awaiting it, so a throwing one is rejected at compile time rather than unwinding
    // whichever run_until_complete() happens to be running
    STATIC_REQUIRE(call_soon_accepts<decltype([]() noexcept { })>);
    STATIC_REQUIRE_FALSE(call_soon_accepts<decltype([]() { })>);
    STATIC_REQUIRE_FALSE(call_soon_accepts<std::function<void()>>);

    // callbacks are stored move-only, so they can own move-only state instead of referencing it
    STATIC_REQUIRE(call_soon_accepts<decltype([p = std::unique_ptr<int>{}]() noexcept { })>);

    // a raw coroutine handle is rejected: its frame could be destroyed while the resume is queued, so coroutine
    // resumes are only queued through views that skip a destroyed frame
    STATIC_REQUIRE_FALSE(call_soon_accepts<std::coroutine_handle<>>);
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
            const auto resolve     = loop.call_soon([p = std::move(promise)]() mutable noexcept { p.set_result(); });
            co_await std::move(future);
        }()));
    }

    SECTION("a callback is cancelled when its scheduled_callback dies before it runs")
    {
        ito::loop loop{};
        bool      ran = false;
        loop.run_until_complete([&]() -> ito::coro<> {
            const auto callback = loop.call_soon([&]() noexcept { ran = true; });
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
            const auto callback = loop.call_soon([token]() noexcept { ++*token; });
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
                callback.emplace(loop.call_soon([token]() noexcept { ++*token; }));
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
            const auto callback = loop.call_soon([token, padding]() noexcept { *token += 1 + padding.front(); });
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
                callback.emplace(loop.call_soon([token, padding]() noexcept { *token += 1 + padding.front(); }));
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
            from_child.emplace(loop.call_soon([&]() noexcept { mock.call(2); }));
            co_return 1;
        };

        REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
        REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);

        loop.run_until_complete([&]() -> ito::coro<> {
            auto task = loop.create_task(child());
            mock.call(co_await std::move(task));

            // give the callback queued by `child` a chance to run before this coroutine finishes
            auto [tick_promise, tick] = ito::async::promise<>::create();
            const auto resolve_tick   = loop.call_soon([&]() noexcept { tick_promise.set_result(); });
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
            const auto                             outer = loop.call_soon([&]() noexcept {
                nested.emplace(loop.call_soon([&]() noexcept { mock.call(2); }));
                promise.set_result();
                mock.call(1);
            });
            co_await std::move(f);
            mock.call(3);
        }());
    }
}

// Needs the per-tick batch (see docs/intrusive-queue.md): the loop drains one entry at a time and checks timers before
// each, so the expired timer currently runs ahead of the work queued during that tick. Drop [!shouldfail] once the
// batch lands; Catch2 fails this test as soon as it starts passing.
TEST_CASE("a timer that expires during a tick resumes on the next tick, after work queued during that tick", "[!shouldfail]")
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
        const auto                             busy         = loop.call_soon([&]() noexcept {
            while (std::chrono::steady_clock::now() < deadline)
            {
            }
            mock.call(1);
        });
        const auto                             queue_nested = loop.call_soon([&]() noexcept {
            mock.call(2);
            nested.emplace(loop.call_soon([&]() noexcept { mock.call(3); }));
        });
        co_await ito::async::sleep_until(deadline);
        mock.call(4);
    }());
}
