// Built as its own executable: it replaces the global operator new/delete to fail an allocation on demand, which
// must not leak into the other tests (and keeps sanitizers' own new/delete checks for them).

#include <catch2/catch_test_macros.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <optional>

namespace
{
    // When set, the next allocation throws std::bad_alloc and this resets, so only that one allocation fails.
    bool g_fail_next_allocation = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

void* operator new(std::size_t size)
{
    if (g_fail_next_allocation)
    {
        g_fail_next_allocation = false;
        throw std::bad_alloc{};
    }
    if (void* ptr = std::malloc(size == 0 ? 1 : size)) // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
        return ptr;
    throw std::bad_alloc{};
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

TEST_CASE("a timer whose transfer to the ready queue fails to allocate still fires later")
{
    using namespace std::chrono_literals;

    // Whether moving a due timer into the ready queue allocates depends on where the queue's storage currently
    // ends, so retry with a growing number of callbacks run beforehand until that transfer is what fails.
    bool hit = false;
    for (int fill = 0; fill < 64 && !hit; ++fill)
    {
        ito::loop                      loop{};
        std::optional<ito::task<void>> sleeper{}; // outlives the failed run, so a lost timer would show
        bool                           resumed = false;

        try
        {
            loop.run_until_complete([&]() -> ito::coro<> {
                sleeper.emplace(loop.create_task([](bool* resumed) -> ito::coro<> {
                    co_await ito::async::sleep_for(1ms);
                    *resumed = true;
                }(&resumed)));

                for (int i = 0; i < fill; ++i)
                    loop.call_soon([]() noexcept {});

                // runs after `sleeper` has started and suspended; once its deadline passes, the next allocation
                // fails, which is the queue growing to take that due timer unless the queue still has room
                const auto deadline = std::chrono::steady_clock::now() + 2ms;
                loop.call_soon([deadline]() noexcept {
                    while (std::chrono::steady_clock::now() < deadline)
                    {
                    }
                    g_fail_next_allocation = true;
                });

                co_await ito::async::sleep_for(20ms);
            }());
        }
        catch (const std::bad_alloc&)
        {
            // only a failure before `sleeper` ran is the case under test; a later one hit some other allocation
            hit = !resumed;
        }
        g_fail_next_allocation = false;

        if (hit)
        {
            // the failed transfer must have left the timer in place, so the next run still resumes `sleeper`
            loop.run_until_complete([]() -> ito::coro<> {
                co_await ito::async::sleep_for(5ms);
            }());
            REQUIRE(resumed);
        }
    }
    REQUIRE(hit);
}
