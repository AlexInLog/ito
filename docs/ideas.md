# Ideas and open issues

Working notes on what to build next and what's still known to be rough. The [README roadmap](../README.md#roadmap)
is the short public checklist; this file keeps the reasoning behind it.

## Open issues

- **Cancelled timers deeper in the heap:** the loop drops cancelled entries from the top of the timer heap before
  it sleeps, so it never waits on a dead deadline. Entries further down keep their memory until they surface.
  Removing them on cancel needs each sleep to know its heap position, which an intrusive timer node would give
  (see below).
- **Timer heap entries are `anchored` handles:** every sift during `push_heap`/`pop_heap` moves an entry, and each
  move writes a back-pointer into the anchor in another coroutine's frame. Fine at today's scale, but it's a cache
  miss per swap.
- **No benchmark with active timers:** the loop calls `steady_clock::now()` once per tick only when timers exist,
  and none of the current benchmarks create one, so that path is unmeasured.
- **`linked` (behind `trackable` and `anchored`) is single-threaded by design:** non-atomic back-pointers, re-pointed
  on every move. Anything multi-threaded will need a different lifetime-tracking scheme (see the multi-threading step
  below).
- **The promise's resolve path sits just under clang's inlining threshold:** `promise_base::set_result_impl` has an
  inline cost of 245 against a threshold of 250. If it goes over, it stops being inlined and the future benchmarks
  jump (resolving before await went from 211 to 297 instructions, +4.9 ns, during the `anchored` migration). It was
  brought back under by dropping a redundant continuation check and by making the continuation a plain
  `queued_resume` (empty when unset), so queuing it needs no `reset()` of a moved-from optional; the throwing
  `loop::current()` lookup is the biggest single cost in there. When changing that path, check
  `-Rpass-missed=inline` for `set_result_impl` and compare instruction counts (`perf stat -e instructions:u`).
- **Destroying a task from inside its own body** isn't guarded: the frame would be destroyed while it's running.
- **Callbacks that capture by reference can dangle:** a `call_soon` callback may run after the coroutine that queued
  it has finished, later in the same run or in the next one. The loop can't see what a callable references, so this
  is a documented contract rather than a check. Callbacks are stored as `move_only_callback`, so the safe form
  (owning what they touch, e.g. a promise captured by move) is always possible.
- **Unobserved task exceptions are dropped silently:** a task that fails and is destroyed without being awaited
  loses its exception. That's documented (README, `task.hpp`) and pinned by a test in `test_task.cpp`. asyncio logs
  these; a loop-level handler called when a failed task is destroyed unawaited would be the equivalent here.
- **`bad_alloc` in a promise's destructor can still lose its awaiter:** when resolving a promise fails to queue the
  awaiter's resume, the awaiter stays registered and the promise's destructor queues it later. If that queuing fails
  too, the destructor has to swallow the exception (it's `noexcept`) and nothing is left to resume the awaiter.
- **Baseline benchmarks move with code layout:** `benchmark_baseline.cpp` links into the same binary as the ito
  benchmarks, so ito changes shift its unchanged code and CodSpeed reports regressions of up to about 20% on
  `bm_small_std_function`. Building the baselines as their own executable would pin their layout; not planned for
  now, so treat changes in the baseline benchmarks as layout noise unless their own code changed.
- **`coro<T&>` doesn't compile**, because `outcome` stores `T` in a `std::variant`.
- **SonarCloud `cpp:S836` false positive on `outcome<T>::get_result()`:** when a test moves a `future<int>` before
  awaiting it, the analyzer loses track of the result variant through `trackable`'s defaulted move and reports an
  uninitialized `int` returned. Index 1 is only ever set by `emplace<1>`, so it can't happen. Marking it in the
  SonarCloud UI doesn't stick, because Sonar tracks issues by location and this code has moved before, so
  `sonar-project.properties` excludes this rule for `outcome.hpp` only. If
  that code moves to another file, move the exclusion with it. clang-analyzer's `core.uninitialized.UndefReturn`
  reports the same path and fails the lint build since `outcome` gained its taken state, so that line also carries a
  `NOLINT` for it.

## Next steps

Rough order. The intrusive ready queue comes first because it reshapes how the loop holds suspended coroutines;
cancellation comes next because its answer shapes most of the others.

1. **Intrusive ready queue:** each suspended awaiter is its own queue node, a doubly-linked node embedded in the
   awaitable (so in the coroutine frame). Queuing links the node; cancelling is the node unlinking itself in its
   destructor, in O(1). That removes `anchored`/`anchor` for queued resumes (the node's lifetime is the frame's
   lifetime), the `deque`, and the per-entry allocation. Callbacks need a heap node holding their
   `move_only_callback`. Timers want the same treatment (an intrusive heap or a timer wheel), which also fixes the
   cancelled-timers-deeper-in-the-heap issue. Open design points: who owns callback nodes, keeping the per-tick
   batching, and how it carries over to multi-threading.
2. **Cooperative cancellation and structured concurrency:** today, cancelling means destroying the frame. That stops
   working once I/O arrives: with io_uring the kernel holds pointers into your buffers until the CQE lands, so a
   frame with a read in flight can't just be destroyed. It needs a stop signal the operation observes, an
   `ASYNC_CANCEL` submission, and a wait for completion. On top of that: `when_all` (tuple results, exception
   aggregation), `when_any` (cancelling the losers), task groups, and timeouts.
3. **Single-threaded sync primitives:** `event`, `mutex`, `semaphore`, `channel<T>`. Each is small, and each pushes
   towards intrusive waiter lists, where the node lives in the awaiter and unlinks itself on cancellation. The same
   intrusive node design as the ready queue applies to their waiter lists.
4. **Allocation work:** a per-loop frame pool via `promise_type::operator new`, measured with the existing CodSpeed
   setup.
5. **Reactor:** epoll first as a baseline, then io_uring. Timers become the `epoll_wait` timeout or
   `IORING_OP_TIMEOUT` and replace `std::this_thread::sleep_until`.
6. **Multi-threading:** `call_soon_threadsafe` with an eventfd wakeup, then a Chase-Lev work-stealing scheduler.
   This is where `linked` (behind `trackable` and `anchored`) has to be redesigned: its moves patch a non-atomic
   back-pointer on the other end, so an anchor in a task dropped on one thread and its queued handle stolen by another
   would race. The likely replacement for queued resumes is a strong ref in the queue plus a cancelled bit and a
   refcount in the frame header: the popper sees the bit and destroys the frame without resuming it. The cost is that
   cancelled frames live until the scheduler reaches them, and their locals are destroyed on the scheduler's thread.
   `future_state` would move to atomic shared state separately. Another reason to settle cancellation first.

Smaller side projects: `async_generator<T>`, `coro<T&>` support, interop with P2300 `std::execution` senders.
