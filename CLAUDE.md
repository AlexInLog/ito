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
- `examples/` — small programs (`ITO_BUILD_EXAMPLES`, on in the presets that build tests), registered as ctest tests
  that check their own outcome. `examples/.clang-tidy` allows reference coroutine parameters: the examples pass
  state owned by the awaiting coroutine.
- Namespaces:
  - `ito` — public API (`coro<T>`, `loop`, `task<T>`).
  - `ito::async` — async primitives layered on top of `coro<T>`, e.g. `promise<T>`/`future<T>` (settable from outside
    a coroutine / awaitable).
  - `ito::details`, `ito::details::utils` — implementation internals, mirroring `include/ito/details/utils/`.
  - `ito::exceptions` — exception types thrown across the library (e.g. `broken_future`).

## Core components

- `coro<T>` — the base coroutine type: lazily-started, single-owner (`initial_suspend` = always suspend).
- `promise<T>`/`future<T>` — `std::promise`/`std::future`-style split, created together via `promise<T>::create()`.
  Both sides are backed by a shared `trackable<future_state<T>>`/`weak_ref` pair (see
  `details::utils::trackable`), so either side can be destroyed independently without dangling the other:
  destroying the `future` while a coroutine hasn't yet awaited it is a no-op; destroying the `promise` while a
  coroutine is suspended `co_await`ing its `future` delivers `exceptions::broken_future` to that coroutine instead
  of leaving it suspended forever. `future<T>::operator co_await() &&` is rvalue-qualified, so single-consumption is
  enforced by the type system (move-only) rather than a runtime flag.
- `task<T>` — created via `loop::create_task(coro<T>&&)`; the loop queues it to start running on the next tick,
  independent of whether/when it's `co_await`ed. Destroying the `task<T>` before it's awaited
  cancels it (the underlying coroutine handle is destroyed, so the body may never run past its next suspend point).
- `details::utils::trackable<T>` — pairs an owning object with a single `weak_ref` that gets nulled out when the
  owner is destroyed or moved-from. Used for `future_state<T>` (shared by `promise`/`future`).
- `trackable` is built on `details::utils::linked<Tag, Side>` (`link_owner`/`link_follower`): an intrusive two-ended link
  where each kind of link has its own tag. The two ends and different kinds of links are distinct types on purpose, so
  clang's type-based alias analysis can keep the pointers in registers; one symmetric type cost ~3x the instructions on
  the create/move/use path (see the comment in `linked.hpp`).
- `details::utils::intrusive_queue<T>` — a circular doubly-linked list with a single sentinel. An unlinked node, like
  an empty queue's sentinel, points at itself, so linking and unlinking never branch. A node unlinks itself in its
  destructor, so whatever the loop reaches through a queue is alive. `consume()` runs only the nodes queued when it
  started, using a marker node on the stack; that's the loop's tick. `push_back_sorted()` keeps a sorted queue
  sorted by scanning from the back (O(1) for nodes arriving in order).
- `details::intrusive_ready_node` — an entry in the loop's ready queue, with `virtual void run() noexcept`. Every
  `coro<T>` promise is one (its `run()` resumes the frame), and so is `scheduled_callback`. A frame is only ever
  queued through its own promise's node, at most once at a time.
- `scheduled_callback` — returned by `loop::schedule(callback)`. It owns the callback and its queue node inline;
  destroying it before the callback runs cancels the callback. Before running, it moves the callback out, so the
  captures are released right after the run and a callback may destroy its own `scheduled_callback`.
- `details::timer_node` — one pending sleep, living in the sleep's awaitable. It holds the deadline and a pointer to
  the sleeping frame's promise; the loop keeps timers in an `intrusive_queue<timer_node>` sorted by deadline, and
  moves a due timer's frame onto the ready queue.
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
  will double-schedule its awaiter's resume. `continuation` is a plain `intrusive_ready_node*` (null when unset), and
  `schedule_continuation()` takes it with `std::exchange(continuation, nullptr)` as it queues the frame. Queuing
  only links a node, so it can't fail. Callers check `continuation` before calling, and this path is
  inlining-sensitive (see `docs/ideas.md`). The pointer is null in two states, "nobody awaits yet" and "already
  queued"; the outcome tells them apart, and the promise has nothing to do in either.
- A pointer from one queue node to another (a future's `continuation`, a timer's `frame`) is only allowed when the
  target outlives the holder. Today both live in the same coroutine frame: the pointer sits in an awaitable,
  the target is that frame's promise, and a frame destroys its awaitables before its promise. Don't store such a
  node anywhere outside the frame it points into (the old timer heap did, and needed `anchored` for it). Queues and
  frames are free to die in any order, because every node unlinks itself on destruction.
- `future<T>`'s and the sleep's awaitables take a typed `std::coroutine_handle<Promise>` with `Promise` derived from
  `details::base_promise_type`: they queue the awaiting frame's own node, which only ito coroutines have. That's why
  they're named nested classes rather than classes local to `operator co_await()`: a local class can't have member
  templates.
- Exception policy: `loop::schedule` only accepts nothrow-invocable callbacks (`details::loop_callback`). Errors
  travel through coroutines and promises, never out of a callback: nobody awaits a callback, so a throw would unwind
  an unrelated `run_until_complete()`. The check is compile-time only. Callbacks are stored as
  `details::utils::move_only_callback`, not `std::function`, so they can own move-only state. A callback is
  cancelled together with whatever owns its `scheduled_callback`, so it may reference that owner's state; the
  contract to keep is that everything it touches outlives its `scheduled_callback`.
- The loop's tick (`consume()` in `run_until_complete_impl`) stops as soon as the coroutine passed to
  `run_until_complete` is done; the rest of the tick stays queued for a later run.
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
