# Ideas and open issues

Working notes on what to build next and what's still known to be rough. The [README roadmap](../README.md#roadmap)
is the short public checklist; this file keeps the reasoning behind it. Planned refactors of the internals are in
[internals-simplifications.md](internals-simplifications.md).

## Open issues

- **`future<T>` await cost:** making future continuations cancellation-aware (the awaitable owns a
  `trackable<std::coroutine_handle<>>`, the queue gets its `weak_view`) costs about 3 ns on the path that never
  suspends (`bm_resolve_future_before_await`, 11.9 to 15.0 ns), which it shouldn't, and under 1 ns on the
  suspending path (`bm_resolve_future_inside_signal`). The cause isn't known yet. One guess: the awaitable and
  `future_state` are no longer trivially destructible, which changes the cleanup path or what gets inlined.
  Defaulting `~awaitable()` made no difference. Next step is reading the codegen.
- **Cancelled timers deeper in the heap:** the loop drops cancelled entries from the top of the timer heap before
  it sleeps, so it never waits on a dead deadline. Entries further down keep their memory until they surface.
  Removing them on cancel needs each sleep to know its heap position, which an intrusive timer node would give
  (see below).
- **Timer heap entries are `weak_view`s:** every sift during `push_heap`/`pop_heap` moves a `weak_view`, and each
  move writes a back-pointer into another coroutine's frame. Fine at today's scale, but it's a cache miss per
  swap.
- **No benchmark with active timers:** the loop calls `steady_clock::now()` once per tick only when timers exist,
  and none of the current benchmarks create one, so that path is unmeasured.
- **`trackable` is single-threaded by design:** one non-atomic back-pointer, re-pointed on every move. Anything
  multi-threaded will need a different lifetime-tracking scheme.
- **Destroying a task from inside its own body** isn't guarded: the frame would be destroyed while it's running.
- **Callbacks that capture by reference can dangle:** a `call_soon` callback may run after the coroutine that queued
  it has finished, later in the same run or in the next one. The loop can't see what a callable references, so this
  is a documented contract rather than a check. Callbacks are stored as `move_only_callback`, so the safe form
  (owning what they touch, e.g. a promise captured by move) is always possible.
- **Unobserved task exceptions are dropped silently:** a task that fails and is destroyed without being awaited
  loses its exception. asyncio logs these; here there isn't even a debug hook.
- **`bad_alloc` while moving due timers into the queue loses a sleeper:** `pop_earliest()` has already moved the
  view out when the queue's `emplace_back` throws, so that coroutine is never resumed. It's the one loop-internal
  path that loses work rather than just failing.
- **Baseline benchmarks move with code layout:** `benchmark_baseline.cpp` links into the same binary as the ito
  benchmarks, so ito changes shift its unchanged code and CodSpeed reports regressions of up to about 20% on
  `bm_small_std_function`. Building the baselines as their own executable would pin their layout.
- **`coro<T&>` doesn't compile**, because `error_or_optional` stores `T` in a `std::variant`.

## Next steps

Rough order. The first item comes first because its answer shapes most of the others.

1. **Cooperative cancellation and structured concurrency:** today, cancelling means destroying the frame. That stops
   working once I/O arrives: with io_uring the kernel holds pointers into your buffers until the CQE lands, so a
   frame with a read in flight can't just be destroyed. It needs a stop signal the operation observes, an
   `ASYNC_CANCEL` submission, and a wait for completion. On top of that: `when_all` (tuple results, exception
   aggregation), `when_any` (cancelling the losers), task groups, and timeouts.
2. **Single-threaded sync primitives:** `event`, `mutex`, `semaphore`, `channel<T>`. Each is small, and each pushes
   towards intrusive waiter lists, where the node lives in the awaiter and unlinks itself on cancellation. The same
   design could replace the `deque<variant<..., move_only_callback>>` ready queue and the `trackable`-based timer heap,
   and would likely address the first three open issues.
3. **Allocation work:** a per-loop frame pool via `promise_type::operator new` and an intrusive ready queue,
   measured with the existing CodSpeed setup.
4. **Reactor:** epoll first as a baseline, then io_uring. Timers become the `epoll_wait` timeout or
   `IORING_OP_TIMEOUT` and replace `std::this_thread::sleep_until`.
5. **Multi-threading:** `call_soon_threadsafe` with an eventfd wakeup, then a Chase-Lev work-stealing scheduler.
   This is where `trackable` has to be redesigned, which is another reason to settle cancellation first.

Smaller side projects: `async_generator<T>`, `coro<T&>` support, interop with P2300 `std::execution` senders.
