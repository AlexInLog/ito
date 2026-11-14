// Built as its own executable: it replaces the global operator new/delete to fail an allocation on demand, which
// must not leak into the other tests (and keeps sanitizers' own new/delete checks for them).

#include <catch2/catch_test_macros.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <new>

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

TEST_CASE("a sleep whose timer fails to register throws into the sleeping coroutine")
{
    using namespace std::chrono_literals;

    ito::loop loop{};
    bool      caught = false;
    loop.run_until_complete([](bool* caught) -> ito::coro<> {
        // a fresh loop's timer heap has no capacity yet, so registering this first timer is the next allocation
        g_fail_next_allocation = true;
        try
        {
            co_await ito::async::sleep_for(10ms);
        }
        catch (const std::bad_alloc&)
        {
            *caught = true;
        }
        g_fail_next_allocation = false;
    }(&caught));

    // the coroutine got the failure at its co_await and carried on, and no timer was left behind to wait for
    REQUIRE(caught);
}

TEST_CASE("a callback that fails to allocate is never queued")
{
    ito::loop           loop{};
    std::array<int, 16> padding{}; // too big to store inline, so call_soon() allocates
    bool                ran = false;

    auto queue_big_callback = [&]() {
        g_fail_next_allocation = true;
        return loop.call_soon([&ran, padding]() noexcept { ran = padding.front() == 0; });
    };
    REQUIRE_THROWS_AS(queue_big_callback(), std::bad_alloc);
    g_fail_next_allocation = false;

    loop.run_until_complete([]() -> ito::coro<> {
        co_return;
    }());
    REQUIRE_FALSE(ran);
}
