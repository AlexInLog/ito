# ito

C++20 coroutine-based async runtime library. MIT licensed, header-only.
Repo: https://github.com/AlexInLog/ito

## Build and test

- Build system: CMake (presets, `Ninja` generator) + Conan (fetched automatically via `cmake-conan` on first configure).
- Tests: Catch2 + trompeloeil. Benchmarks: Google Benchmark, fetched via CodSpeed's fork (`CodSpeedHQ/codspeed-cpp`)
  through `FetchContent`, not Conan. It no longer uses Catch2's benchmark macros.
- `ITO_BUILD_TESTS`/`ITO_BUILD_BENCHMARKS` default to `OFF` (opt-in); enable them via the presets below rather than
  passing the cache variables directly.
- Configure presets: `debug`, `release`, `lint` (debug + clang-tidy + cppcheck), `coverage`, `release-tests`/
  `debug-tests` (adds `ITO_BUILD_TESTS`), `benchmarks` (adds `ITO_BUILD_BENCHMARKS`, inherits `release`), and
  sanitizer presets `sanitize-asan`, `sanitize-tsan`, `sanitize-lsan`, `sanitize-ubsan`, `sanitize-msan`.
  `codspeed-simulation`/`codspeed-walltime` are CI-only presets used by the CodSpeed workflow.
- Build preset is always `build`; test presets are `tests` (default) or `sanitize` (sets ASAN/UBSAN/TSAN options).

  ```bash
  cmake --preset sanitize-asan
  cmake --build --preset build
  ctest --preset sanitize
  ```

- Day-to-day local builds use the `release` or `lint` configure presets (`lint` adds clang-tidy + cppcheck), build
  preset `build`, test preset `tests`. `CMakeUserPresets.json` stays empty (`{"version": 4}`) — there's no local
  `default` preset in use.
- CI runs a GCC/Clang/MSVC/AppleClang matrix via GitHub Actions (`lint-ubuntu-gcc`, `lint-ubuntu-clang`, `lint-windows`,
  `lint-macos`, plus one job per sanitizer), all using `buildPreset: build`. `sanitize-msan` exists as a preset but is
  **not** run in CI.

## Directory layout

- `include/ito/` — public headers.
  - `include/ito/details/` — implementation details not part of the public API (namespace-mirrored, see below).
  - `include/ito/async/` — async primitives: `future.hpp` (`promise<T>`/`future<T>`; `promise.hpp` just includes it)
    and `sleep.hpp`.
- Namespaces:
  - `ito` — public API (`coro<T>`, `loop`, `task<T>`).
  - `ito::async` — async primitives layered on top of `coro<T>`, e.g. `promise<T>`/`future<T>` (settable from outside
    a coroutine / awaitable).
  - `ito::details`, `ito::details::utils` — implementation internals, mirroring `include/ito/details/utils/`.
  - `ito::exceptions` — exception types thrown across the library (e.g. `broken_future`).

## Core components

- `coro<T>` — the base coroutine type: lazily-started, single-owner (`initial_suspend` = always suspend).
- `promise<T>`/`future<T>` — `std::promise`/`std::future`-style split, created together via `promise<T>::create()`.
  Both sides are backed by a shared `trackable<future_state<T>>`/`weak_view` pair (see
  `details::utils::trackable`), so either side can be destroyed independently without dangling the other:
  destroying the `future` while a coroutine hasn't yet awaited it is a no-op; destroying the `promise` while a
  coroutine is suspended `co_await`ing its `future` delivers `exceptions::broken_future` to that coroutine instead
  of leaving it suspended forever. `future<T>::operator co_await() &&` is rvalue-qualified, so single-consumption is
  enforced by the type system (move-only) rather than a runtime flag.
- `task<T>` — created via `loop::create_task(coro<T>&&)`; the loop schedules it to start running on the next
  `call_soon` tick, independent of whether/when it's `co_await`ed. Destroying the `task<T>` before it's awaited
  cancels it (the underlying coroutine handle is destroyed, so the body may never run past its next suspend point).
- `details::utils::trackable<T>` — pairs an owning object with a single `weak_view` that gets nulled out when the
  owner is destroyed or moved-from. Used for `future_state<T>` (shared by `promise`/`future`) and for every queued
  coroutine resume (`task<T>` starts, sleeps, future continuations, `run_until_complete`), so the loop skips a resume
  whose frame was destroyed while it was queued. A reversed variant for the queued resumes, `guarded<T>`, is planned
  (`docs/internals-simplifications.md`).
- `details::utils::outcome<T>` — the set-once result slot (`variant<monostate, T, exception_ptr, moved_out_t>`) behind
  both `coro<T>` promises and `future_state<T>`. `get_result()` returns `T` and marks the slot as taken (`moved_out_t`,
  distinct from `monostate`, which means never set), so a result can only be taken once: a second call reports "value
  already taken". `is_ready()` means a result is there to take; `is_settled()` means one was set, taken or not, and is
  what `set_result()`/`set_exception()` check before throwing `value_is_set`. A coroutine's `unhandled_exception()` uses the unchecked
  `replace_with_exception()` instead, because a local's destructor can throw after `co_return`.
