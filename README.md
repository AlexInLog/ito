# ito

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) [![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=AlexInLog_ito&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=AlexInLog_ito)

A C++20/23 async runtime, aiming for an `asyncio`-style API with a work-stealing scheduler and an `io_uring`-based reactor.

> **Status: early stage.** The public API is unstable and internals described in the roadmap below are not implemented yet. See [Current state](#current-state) for what actually exists today.

## Why

Most C++ coroutine libraries either expose raw, easy-to-misuse primitives, or hide everything behind a heavyweight framework. `ito` is an attempt to land in between: a small, explicit `coro<T>` type with predictable ownership rules, built up incrementally into a real multi-threaded runtime.

## Current state

What exists right now:

- `ito::coro<T>` — a lazily-started, single-owner coroutine type (`initial_suspend` = always suspend). Supports `T = void` and move-only result types.
- `ito::executor` — currently a synchronous, single-shot runner (`executor{}.run(coro)`). Not yet a scheduler.
- Exception propagation from the coroutine body to the caller via `ito::exceptions::*`.
- Chaining: a `coro<T>` can `co_await` another `coro<U>`.

What's *not* here yet (see [Roadmap](#roadmap)): multi-threading, work-stealing, I/O, timers, cancellation.

### Example

```cpp
#include <ito/coro.hpp>
#include <ito/executor.hpp>

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
    ito::executor executor;
    std::string   result = executor.run(greet());
    // result == "answer: 42"
}
```

## Roadmap

- [ ] Work-stealing multi-threaded scheduler
- [ ] `io_uring`-based reactor for async I/O
- [ ] Timers / sleep
- [ ] Cancellation
- [ ] Structured concurrency helpers (`join`, `select`, task groups)

## Building

`ito` is header-only; dependencies are managed with [Conan](https://conan.io/) and fetched automatically through `cmake-conan` on first configure.

```bash
cmake --preset default
cmake --build --preset default
```

Useful CMake options:

| Option             | Default | Description                     |
|---------------------|---------|----------------------------------|
| `ITO_BUILD_TESTS`   | `ON`    | Build the test suite            |

Requires a C++23-capable compiler (GCC 13+ / Clang 17+).

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2) and [trompeloeil](https://github.com/rollbear/trompeloeil), and cover coroutine lifetime, result move/copy semantics, and exception propagation.

```bash
ctest --preset default
```

Sanitizer builds (ASan/UBSan, TSan) are available as build presets for local and CI use.

## License

[MIT](LICENSE)
