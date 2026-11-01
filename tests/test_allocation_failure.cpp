// Built as its own executable: it replaces the global operator new/delete to fail an allocation on demand, which
// must not leak into the other tests (and keeps sanitizers' own new/delete checks for them).

#include <catch2/catch_test_macros.hpp>
#include <ito/async/future.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/exceptions.hpp>
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

    // The transfer only allocates when the ready queue has to grow, which depends on how many entries are queued
    // ahead of it and on the standard library's deque block size (one entry per block on MSVC, about a dozen on
    // libstdc++, about a hundred on libc++, which also recycles an emptied front block). So queue a growing number
    // of live entries ahead of it until the transfer is what has to allocate.
    bool hit = false;
    for (int queued = 0; queued < 256 && !hit; ++queued)
    {
        ito::loop                      loop{};
        std::optional<ito::task<void>> sleeper{}; // outlives the failed run, so a lost timer would show
        bool                           resumed = false;
        auto [never_resolved, never]   = ito::async::promise<>::create();

        try
        {
            loop.run_until_complete([&]() -> ito::coro<> {
                sleeper.emplace(loop.create_task([](bool* resumed) -> ito::coro<> {
                    co_await ito::async::sleep_for(1ms);
                    *resumed = true;
                }(&resumed)));

                // runs after `sleeper` has started and suspended: waits out its deadline, so the next tick starts by
                // moving its timer into the ready queue behind `queued` callbacks, and makes that tick's first
                // allocation fail
                loop.call_soon([&loop, queued]() noexcept {
                    const auto deadline = std::chrono::steady_clock::now() + 2ms;
                    while (std::chrono::steady_clock::now() < deadline)
                    {
                    }
                    for (int i = 0; i < queued; ++i)
                        loop.call_soon([]() noexcept {});
                    g_fail_next_allocation = true;
                });

                // keeps this run going until it either throws or runs dry, without a timer of its own
                co_await std::move(never);
            }());
        }
        catch (const std::bad_alloc&)
        {
            // only a failure before `sleeper` ran is the case under test; a later one hit some other allocation
            hit = !resumed;
        }
        catch (const ito::exceptions::empty_value&) // NOLINT(bugprone-empty-catch)
        {
            // the transfer had room and didn't allocate: the run ran dry with `never` unresolved, try the next size
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
