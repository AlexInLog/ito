# Ideas and open issues

Working notes on what to build next and what's still known to be rough. The [README roadmap](../README.md#roadmap)
is the short public checklist; this file keeps the reasoning behind it.

## Open issues

- **Inserting a timer is O(n) at worst:** timers live in an intrusive list sorted by deadline, and an insert scans
  from the back. Sleeps of the same length arrive in deadline order and land at the tail in O(1); many timers with
  mixed deadlines pay a scan each. If that shows up, the candidates are a pairing heap behind the same `timer_node`,
  or bucketing deadlines into a timer wheel. A wheel trades exact deadlines for a fixed resolution, so it belongs with
  the reactor, where thousands of socket timeouts make that trade worth it.
- **No benchmark with active timers:** the loop calls `steady_clock::now()` once per tick only when timers exist,
  and none of the current benchmarks create one, so that path is unmeasured.
- **`linked` (behind `trackable`) and the intrusive queues are single-threaded by design:** non-atomic back-pointers, re-pointed
  on every move. Anything multi-threaded will need a different lifetime-tracking scheme (see the multi-threading step
  below).
- **The promise's resolve path sits just under clang's inlining threshold:** `promise_base::set_result_impl` has an
  inline cost of 245 against a threshold of 250. If it goes over, it stops being inlined and the future benchmarks
  jump (resolving before await went from 211 to 297 instructions, +4.9 ns, during the `anchored` migration). Those
  numbers predate the intrusive queue, which made the continuation a raw node pointer and queuing it `noexcept`;
  the threshold hasn't been re-checked since. The throwing `loop::current()` lookup was the biggest single cost in
  there. When changing that path, check
  `-Rpass-missed=inline` for `set_result_impl` and compare instruction counts (`perf stat -e instructions:u`).
- **Destroying a task from inside its own body** isn't guarded: the frame would be destroyed while it's running.
- **A scheduled callback can still dangle if its handle outlives what it references:** destroying a
  `scheduled_callback` cancels the callback, so one held as a coroutine's local can safely reference that
  coroutine's locals. Moving the handle somewhere longer-lived (a member, an `optional` outside the coroutine) brings
  back the old risk. The loop can't see what a callable references, so this is a documented contract.
- **Unobserved task exceptions are dropped silently:** a task that fails and is destroyed without being awaited
  loses its exception. That's documented (README, `task.hpp`) and pinned by a test in `test_task.cpp`. asyncio logs
  these; a loop-level handler called when a failed task is destroyed unawaited would be the equivalent here.
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

Rough order. Cancellation comes first because its answer shapes most of the others. The intrusive ready queue,
which used to be step one, is done: frames, callbacks and timers are all intrusive nodes now, and `anchored` is gone.

1. **Cooperative cancellation and structured concurrency:** today, cancelling means destroying the frame. That stops
   working once I/O arrives: with io_uring the kernel holds pointers into your buffers until the CQE lands, so a
   frame with a read in flight can't just be destroyed. It needs a stop signal the operation observes, an
   `ASYNC_CANCEL` submission, and a wait for completion. On top of that: `when_all` (tuple results, exception
   aggregation), `when_any` (cancelling the losers), task groups, and timeouts.
2. **Single-threaded sync primitives:** `event`, `mutex`, `semaphore`, `channel<T>`. Each is small, and each pushes
   towards intrusive waiter lists, where the node lives in the awaiter and unlinks itself on cancellation. The same
   intrusive node design as the ready queue applies to their waiter lists.
3. **Allocation work:** a per-loop frame pool via `promise_type::operator new`, measured with the existing CodSpeed
   setup.
4. **Reactor:** epoll first as a baseline, then io_uring. Timers become the `epoll_wait` timeout or
   `IORING_OP_TIMEOUT` and replace `std::this_thread::sleep_until`.
5. **Multi-threading:** a thread-safe `schedule` with an eventfd wakeup, then a Chase-Lev work-stealing scheduler.
   This is where the intrusive queues and `linked` (behind `trackable`) have to be redesigned: a node unlinks itself
   from whatever queue it's in when its frame dies, so a task destroyed on one thread while another thread pops its
   node would race. The likely replacement for queued frames is a strong ref in the queue plus a cancelled bit and a
   refcount in the frame header: the popper sees the bit and destroys the frame without resuming it. The cost is that
   cancelled frames live until the scheduler reaches them, and their locals are destroyed on the scheduler's thread.
   `future_state` would move to atomic shared state separately. Another reason to settle cancellation first.

Smaller side projects: `async_generator<T>`, `coro<T&>` support, interop with P2300 `std::execution` senders.
