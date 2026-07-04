#include <catch2/catch_test_macros.hpp>
#include <ito/task.hpp>
#include <ito/executor.hpp>
#include <coroutine>

struct lifetime_tracker
{
    explicit lifetime_tracker(int& counter)
        : m_counter(counter)
    {
        ++m_counter;
    }
    ~lifetime_tracker() { --m_counter; }

    lifetime_tracker(const lifetime_tracker&)            = delete;
    lifetime_tracker& operator=(const lifetime_tracker&) = delete;
    lifetime_tracker(lifetime_tracker&&)                 = delete;
    lifetime_tracker& operator=(lifetime_tracker&&)      = delete;

private:
    int& m_counter;
};


TEST_CASE("base task checks")
{
    int alive          = 0;
    int caputure_alive = 0;

    ito::executor executor{};

    {
        int called{};
        auto make_task = [&, c = lifetime_tracker{caputure_alive}]() -> ito::task {
            lifetime_tracker t(alive);
            ++called;
            co_await std::suspend_always{};
            ++called;
            co_return;
        };

        REQUIRE(caputure_alive == 1);

        SECTION("create task but don't run it yet")
        {
            [[maybe_unused]] auto task = make_task(); // created but never run
            // alive is still 0 here — tracker not yet constructed (lazy)
            REQUIRE(alive == 0);
            REQUIRE(called == 0);

            SECTION("run task") {
                executor.run(task);
                REQUIRE(called == 1);
            }
        }
    }

    REQUIRE(alive == 0);
    REQUIRE(caputure_alive == 0);
}
