
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
            REQUIRE_THROWS_AS(res.set_result(10), ito::exceptions::invalid_loop_state);
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
}
