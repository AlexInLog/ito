# Internals simplifications

Two refactors of `include/ito/details/utils/`. Neither changes behavior. The second (flattening
`error_or_optional`) is done; the first is the next step on the roadmap, verified by the existing tests plus
benchmarks before and after. Related open
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

## 2. Flatten `error_or_optional` (done)

The set-once `variant<monostate, T, exception_ptr>` used to be spread over `error_or_optional_base<T>`,
`error_or_optional<T>`, `error_or_optional<void>` and `void_t`, only to give `set_result()` and `get_result()` their
void forms. It's now one class, `outcome<T>` in `outcome.hpp` (`void_t` moved to `void.hpp`), using C++20
constrained members:

```cpp
template<typename T>
class outcome {
public:
    using stored_t = std::conditional_t<std::is_void_v<T>, void_t, T>;
    void set_result() requires (std::is_void_v<T>);
    void set_result(const stored_t&) requires (!std::is_void_v<T>);
    void set_result(stored_t&&)      requires (!std::is_void_v<T>);
    T get_result();                                // consumes the result; void for T = void
    void replace_with_exception(const std::exception_ptr&);   // unchecked, for unhandled_exception()
    // is_ready(), set_exception() unchanged
};
```

Behavior and benchmarks are unchanged (all within noise), and it builds and passes with both clang and GCC. The
now-unused `overloaded.hpp` was deleted with it.

### Follow-ups, also done

- **`get_result()` returns `T` and consumes the result.** Every caller returns `T` by value, and with guaranteed copy
  elision returning `T` costs the same single move as returning `T&&` (checked with a move-counting type on clang and
  GCC). A guard empties the slot on every exit path, so a second call reports `empty_value` rather than handing out a
  moved-from `T` or rethrowing the same exception. Benchmarks are unchanged: the moved-from `T` was destroyed later
  anyway. `tests/test_outcome.cpp` covers it.
- **The coroutine promise no longer goes through `ensure_not_set()`.** It wasn't just redundant there: a local whose
  destructor throws after `co_return` makes `unhandled_exception()` run after `return_value()`, and the check then
  threw `value_is_set` out of `unhandled_exception()`, and so out of `resume()`, hiding the real exception.
  `unhandled_exception()` now uses `outcome::replace_with_exception()`, which overwrites unchecked; `future`'s
  `set_exception()` keeps the check, since a second set there is a caller error. Covered in `tests/test_coro.cpp`.
