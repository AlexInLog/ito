// Built as its own executable: it replaces the global operator new/delete to fail an allocation on demand, which
// must not leak into the other tests (and keeps sanitizers' own new/delete checks for them).

#include <catch2/catch_test_macros.hpp>
#include <ito/async/future.hpp>
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <optional>
#include <utility>

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

// GCC inlines these into new-expression sites and then reports free() on a pointer from operator new as mismatched,
// although the replacement operator new above allocates with malloc().
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void operator delete(void* ptr) noexcept
{
    std::free(ptr); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif

TEST_CASE("sleeping registers and fires its timer without allocating")
{
    using namespace std::chrono_literals;

    ito::loop loop{};
    bool      nothing_allocated = false;
    loop.run_until_complete([](bool* nothing_allocated) -> ito::coro<> {
        // armed across the whole sleep: registering the timer, the loop's wait and the resume must not consume it
        g_fail_next_allocation = true;
        co_await ito::async::sleep_for(1ms);
        *nothing_allocated     = g_fail_next_allocation;
        g_fail_next_allocation = false;
    }(&nothing_allocated));

    REQUIRE(nothing_allocated);
}

TEST_CASE("resolving a future queues its awaiter without allocating")
{
    ito::loop          loop{};
    std::optional<int> received{};
    bool               nothing_allocated = false;

    loop.run_until_complete([&]() -> ito::coro<> {
        auto [promise, future] = ito::async::promise<int>::create();
        auto awaiter           = loop.create_task([](ito::async::future<int> f, std::optional<int>* out) -> ito::coro<> {
            *out = co_await std::move(f);
        }(std::move(future), &received));

        // lets `awaiter` start and suspend on its future
        auto [tick_promise, tick] = ito::async::promise<>::create();
        const auto resolve_tick   = loop.schedule([&]() noexcept { tick_promise.set_result(); });
        co_await std::move(tick);

        g_fail_next_allocation = true;
        promise.set_result(42);
        nothing_allocated      = g_fail_next_allocation;
        g_fail_next_allocation = false;

        co_await std::move(awaiter);
    }());

    REQUIRE(nothing_allocated);
    REQUIRE(received == 42);
}

TEST_CASE("a callback that fails to allocate throws from schedule() and leaves the loop usable")
{
    ito::loop           loop{};
    std::array<int, 16> padding{}; // too big to store inline, so schedule() allocates

    auto queue_big_callback = [&]() {
        g_fail_next_allocation = true;
        return loop.schedule([padding]() noexcept { static_cast<void>(padding); });
    };
    REQUIRE_THROWS_AS(queue_big_callback(), std::bad_alloc);
    g_fail_next_allocation = false;

    // nothing half-queued is left behind: a callback scheduled afterwards runs, and the run ends normally
    bool       ran   = false;
    const auto after = loop.schedule([&ran]() noexcept { ran = true; });
    loop.run_until_complete([]() -> ito::coro<> {
        co_return;
    }());
    REQUIRE(ran);
}
