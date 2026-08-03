
#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/async/future.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
#include <ito/loop.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <exception>
#include <optional>
#include <stdexcept>

struct custom_error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};


TEST_CASE("future basics")
{
    ito::loop             loop{};
    call_mock             mock{};
    trompeloeil::sequence s{};

    SECTION("future never resolved: awaiting coroutine is left suspended forever")
    {
        auto [promise, res] = ito::async::promise<int>::create();
        REQUIRE_FALSE(promise.is_ready());
        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<int> {
                co_return co_await std::move(res);
            }()),
            ito::exceptions::empty_value
        );

        SECTION("and then resolve the promise anyway")
        {
            // run_until_complete() above threw and unwound, destroying the still-suspended
            // coroutine; that destruction already tore down the `co_await std::move(res)`
            // awaitable (clearing this promise's continuation), so resolving it now, with no
            // one left listening, must be a harmless no-op rather than a resume through freed
            // memory
            promise.set_result(20);

            loop.run_until_complete([&]() -> ito::coro<> {
                auto [inner_promise, inner_res] = ito::async::promise<int>::create();
                loop.call_soon([&]() { inner_promise.set_result(10); });
                co_await std::move(inner_res);
            }());
        }
    }

    SECTION("resolve future before await")
    {
        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            auto [promise, fut] = ito::async::promise<int>::create();
            promise.set_result(10);
            loop.call_soon([&]() { mock.call(0); });
            co_return co_await std::move(fut);
        }());

        REQUIRE(res == 10);
    }

    SECTION("resolve future as error before await")
    {
        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            auto [promise, fut] = ito::async::promise<int>::create();
            promise.set_exception(std::make_exception_ptr(custom_error{"custom error"}));
            REQUIRE(promise.is_ready());
            REQUIRE_THROWS_AS(co_await std::move(fut), custom_error);
            co_return 2;
        }());

        REQUIRE(res == 2);
    }

    SECTION("resolve future as error before await and don't catch it")
    {
        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<int> {
                auto [promise, fut] = ito::async::promise<int>::create();
                promise.set_exception(std::make_exception_ptr(custom_error{"custom error"}));
                co_await std::move(fut);
                co_return 2;
            }()),
            custom_error
        );
    }

    SECTION("resolve future inside call_soon")
    {
        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            auto [promise, fut] = ito::async::promise<int>::create();
            int value           = 10;

            loop.call_soon([&]() { mock.call(0); });
            loop.call_soon([&]() {
                mock.call(1);

                REQUIRE_FALSE(promise.is_ready());
                loop.call_soon([&]() { mock.call(3); });
                promise.set_result(value);
                REQUIRE(promise.is_ready());
                loop.call_soon([&]() { mock.call(4); });
            });
            loop.call_soon([&]() { mock.call(2); });

            REQUIRE_CALL(mock, call(0)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);

            co_return co_await std::move(fut);
        }());

        REQUIRE(res == 10);
    }
    SECTION("resolve promise twice")
    {
        auto [promise, fut] = ito::async::promise<int>::create();
        promise.set_result(10);
        REQUIRE(promise.is_ready());
        REQUIRE_THROWS_AS(promise.set_result(20), ito::exceptions::value_is_set);
        REQUIRE_THROWS_AS(promise.set_exception({}), ito::exceptions::value_is_set);
    }
    SECTION("future of void")
    {
        loop.run_until_complete([&]() -> ito::coro<> {
            auto [promise, res] = ito::async::promise<>::create();
            promise.set_result();
            co_await std::move(res);
        }());
    }

    SECTION("promise reports is_ready before and after set_result")
    {
        auto [promise, fut] = ito::async::promise<int>::create();
        REQUIRE_FALSE(promise.is_ready());
        promise.set_result(5);
        REQUIRE(promise.is_ready());
    }

    SECTION("destroy promise after resolving it, before the future is ever awaited")
    {
        // regression test: the promise used to be the sole owner of the shared state, so
        // dropping it here (before anyone awaited `fut`) tore the state down and turned an
        // already-delivered value into a spurious broken_future
        auto make_future = [] {
            auto [promise, fut] = ito::async::promise<int>::create();
            promise.set_result(42);
            return std::move(fut);
        };

        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            co_return co_await make_future();
        }());

        REQUIRE(res == 42);
    }

    SECTION("destroy promise before ever resolving it, and before the future is awaited")
    {
        auto make_future = [] {
            auto [promise, fut] = ito::async::promise<int>::create();
            return std::move(fut);
            // promise destroyed here, having never called set_result()/set_exception()
        };

        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<int> {
                co_return co_await make_future();
            }()),
            ito::exceptions::broken_future
        );
    }

    SECTION("destroy promise while a task is still suspended awaiting its future -> broken_future")
    {
        auto [promise, future] = ito::async::promise<int>::create();

        std::optional<ito::task<int>> task{};

        loop.run_until_complete([&]() -> ito::coro<void> {
            auto inner = [&]() -> ito::coro<int> {
                mock.call(-1);
                co_return co_await std::move(future);
            };

            REQUIRE_CALL(mock, call(-1));
            task.emplace(loop.create_task(inner()));

            // hop through the loop once so `task` actually starts and suspends inside
            // `co_await future`, rather than being cancelled before it ever ran
            auto [tick_promise, tick] = ito::async::promise<>::create();
            loop.call_soon([&]() { tick_promise.set_result(); });
            co_await std::move(tick);

            // drop `promise` while `task` is still suspended awaiting its `future`: the
            // shared state must stay alive long enough to deliver `broken_future` to `task`,
            // not just quietly leave it suspended forever
            REQUIRE_FALSE(promise.is_ready());
            {
                auto discard = std::move(promise);
            }

            // `task` must actually resume with `broken_future` rather than hang; awaiting it
            // here both drives that resume through the loop and observes the exception
            REQUIRE_THROWS_AS(co_await std::move(*task), ito::exceptions::broken_future);
        }());
    }
}