- Scheduler components that aren't generic use a type-erased, compiled-core pattern rather than being templated — keep
  this pattern for new scheduler code unless there's a specific reason to templatize.

## Known gotchas

- `coro<T>` previously had a double-await UB bug. The fix requires coordinating `std::exchange` with an
  `utils::finally`-guarded `.destroy()` call inside the awaitable — if you touch await/destroy logic in `coro<T>`,
  re-check this invariant carefully rather than assuming a simpler fix is safe.
- Coverage tooling (llvm-cov + SonarQube) has known false positives on branch merging for template instantiations —
  this is an upstream LLVM issue (llvm/llvm-project#93843, #111743, #119299), not a bug in this codebase. Don't "fix"
  coverage gaps that trace back to this.
- `promise<T>`'s destructor schedules a resume via `loop::try_current()` (not `loop::current()`) specifically because
  it must stay `noexcept`-safe — if you touch that destructor, don't switch it back to the throwing lookup.
  Double-scheduling a resume (UB) is guarded by `future_state<T>::schedule_continuation()` clearing `continuation`
  the instant a resume is scheduled, not by checking `is_ready()` — if you change how `continuation` is consumed, make
  sure whatever claims it still clears it in the same step, or a promise resolved and destroyed in quick succession
  will double-schedule its awaiter's resume. It clears it only after `call_soon()` succeeded: if queuing throws, the
  awaiter stays registered so the promise's destructor can still deliver the result, rather than being lost.
- Every loop queue entry that resumes a coroutine which can be cancelled (task frames) must be a `weak_view`, not a
  raw `std::coroutine_handle<>`: destroying a `task<T>` destroys its frame at whatever suspend point it's at, and a
  raw handle queued before that would resume freed memory. `future<T>`'s awaitable and `sleep_awaitable` both own a
  `trackable<std::coroutine_handle<>>` for this reason.
- Exception policy: `loop::call_soon` only accepts coroutine entries or nothrow-invocable callbacks
  (`details::loop_handle`/`details::loop_callback`). Errors travel through coroutines and promises, never out of a
  callback: nobody awaits a callback, so a throw would unwind an unrelated `run_until_complete()`. The check is
  compile-time only. Don't add `noexcept` to `loop_entry_executor`: it cost ~1.4 ns per callback for no extra safety.
  Callbacks are stored as `details::utils::move_only_callback`, not `std::function`, so they can own move-only state:
  the loop can't see what a callback references, and one queued by a coroutine may run after that coroutine is gone,
  so callbacks should own what they touch (e.g. capture a promise by move) rather than capture frame locals by `&`.
  `run_until_complete` still queues its coroutine as a `weak_view`, because allocation failure inside the loop can
  still unwind it.
- Capturing coroutine lambdas: a lambda's captures live in its closure object, not in the coroutine frame, so a
  temporary closure dies at the end of the statement that called it. `run_until_complete([&]() -> coro<> {...}())` is
  safe, because the statement only ends after the run. `create_task([&]() -> coro<> {...}())` is not: the task runs
  after the statement, and its body reads captures from a dead closure (ASan reports stack-use-after-return). Name the
  lambda so it outlives the task, or pass what the body needs as coroutine parameters. clang-tidy's
  `cppcoreguidelines-avoid-capturing-lambda-coroutines` is on for library code but off in `tests/.clang-tidy`: it
  flags every capturing coroutine lambda, including the safe `run_until_complete` form most tests use, so tests rely
  on ASan to catch the unsafe ones.

## CI

- GitHub Actions, split into separate jobs with least-privilege permissions per job (`tests`, `coverage`,
  `benchmark-run`, `benchmark-report`, `benchmark-publish`) — don't collapse jobs back into one broad-permission
  workflow.
- Benchmarks run via `benchmark-action/github-action-benchmark`; results published to `gh-pages` only on push to
  `main`, posted as a PR comment otherwise.
- `codspeed.yml` runs the Google Benchmark suite through CodSpeed (`simulation` and `walltime` modes, via a matrix),
  reporting with `CodSpeedHQ/action`, on push to `main`, on PRs, and on `workflow_dispatch`. This is a separate
  pipeline from the `benchmark-run`/`benchmark-report`/`benchmark-publish` jobs above.
- Coverage: llvm-cov → SonarQube (`sonar.cfamily.compile-commands` fed from `build/compile_commands.json`).

## Code style

- Formatted with `clang-format` (WebKit-based, 4-space indent, 140-column limit); `clangd` configured with inlay
  hints enabled.
- Header guard: `#pragma once` everywhere, not `#ifndef` guards.
- Include ordering is enforced by clang-format (`IncludeBlocks: Regroup`): quoted includes first, then
  `<doctest|nanobench>`, then other extension-bearing angle includes, then plain system headers
  (`SortIncludes: CaseSensitive`).
- Member variables use an `m_` prefix; types and functions are `snake_case` (see `promise_base`, `set_result_impl`).

## Working with me on this project

- RAII over raw pointers/manual lifetime management, as elsewhere.
- This library is also the subject of a public decision-journal blog series — if asked to help draft a post about a
  change here, use the problem → options → tradeoffs → choice → consequences structure.
- Never `git commit` / `git push` on your own.
