#include <ito/exceptions.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <ito/coro.hpp>
#include <ito/executor.hpp>
#include <trompeloeil/lifetime.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <coroutine>
#include <memory>
#include <stdexcept>


struct lifetime_tracker
{
    virtual ~lifetime_tracker() { }
    lifetime_tracker() = default;

    MAKE_MOCK(call, auto()->void);
};


struct copy_count_tracker_impl
{
    MAKE_MOCK(copy_constructor, auto()->void);
    MAKE_MOCK(move_constructor, auto()->void);
};

class copy_count_tracker
{
public:
    copy_count_tracker() = default;

    copy_count_tracker(const copy_count_tracker& v)
        : m_impl(v.m_impl)
    {
        m_impl->copy_constructor();
    }

    copy_count_tracker(copy_count_tracker&& v) noexcept
        : m_impl(std::move(v.m_impl))
    {
        m_impl->move_constructor();
    }

    copy_count_tracker& operator=(const copy_count_tracker& v)     = delete;
    copy_count_tracker& operator=(copy_count_tracker&& v) noexcept = delete;

    copy_count_tracker_impl& impl() { return *m_impl; }

private:
    std::shared_ptr<copy_count_tracker_impl> m_impl = std::make_shared<copy_count_tracker_impl>();
};


TEST_CASE("Validate base coroutine checks")
{
    auto owner = std::make_unique<trompeloeil::deathwatched<lifetime_tracker>>();
    auto mock  = owner.get(); // use raw unowned pointer

    {
        REQUIRE_DESTRUCTION(*mock);

        auto make_coro = [&, c = std::move(owner)](auto...) -> ito::coro<int> {
            c->call();
            co_return 42;
        };

        SECTION("create and destruct coro")
        {
            auto inner      = std::make_unique<trompeloeil::deathwatched<lifetime_tracker>>();
            auto inner_mock = inner.get(); // use raw unowned pointer

            REQUIRE_DESTRUCTION(*inner_mock);
            {
                auto coro = make_coro(std::move(inner));
            }
        }

        SECTION("create and run task")
        {
            auto coro = make_coro();
            REQUIRE_CALL(*mock, call());

            const auto res = ito::executor{}.run(std::move(coro));
            REQUIRE(res == 42);

            SECTION("execute same coro twice") {
                REQUIRE_THROWS_AS(ito::executor{}.run(std::move(coro)), ito::exceptions::invalid_coro_handle_state);
            }
        }

        SECTION("run task from parent")
        {
            auto       coro       = make_coro();
            const auto child_coro = [&]() -> ito::coro<int> {
                co_return co_await std::move(coro) + 2;
            };

            REQUIRE_CALL(*mock, call());
            const auto res = ito::executor{}.run(child_coro());
            REQUIRE(res == 44);
        }
        SECTION("run task from parent of another type")
        {
            auto       coro       = make_coro();
            const auto child_coro = [&]() -> ito::coro<std::string> {
                co_return std::to_string(co_await std::move(coro));
            };

            REQUIRE_CALL(*mock, call());
            const auto res = ito::executor{}.run(child_coro());
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

            REQUIRE_CALL(*mock, call());
            ito::executor{}.run(child_coro());
            REQUIRE(child_res == 42);
        }
    }
}

TEST_CASE("return move_only value")
{
    auto owner = std::make_unique<trompeloeil::deathwatched<lifetime_tracker>>();
    {
        auto mock      = owner.get();
        auto make_task = [&]() -> ito::coro<std::unique_ptr<trompeloeil::deathwatched<lifetime_tracker>>> {
            co_return std::move(owner);
        };

        auto task = make_task();
        auto r    = ito::executor{}.run(std::move(task));
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

    [[maybe_unused]] auto _ = ito::executor{}.run(make_task());
}

TEST_CASE("return-move object")
{
    copy_count_tracker t{};

    auto make_task = [&]() -> ito::coro<copy_count_tracker> {
        co_return std::move(t);
    };

    trompeloeil::sequence s{};
    REQUIRE_CALL(t.impl(), move_constructor()).TIMES(2).IN_SEQUENCE(s);

    [[maybe_unused]] auto _ = ito::executor{}.run(make_task());
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

    REQUIRE_THROWS_AS(ito::executor{}.run(std::move(task)), custom_error);
}

TEST_CASE("coro co_await suspend_always") {
    auto make_task = []() -> ito::coro<> {
        co_await std::suspend_always{};
        co_return;
    };

    auto task = make_task();
    REQUIRE_THROWS_AS(ito::executor{}.run(std::move(task)), ito::exceptions::empty_value);
}
