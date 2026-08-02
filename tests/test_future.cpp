
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
#include <memory>
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

    SECTION("do not resolve future before await")
    {
        ito::async::future<int> res{};
        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<int> {
                co_return co_await res;
            }()),
            ito::exceptions::empty_value
        );

        SECTION("and then resolve future")
        {
            res.set_result(10);
            loop.run_until_complete([&]() -> ito::coro<void> {
                REQUIRE_THROWS_AS(co_await res, ito::exceptions::future_just_awaited);
                co_return;
            }());
        }

        SECTION("and then run one more coro and resolve future and suspend")
        {
            loop.run_until_complete([&]() -> ito::coro<> {
                // it shouldn't cause SEGFAULT if we are trying to resume currently dead coro
                res.set_result(20);

                ito::async::future<int> inner_res{};
                loop.call_soon([&]() { inner_res.set_result(10); });
                co_await inner_res;
            }());
        }
    }

    SECTION("resolve future before await")
    {
        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            ito::async::future<int> res{};
            res.set_result(10);
            loop.call_soon([&]() { mock.call(0); });
            co_return co_await res;
        }());

        REQUIRE(res == 10);
    }

    SECTION("resolve future as error before await")
    {
        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            ito::async::future<int> res{};
            res.set_exception(std::make_exception_ptr(custom_error{"custom error"}));
            REQUIRE_THROWS_AS(co_await res, custom_error);
            co_return 2;
        }());

        REQUIRE(res == 2);
    }

    SECTION("resolve future as error before await and don't catch it")
    {
        REQUIRE_THROWS_AS(
            loop.run_until_complete([&]() -> ito::coro<int> {
                ito::async::future<int> res{};
                res.set_exception(std::make_exception_ptr(custom_error{"custom error"}));
                co_await res;
                co_return 2;
            }()),
            custom_error
        );
    }

    SECTION("resolve future inside call_soon")
    {
        const auto res = loop.run_until_complete([&]() -> ito::coro<int> {
            ito::async::future<int> res{};
            int                     value = 10;

            loop.call_soon([&]() { mock.call(0); });
            loop.call_soon([&]() {
                mock.call(1);

                loop.call_soon([&]() { mock.call(3); });
                res.set_result(value);
                loop.call_soon([&]() { mock.call(4); });
            });
            loop.call_soon([&]() { mock.call(2); });

            REQUIRE_CALL(mock, call(0)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(1)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(2)).IN_SEQUENCE(s);
            REQUIRE_CALL(mock, call(3)).IN_SEQUENCE(s);

            co_return co_await res;
        }());

        REQUIRE(res == 10);
    }
    SECTION("resolve future twice")
    {
        ito::async::future<int> f{};
        f.set_result(10);
        REQUIRE_THROWS_AS(f.set_result(20), ito::exceptions::value_is_set);
        REQUIRE_THROWS_AS(f.set_exception({}), ito::exceptions::value_is_set);
    }
    SECTION("future of void")
    {
        loop.run_until_complete([&]() -> ito::coro<> {
            ito::async::future<> res{};
            res.set_result();
            co_await res;
        }());
    }

    SECTION("destroy future while a task is still suspended awaiting it, then destroy the task")
    {
        auto fut = std::make_unique<ito::async::future<int>>();

        std::optional<ito::task<int>> task{};

        REQUIRE_NOTHROW(loop.run_until_complete([&]() -> ito::coro<void> {
            auto inner = [&]() -> ito::coro<int> {
                mock.call(-1);
                co_return co_await *fut;
            };

            REQUIRE_CALL(mock, call(-1));
            task.emplace(loop.create_task(inner()));

            // hop through the loop once so `task` actually starts and suspends on `co_await *fut`,
            // rather than being cancelled before it ever ran
            ito::async::future<> tick{};
            loop.call_soon([&]() { tick.set_result(); });
            co_await tick;

            // `task`'s coroutine is currently suspended inside `co_await *fut`; destroying `fut`
            // here must unregister it from the awaiting coroutine rather than leaving that
            // coroutine's awaitable pointing at freed future state
            fut.reset();

            // destroying `task` afterwards must be a harmless no-op too: it must not resume
            // through, or otherwise touch, the already-destroyed future
            task.reset();
        }()));
    }

    // TODO: enable once future is split into promise<T>/future<T> (promise<T>::create() ->
    // std::pair<promise<T>, future<T>>, backed by a ref-counted shared state) and promise<T>'s
    // destructor delivers ito::exceptions::broken_promise to a still-suspended awaiter instead of
    // leaving it to hang forever. Regression guard for that behavior:
    //
    // SECTION("destroy promise while a task is still suspended awaiting its future -> broken_promise")
    // {
    //     auto [promise, future] = ito::async::promise<int>::create();
    //
    //     std::optional<ito::task<int>> task{};
    //
    //     loop.run_until_complete([&]() -> ito::coro<void> {
    //         auto inner = [&]() -> ito::coro<int> {
    //             mock.call(-1);
    //             co_return co_await std::move(future);
    //         };
    //
    //         REQUIRE_CALL(mock, call(-1));
    //         task.emplace(loop.create_task(inner()));
    //
    //         // hop through the loop once so `task` actually starts and suspends inside
    //         // `co_await future`, rather than being cancelled before it ever ran
    //         ito::async::future<> tick{};
    //         loop.call_soon([&]() { tick.set_result(); });
    //         co_await tick;
    //
    //         // drop `promise` while `task` is still suspended awaiting its `future`: the
    //         // shared state must stay alive long enough to deliver `broken_promise` to `task`,
    //         // not just quietly leave it suspended forever
    //         { auto discard = std::move(promise); }
    //
    //         // `task` must actually resume with `broken_promise` rather than hang; awaiting it
    //         // here both drives that resume through the loop and observes the exception
    //         REQUIRE_THROWS_AS(co_await std::move(*task), ito::exceptions::broken_promise);
    //     }());
    // }
}
