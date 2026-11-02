# ito

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) [![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=AlexInLog_ito&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=AlexInLog_ito) [![Coverage](https://sonarcloud.io/api/project_badges/measure?project=AlexInLog_ito&metric=coverage)](https://sonarcloud.io/summary/new_code?id=AlexInLog_ito) [![CodSpeed Badge](https://img.shields.io/endpoint?url=https://app.codspeed.io//badge.json)](https://app.codspeed.io//AlexInLog/ito?utm_source=badge)

A C++20 async runtime, aiming for an `asyncio`-style API with a work-stealing scheduler and an `io_uring`-based reactor.

> [!IMPORTANT]
> **Status: early stage.** The public API is unstable and internals described in the roadmap below are not implemented yet. See [Current state](#current-state) for what actually exists today.

## Why

Most C++ coroutine libraries either expose raw, easy-to-misuse primitives, or hide everything behind a heavyweight framework. `ito` is an attempt to land in between: a small, explicit `coro<T>` type with predictable ownership rules, built up incrementally into a real multi-threaded runtime.

## Current state

What exists right now:

- `ito::coro<T>` — a lazily-started, single-owner coroutine type (`initial_suspend` = always suspend). Supports `T = void` and move-only result types.
- `ito::loop` — runs the coroutines and tasks scheduled onto it.
- Exception propagation from the coroutine body to the caller via `ito::exceptions::*`.
- Chaining: a `coro<T>` can `co_await` another `coro<U>`.
- `ito::async::promise<T>`/`ito::async::future<T>` — `std::promise`/`std::future`-style split: `promise<T>::create()`
  returns a connected pair, `promise<T>` is the write side (`set_result`/`set_exception`), `future<T>` is the
  single-consumption, `co_await`-able read side.
- `ito::task<T>` — created via `loop.create_task(coro)`, starts running independently of when (or whether) it's
  `co_await`ed; destroying it before it's awaited cancels it. A task that hasn't started never runs; a task suspended
  on a `future` or a sleep has its coroutine frame destroyed at that suspension point, so locals are destroyed but
  nothing after the `co_await` runs. There's no cooperative cancellation signal yet, so the task can't observe or
  react to being cancelled. An exception thrown inside a task is rethrown by `co_await`; if the task is destroyed
  without being awaited, the exception is dropped silently.
- `ito::async::sleep_for`/`ito::async::sleep_until` — suspend a coroutine until a duration elapses or a
  `steady_clock` deadline is reached, backed by a timer queue on `ito::loop`.
- `loop.call_soon(callback)` — queues a plain callback to run on the next tick. Callbacks must be `noexcept`
  (enforced at compile time): nothing awaits a callback, so there's nowhere to deliver its exception, and errors
  belong in coroutines and promises instead. Callbacks are stored move-only, and one queued from a coroutine can run
  after that coroutine has finished, so it should own what it touches rather than capture the coroutine's locals by
  reference. Resolving a promise from a callback is the typical use:

  ```cpp
  auto [promise, future] = ito::async::promise<int>::create();
  loop.call_soon([p = std::move(promise)]() mutable noexcept { p.set_result(42); });
  const int value = co_await std::move(future);
  ```

What's *not* here yet (see [Roadmap](#roadmap)): multi-threading, work-stealing, I/O, cooperative cancellation.

### Scheduling order

- `ito::loop` runs in ticks. At the start of a tick it moves every timer whose deadline has passed onto the ready
  queue, then runs exactly the entries that were queued at that point. Anything scheduled while the tick runs
  (`call_soon`, `create_task`, resumes) waits for the next tick, in FIFO order.
- When a `coro<T>` or `task<T>` finishes, the coroutine awaiting it resumes right away, without a trip through the
  queue.
- Resolving or destroying a `promise<T>` never resumes the coroutine awaiting its `future<T>` inside that call: the
  resume is queued behind whatever is already scheduled.

### Example

```cpp
#include <ito/coro.hpp>
#include <ito/loop.hpp>

ito::coro<int> compute()
{
    co_return 42;
}

ito::coro<std::string> greet()
{
    const int value = co_await compute();
    co_return "answer: " + std::to_string(value);
}

int main()
{
    ito::loop   loop;
    std::string result = loop.run_until_complete(greet());
    // result == "answer: 42"
}
```

`loop.create_task` queues a coroutine to start running on the next scheduler tick, whether or not you ever `co_await` it, as long as the task stays alive; destroying it before it's awaited cancels it:

```cpp
#include <ito/coro.hpp>
#include <ito/loop.hpp>
#include <ito/task.hpp>

int main()
{
    ito::loop loop;
    int       result = loop.run_until_complete([&loop]() -> ito::coro<int> {
        auto task = loop.create_task(compute());
        // `task` is queued, not running yet: it starts once this coroutine
        // suspends, which is what `co_await` below does
        co_return co_await std::move(task);
    }());
    // result == 42
}
```

A coroutine lambda's captures live in the lambda object, not in the coroutine, so don't pass a capturing lambda
straight to `create_task` as a temporary: `loop.create_task([&]() -> ito::coro<> { ... }())` destroys the lambda,
and with it the captures, before the task runs. Name the lambda so it outlives the task, or pass what the body needs
as parameters. Calling one inline in `run_until_complete(...)` is fine, since the run finishes before that statement
does. clang-tidy's `cppcoreguidelines-avoid-capturing-lambda-coroutines` flags capturing coroutine lambdas, if you
want a check for this in your own code; it can't tell the safe form from the unsafe one, so expect it to flag both.

`ito::async::sleep_for`/`sleep_until` suspend the calling coroutine without blocking other queued work; the loop
resumes it once its deadline is reached:

```cpp
#include <ito/async/sleep.hpp>
#include <ito/coro.hpp>
#include <ito/loop.hpp>

#include <chrono>

int main()
{
    ito::loop loop;
    loop.run_until_complete([]() -> ito::coro<> {
        co_await ito::async::sleep_for(std::chrono::milliseconds(10));
        co_return;
    }());
}
```

## Roadmap

- [ ] Work-stealing multi-threaded scheduler
- [ ] `io_uring`-based reactor for async I/O
- [x] Timers / sleep
- [ ] Cancellation
- [ ] Structured concurrency helpers (`join`, `select`, task groups)

## Building

`ito` is header-only; dependencies are managed with [Conan](https://conan.io/) and fetched automatically through `cmake-conan` on first configure.

```bash
cmake --preset release
cmake --build --preset build
```

For local development, `lint` runs the same build with clang-tidy + cppcheck enabled:

```bash
cmake --preset lint
cmake --build --preset build
```

`tests/.clang-tidy` turns off two checks for the tests: `cppcoreguidelines-avoid-capturing-lambda-coroutines`, which
stays on for the library (see the `create_task` note above), and `clang-analyzer-core.StackAddressEscape`, which
reports a false positive on futures returned while their promise is still a local.

Useful CMake options:

| Option                | Default | Description                     |
|------------------------|---------|----------------------------------|
| `ITO_BUILD_TESTS`      | `ON`    | Build the test suite            |
| `ITO_BUILD_BENCHMARKS` | `ON`    | Build the benchmark suite       |

Requires a C++20-capable compiler.

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2) and [trompeloeil](https://github.com/rollbear/trompeloeil), and cover coroutine lifetime, result move/copy semantics, exception propagation, and timer/sleep scheduling. Allocation-failure paths are tested in a separate executable, `ito_alloc_failure_tests`, which replaces the global `operator new`; it isn't built under TSan, LSan or MSan, whose runtimes define their own.

```bash
ctest --preset tests
```

Sanitizer builds (ASan, TSan, LSan, UBSan, MSan) are available as configure presets (`sanitize-asan`, `sanitize-tsan`,
`sanitize-lsan`, `sanitize-ubsan`, `sanitize-msan`) for local and CI use — use the `sanitize` test preset with them:

```bash
cmake --preset sanitize-asan
cmake --build --preset build
ctest --preset sanitize
```

## Benchmarks

Benchmarks live in `benchmarks/` and run via Google Benchmarks but patched by [codspeed.io](https://codspeed.io/). CI tracks results over time and posts
regressions as PR comments. Historical results at [codspeed](https://app.codspeed.io/AlexInLog/ito)

## License

[MIT](LICENSE)
