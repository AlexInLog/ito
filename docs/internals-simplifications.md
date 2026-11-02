# Internals simplifications

Two planned refactors of `include/ito/details/utils/`. Neither changes behavior. They're the next step on the
roadmap, done as two separate PRs, each verified by the existing tests plus benchmarks before and after. Related open
issues and the longer roadmap are in [ideas.md](ideas.md).

## 1. `guarded<T>`: a reversed `trackable` for queued resumes

### Problem

`trackable<T>` pairs an owner holding a `T` with a `weak_view` that borrows it. That fits `future_state<T>`: the
promise reads and writes the shared state through the view.

The other users only want to resume a coroutine if its frame is still alive:

- the sleep awaitable (`trackable<std::coroutine_handle<>>`, view in the timer heap),
- `run_until_complete` (same type, view in the ready queue),
- `task` (`trackable<raii_coroutine_handle<>>`, view in the ready queue),
- the future continuation (`future<T>`'s awaitable, view in `future_state`).

Here nobody reads the payload on the owner's side. The handle is known when the entry is queued and never changes;
the owner exists only so that its destruction cancels the queued resume. The payload sits on the wrong side, and the
loop chases `view -> owner -> handle` to get a value it could carry itself. It also gives `loop_entry_t` two
alternatives with the same meaning ("resume this handle if still alive"), which differ only in the owner's payload
type.

### Design

Keep the link and back-pointer patching, but put the payload on the queued side:

| | holds `T` | other side |
|---|---|---|
| `trackable<T>` (kept for `future_state`) | owner | `trackable<T>::weak_view` |
| `guarded<T>` (queued resumes) | `guarded<T>`, the queued ref | `guard`, an alive marker |

```cpp
class guard;

class guard_link {                 // non-template: back-pointer patching on move/destroy
    friend class guard;
    guard* m_guard{};
};

class guard {
    guard_link* m_link{};          // nulls m_link->m_guard on destruction, re-points on move
};

template<typename T>
class guarded : private guard_link {
    T m_value;
public:
    static std::pair<guard, guarded> create(T value);
    T* get() { return alive() ? &m_value : nullptr; }
};
```

- `guard` is not a template: it holds no `T`, and its back-pointer goes to the non-template `guard_link`. Code that
  only holds a guard (awaitables, `task`, `run_until_complete`) doesn't spell out `T`. A nested `guarded<T>::guard`
  would also work, but it would make `guarded<A>::guard` and `guarded<B>::guard` distinct types for no reason.
- `guarded<T>` copies the `std::coroutine_handle<>`. That's fine: a handle is one pointer to a frame whose address
  never changes. Safety comes from the alive check before resuming, not from where the handle is stored. The bug
  with queuing a raw handle was the missing check.
- `task` becomes `raii_coroutine_handle m_h; guard m_guard;`, which separates "owns the frame" from "its
  destruction cancels the queued start".
- `loop_entry_t` becomes `guarded<std::coroutine_handle<>> | move_only_callback`.
- `trackable` could later be rebuilt on the same link base, if one implementation is wanted.

### Invariant

The guard must be dead whenever the frame is destroyed. The current placements satisfy this:

- sleep and future awaitables: the guard lives inside the frame;
- `run_until_complete`: the guard is on the stack next to the `raii_coroutine_handle`, and both go on return or
  unwind;
- `task`: the guard sits next to the `raii_coroutine_handle` that destroys the frame.

Putting a guard somewhere that can outlive the frame breaks it. `trackable` has the same invariant today (the owner
must die with the frame); it just isn't stated anywhere.

### Order

The prerequisite is done: future continuations are queued as views, and the raw `std::coroutine_handle<>`
alternative is gone from `loop_entry_t` (so `call_soon` no longer accepts raw handles). What's left is this refactor,
as a pure behavior-preserving change with the existing tests as the invariant, in two steps:

1. The `guard`/`guarded<T>` primitive itself, next to `trackable`. This is where the design decisions are: the shared
   link base, move semantics, and what `guarded<T>` exposes.
2. Moving the four users onto it (sleep, `run_until_complete`, the future continuation, `task`), which collapses the
   two handle-view alternatives in `loop_entry_t` into one and shrinks `details::loop_handle` to match. `task` needs
   the most care: its guard and its `raii_coroutine_handle` become separate members, and the invariant above has to
   hold for both.

Benchmark it: `run_until_complete`'s guard (~1 ns) and the future continuation both sit on paths this refactor
removes an indirection from.

### Multi-threading

Like `trackable`, this is single-threaded only: moves patch a non-atomic back-pointer on the other side. Under
work-stealing the guard (in a task dropped on one thread) and the ref (in a queue stolen by another) race. The likely
replacement is a strong ref in the queue plus a cancelled bit and refcount in the frame header: the popper sees the
bit and destroys the frame without resuming it. The cost is that cancelled frames live until the scheduler reaches
them, and their locals are destroyed on the scheduler's thread. Keeping `guarded` separate from `trackable` means
this queued-resume path can be replaced on its own, while `future_state` moves to atomic shared state separately.

## 2. Flatten `error_or_optional`

Mechanical once one decision is made: how `get_result()` treats consumption (first point under "Points to settle"
below). The SonarCloud `cpp:S836` false positive on `future<T>::await_resume()` goes through this code, so check
whether it's still reported afterwards.

### Problem

One concept (a set-once `variant<monostate, T, exception_ptr>`) is spread over `error_or_optional_base<T>`,
`error_or_optional<T>`, `error_or_optional<void>` and `void_t`. The layering exists only to give `set_result()` and
`get_result()` their void forms.

### Design

C++20 constrained non-template members collapse it into one class:

```cpp
template<typename T>
class error_or_optional {
    using stored_t = std::conditional_t<std::is_void_v<T>, void_t, T>;
public:
    void set_result() requires std::is_void_v<T>;
    void set_result(const stored_t&) requires (!std::is_void_v<T>);
    void set_result(stored_t&&)      requires (!std::is_void_v<T>);
    std::add_rvalue_reference_t<T> get_result();   // void for T = void
    // is_ready(), set_exception() unchanged
};
```

`std::add_rvalue_reference_t<void>` is `void`, so `get_result()` needs no special case. Codegen should be the same.
Check the benchmarks anyway, since the `get_if` hot path and the out-of-line throw helpers were tuned for inlining.

### Points to settle while in there

- **`get_result()` returns `T&&` into its own storage and never marks it consumed** (the `// TODO: clear state`).
  This is safe only because every consumer is single-shot (`co_await` is `&&`-qualified, `run_until_complete`
  returns by value). A second call silently returns a moved-from `T`. Either document single consumption as the
  caller's invariant, or move out and reset to `monostate`. That reset is an extra destroy on the hot path, so
  measure it.
- **The name** describes the type algebra, not the role. `result_slot<T>` or `outcome<T>` would fit better. Rename it
  only as part of this change, to avoid churn.
- **`ensure_not_set()` is redundant for the `coro` promise**, since the language already guarantees a single
  `return_value`/`unhandled_exception`. Only `future` needs it. It's one predictable branch, so not worth splitting
  the type over.
- **Unused include:** `error_or_optional.hpp` includes `overloaded.hpp` but uses nothing from it, and nothing else
  in `include/` uses `overloaded`.
