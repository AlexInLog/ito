# Intrusive ready queue: review notes

Findings from reviewing the first cut of `details/utils/intrusive_queue.hpp` and its use in the loop (branch
`use_intrusive`, on top of `465af1c`). The list itself is correct: a node destroyed while queued, the queue destroyed
before its nodes, and a node destroyed during a drain all behave under ASan. What's left is wiring and invariants.

Suggested order: the `ready_node` layer (unblocks `call_soon`), then the tick batch with splice-back and its test, then
futures with a typed `await_suspend` and a weak node reference. The `ready_node` layer and RAII callbacks are done (see
[Callbacks](#callbacks-scheduled_callback)).

## Wiring

1. **Done.** **Queue element type:** `intrusive_queue<base_promise_type>` can't hold callbacks, which is what blocked
   `call_soon`. The queue now holds `details::ready_node`, an `intrusive_queue_node` with a pure `virtual void run()
   noexcept`. The concrete `coro<T>::promise_type` overrides it as
   `coroutine_handle<promise_type>::from_promise(*this).resume()`, and `scheduled_callback` overrides it to run its
   callback. Doing this in the base with `coroutine_handle<base_promise_type>::from_promise(base)` would be UB: a base
   subobject isn't the coroutine's promise object.

   A function-pointer version (`run` stored in the node, the handle stored in the promise) was tried first. It's 8 bytes
   bigger per frame and measured the same: instruction counts within 2% either way, and walltime a wash when the two
   builds run back to back. Each resume is two indirect calls in both versions (`run`, then `resume`). Nothing is
   destroyed through these bases, so the destructors are protected and non-virtual; clang-tidy then wants the rule of
   five spelled out on each intermediate promise class.

2. **Pushing a node that's already queued moves it to the back:** Push `a`, `b`, then `a` again, and the queue pops
   `b, a`. That hides the double-scheduling bug the design should rule out. Assert that the node isn't linked (an
   `is_linked()` private to the queue), or make it a deliberate no-op, but don't reorder.

3. **No tick batch in the drain:** Popping and resuming one entry at a time breaks the README's guarantee that a tick
   runs exactly what was queued when it started, and a coroutine that keeps rescheduling itself starves timers. The
   queue's move constructor is already the splice: `auto batch = std::move(m_queue);`, then drain `batch`. Nodes
   destroyed during the drain unlink from `batch` without knowing which list they're in.

   The catch: the drain stops early once `h.done()`. The old loop left the remaining entries in `m_queue` for the next
   run. With a batch, they'd be unlinked when `batch` is destroyed, and those tasks would never run. Splice what's left
   back to the front of `m_queue` on every exit, including unwinding. Write that test first.

## Node in the promise

One node per frame, embedded in the promise, matches what a suspended frame is (it waits on exactly one thing) and
costs no allocation. What it means for the next stages:

- **Stage 3 has to keep the "clear on schedule" rule:** this isn't a bug today; `future.hpp` still empties
  `continuation` in the same step that queues it. It becomes one only if the migration to frame nodes decides whether
  to schedule by asking "is the node linked?". The ordinary sequence (resolve, the awaiter resumes and suspends
  somewhere else, then the promise is destroyed) would then queue the frame at that later, unrelated suspension point
  and resume the wrong code, with no crash. A node in an awaitable couldn't do this, because it died with its
  full-expression; a node in the promise lives as long as the frame. So the future's reference to the node stays weak,
  is cleared the moment it's scheduled, and is cleared in the awaitable's destructor, exactly as today. Pin it with a
  test: resolve, let the awaiter suspend on something else, then destroy the promise.
- **Awaitables need a typed handle:** finding the node means `await_suspend` takes `std::coroutine_handle<P>` with `P`
  derived from `base_promise_type`, so only ito coroutines can await ito futures and sleeps. An API decision; asio
  makes the same one.
- **Every frame grows by 16–24 bytes,** including child `coro`s that are never queued. Minor; `bm_nested_coro_chain`
  shows it.
- **A frame can wait on only one waker at a time:** registering the same frame with two wakers would double-link its
  node. So `when_any` (step 2 in `ideas.md`) has to be built from child frames, or a combinator with its own node, that
  resume the parent exactly once, rather than by registering the parent with a future and a timer at once.

Timers stay on `anchored` for now: heap entries move during sifts, so an intrusive node can't unlink itself from the
heap without tracking its index.

## Callbacks: `scheduled_callback`

Decided: `call_soon` returns a `[[nodiscard]] ito::scheduled_callback` that owns its `ready_node` and its
`move_only_callback` inline, the same ownership model as `task`. The loop owns nothing; it only links nodes.

- Queuing a callback allocates nothing (only a callable too big for `move_only_callback`'s inline storage does), so no
  callback pool is needed.
- Destroying the handle cancels the callback. A callback can't outlive whatever holds its handle, so when a coroutine
  holds it, capturing the coroutine's locals by `&` is safe.
- The handle may outlive the loop: the loop's queue unlinks it on destruction, and the callback never runs.
- Before running, the callable is moved out of the handle into a local. Captures are released as soon as the callback
  returns (the old behaviour), and a callback may destroy its own handle. This costs about 2 ns per callback
  (`bm_call_soon_burst/1024`: 10.4 µs without the move, 12.2 µs with it).
- Fire-and-forget is gone. A callback queued from inside another callback, or one that has to outlive the coroutine
  that queued it, needs its handle stored somewhere that lives long enough; the tests use an `std::optional` or a
  `std::vector` held outside. If a fire-and-forget case turns up (bridging from a C callback API, say), add a separate
  allocating function for it rather than changing this default.
- The name still says `call_soon`, which suggests asyncio's fire-and-forget. Renaming it (`schedule`, say) is a
  mechanical follow-up.

The promise side: `base_promise_type` is a `ready_node`; the concrete promise's `run()` resumes its frame (item 1 above).

Measured on this branch (pinned core, mean of 3; the drain isn't batched yet, so treat these as rough):

| Benchmark | `main` (deque) | branch |
|---|---|---|
| `bm_call_soon_before_noop_coro` | 19.2 ns | 17.4 ns |
| `bm_call_soon_burst/16` | 108 ns | 247 ns |
| `bm_call_soon_burst/1024` | 6.4 µs | 12.2 µs |
| `bm_fan_out_tasks/1024` | 29.0 µs | 22.3 µs |
| `bm_cancel_tasks_before_start/1024` | 27.6 µs | 17.9 µs |

The burst regression is partly the benchmark itself: it now stores N handles in a `vector`, so each callback also pays
a move into the vector (a relink plus `move_only_callback`'s indirect move), on top of the move-out before running.

## Futures and timers on the new queue

Done, everything except the tick batch, so all tests and benchmarks are back:

- A future's state holds the awaiting frame's `ready_node*`. The state lives in the awaitable, in that same frame, so
  the pointer can't outlive the frame. Scheduling goes through `details::loop_internals::schedule()` and clears the
  pointer in the same step, the rule from [Node in the promise](#node-in-the-promise). Queuing can no longer fail, so
  the promise destructor's try/catch around it is gone.
- The timer heap keeps `anchored`, now `anchored<ready_node*>`: a due timer pushes the sleeping frame's node, and a
  destroyed frame leaves an entry whose `get()` is null.
- The future's and the sleep's `await_suspend` take `std::coroutine_handle<P>` with `P` derived from
  `base_promise_type`. Both awaitables became named member classes, because a class local to `operator co_await()`
  can't have a member template.
- `test_allocation_failure.cpp`: the two old cases made the ready queue's growth allocation fail, which no longer
  exists. They're replaced by the two allocations that remain: registering a timer (the heap's `vector`; the sleeping
  coroutine gets `bad_alloc` at its `co_await`) and storing a callback too big for inline storage (`call_soon` throws
  and queues nothing).
- The timer-ordering test needs the batch, so it's its own test case tagged `[!shouldfail]`. Remove the tag when the
  batch lands; Catch2 fails the test once it passes.

`main` against this branch (pinned core, mean of 3, same machine):

| Benchmark | `main` | branch |
|---|---|---|
| `bm_creation_of_coro` | 7.1 ns | 7.6 ns |
| `bm_call_noop_coro` | 11.4 ns | 11.8 ns |
| `bm_call_soon_before_noop_coro` | 19.3 ns | 20.0 ns |
| `bm_resolve_future_before_await` | 11.7 ns | 12.0 ns |
| `bm_resolve_future_inside_signal` | 29.7 ns | 25.6 ns |
| `bm_start_task_inside_coro_and_await` | 29.4 ns | 24.8 ns |
| `bm_start_two_tasks_inside_coro_and_await_second` | 43.5 ns | 30.7 ns |
| `bm_fan_out_tasks/1024` | 29.2 µs | 22.6 µs |
| `bm_ping_pong_tasks/1024` | 26.7 µs | 19.1 µs |
| `bm_cancel_tasks_before_start/1024` | 26.0 µs | 17.1 µs |
| `bm_call_soon_burst/1024` | 6.2 µs | 12.8 µs |
| `bm_nested_coro_chain/16` | 214 ns | 223 ns |

Anything that goes through the queue is faster; the small losses on frame creation and the nested chain are the
bigger promise (vptr and the node's two links). The callback burst is the one real regression (see above).

Still open:

- The tick batch, with the splice back on early exit (item 3 under Wiring).
- Docs to update before merging: `CLAUDE.md` (the exception policy still mentions `loop_handle`, removed here, and says
  callbacks should own what they touch; the queued-resume, double-scheduling and allocation-failure notes describe the
  old queue) and the README's `call_soon` notes.

## Small things

- `ITO_ENSURE`:
  - `if (!(c)) throw_ensure(...);` has the dangling-else problem: `if (a) ITO_ENSURE(b); else ...` binds the `else` to
    the macro's `if`. Wrap it in `do { ... } while (false)`.
  - `throw_ensure` is called unqualified, so the macro only compiles inside `ito::details`.
  - Its `NOLINTNEXTLINE()` has an empty check list.
  - Nothing uses it yet; add it with its first caller.
- `ITO_ASSERT(condition)` expands with its own `;`, so `ITO_ASSERT(x);` produces `;;`.
- The `else if` branch in `intrusive_queue_node::unlink()` can only be taken by the head sentinel, which never has a
  next node at that point. Harmless, but worth a comment.
- The queue's move constructor makes `loop` movable. That's fine, just new.
- `tests/test_intrusive_queue.cpp`:
  - It's missing `<array>` and `<string>`, and uses unqualified `size_t`.
  - Missing cases: pushing an already-linked node, the queue destroyed before its nodes, a node destroyed mid-drain,
    and the splice back after the drain stops early.
- WIP leftovers to clean up before merging:
  - the tests commented out in `tests/CMakeLists.txt`;
  - the commented-out timer block and old `m_queue` in `loop.hpp`, and the commented-out body of `call_soon`;
  - `[[maybe_unused]]` on `call_soon`'s parameter;
  - an extra blank line at the top of `loop.hpp`.
