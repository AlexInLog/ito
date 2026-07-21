# ito

C++20/23 coroutine-based async runtime library. MIT licensed, header-only.
Repo: https://github.com/AlexInLog/ito

## Build and test

- Build system: CMake (presets, `Ninja` generator) + Conan (fetched automatically via `cmake-conan` on first configure).
- Tests: Catch2 + trompeloeil.
- Configure presets: `debug`, `release`, `lint` (debug + clang-tidy + cppcheck), `coverage`, and sanitizer presets
  `sanitize-asan`, `sanitize-tsan`, `sanitize-lsan`, `sanitize-ubsan`, `sanitize-msan`.
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
  - `include/ito/async/` — async primitives (currently `future.hpp`).
- Namespaces:
  - `ito` — public API (`coro<T>`, `loop`).
  - `ito::async` — async primitives layered on top of `coro<T>`, e.g. `future<T>` (awaitable, settable from outside a coroutine).
  - `ito::details`, `ito::details::utils` — implementation internals, mirroring `include/ito/details/utils/`.
  - `ito::exceptions` — exception types thrown across the library (e.g. `future_just_awaited`).

## Core components

- `coro<T>` — the base coroutine type: lazily-started, single-owner (`initial_suspend` = always suspend).
- `future<T>` — uses CRTP-based `set_value` specialization to handle `void` vs non-`void` results without duplicating
  the interface.
- `task<T>` — a `spawn`-based wrapper around `coro<T>` with cancel-on-destroy semantics.
- `paired_object<T>` — utility for mutual back-pointers between two objects.
- Scheduler components that aren't generic use a type-erased, compiled-core pattern rather than being templated — keep
  this pattern for new scheduler code unless there's a specific reason to templatize.

## Known gotchas

- `coro<T>` previously had a double-await UB bug. The fix requires coordinating `std::exchange` with an
  `utils::finally`-guarded `.destroy()` call inside the awaitable — if you touch await/destroy logic in `coro<T>`,
  re-check this invariant carefully rather than assuming a simpler fix is safe.
- Coverage tooling (llvm-cov + SonarQube) has known false positives on branch merging for template instantiations —
  this is an upstream LLVM issue (llvm/llvm-project#93843, #111743, #119299), not a bug in this codebase. Don't "fix"
  coverage gaps that trace back to this.
- `.clang-format` sets `UseCRLF: true` — this repo uses CRLF line endings despite being developed on Linux. Don't
  "normalize" line endings to LF.

## CI

- GitHub Actions, split into separate jobs with least-privilege permissions per job (`tests`, `coverage`,
  `benchmark-run`, `benchmark-report`, `benchmark-publish`) — don't collapse jobs back into one broad-permission
  workflow.
- Benchmarks run via `benchmark-action/github-action-benchmark`; results published to `gh-pages` only on push to
  `main`, posted as a PR comment otherwise.
- Coverage: llvm-cov → SonarQube (`sonar.cfamily.compile-commands` fed from `build/compile_commands.json`).

## Code style

- Formatted with `clang-format` (WebKit-based, 4-space indent, 140-column limit); `clangd` configured with inlay
  hints enabled.
- Header guard: `#pragma once` everywhere, not `#ifndef` guards.
- Include ordering is enforced by clang-format (`IncludeBlocks: Regroup`): quoted includes first, then
  `<doctest|nanobench>`, then other extension-bearing angle includes, then plain system headers
  (`SortIncludes: CaseSensitive`).
- Member variables use an `m_` prefix; types and functions are `snake_case` (see `future_base`, `set_result_impl`).

## Working with me on this project

- RAII over raw pointers/manual lifetime management, as elsewhere.
- This library is also the subject of a public decision-journal blog series — if asked to help draft a post about a
  change here, use the problem → options → tradeoffs → choice → consequences structure.
- Never `git commit` / `git push` on your own.
