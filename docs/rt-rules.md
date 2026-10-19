# Real-time thread rules

These rules apply to any code that can run on the audio callback thread or an engine
worker thread. They exist because a single missed deadline is an audible glitch, and
the worst offenders (page faults from allocation, priority inversion from locks) are
unbounded in duration.

The rules are enforced three ways: code review, `AURAL_ASSERT_NOT_REALTIME()` at the
entry of every function that is known to be unsafe, and the debug-build allocation hook
in `src/engine/rt/RealtimeAllocationHook.cpp`.

## What a real-time thread is

A thread is real-time while it is inside an `aural::rt::ScopedRealtimeSection`. The
audio device callback enters one before calling into the engine; worker threads enter
one for the lifetime of each job. Everything called from inside that scope, including
plugin `process` calls, is subject to these rules.

## Never, on a real-time thread

1. **Allocate or free memory.** No `new`, `delete`, `malloc`, `free`, `std::vector`
   growth, `std::string` construction, `std::function` with captures larger than the
   small buffer, `std::shared_ptr` creation, `juce::String`, `juce::Array` growth,
   `juce::var`, or `juce::ValueTree`. Preallocate at graph compile time.
2. **Take a lock or wait.** No `std::mutex`, `juce::CriticalSection`,
   `juce::SpinLock`, `WaitableEvent::wait`, condition variables, `Thread::sleep`, or
   `join`. A try-lock that never blocks is acceptable only in the rare case where
   failure has a safe fallback, and must be documented at the call site.
3. **Do system calls that may block.** No file I/O, no sockets, no `OutputDebugString`,
   no `printf`, no logging, no `GetTickCount`-style calls that are not known to be
   user-mode only. `QueryPerformanceCounter` and `std::chrono::steady_clock` are fine.
4. **Touch the UI or message thread.** No `MessageManager::callAsync`,
   `Component::repaint`, `AsyncUpdater::triggerAsyncUpdate` (it allocates on first use
   and posts a message). Publish state through the lock-free snapshot instead.
5. **Throw exceptions** or call anything that might. Mark RT functions `noexcept`.
6. **Call into unknown code** other than plugin processing, which is already
   constrained by the plugin API contract.
7. **Produce or consume denormals** without FTZ/DAZ set. The RT section entry sets the
   flags; do not clear them.

## Always, on a real-time thread

- Read shared state through an atomically-published immutable snapshot
  (`std::atomic<Snapshot*>` with acquire loads), or through a lock-free queue.
- Write results into buffers owned by the RT thread or into lock-free queues with a
  preallocated capacity. If a queue is full, drop and count the drop; never block.
- Keep per-callback work bounded. Loops over tracks and nodes are fine; loops whose
  bound depends on user input (e.g. the number of pending UI commands) must be capped.
- Use `AURAL_ASSERT_NOT_REALTIME("reason")` at the top of any helper that is unsafe,
  so an accidental call from RT code is caught in debug builds.

## Ownership and lifetime

- The RT thread never frees anything. When it finishes with a snapshot (because a new
  one was swapped in), it pushes the old pointer to a garbage queue that the controller
  thread drains and deletes.
- Objects referenced by a snapshot (plugin instances, buffers, clip readers) outlive
  every snapshot that references them. The controller enforces this with reference
  counts on the non-RT side.
- A plugin's `process` is called only from RT threads; its editor and parameter
  listeners only from the message thread. Parameter changes cross between the two via
  the engine's command and snapshot paths, not via the plugin directly.

## Transitions

Moving data between the UI/controller and RT sides uses exactly three mechanisms:

| Direction | Mechanism | Used for |
|---|---|---|
| Controller to RT | Atomic snapshot swap | Graph structure, clip placement, routing, anything structural |
| Controller to RT | SPSC lock-free command queue | Transport commands, parameter changes, arm/monitor toggles |
| RT to controller | SPSC lock-free event queue plus double-buffered meter block | Playhead position, meters, dropouts, recorded-block notifications |

Anything that does not fit one of these needs a design discussion first.

## Verifying

- Debug and RelWithDebInfo builds define `AURAL_RT_CHECKS`. Any `operator new` or
  `operator delete` on an RT thread aborts with a message naming the thread state.
- The unit tests in `tests/unit/rt/` exercise the guard itself.
- Phase 2 adds an offline engine harness that runs scripted sessions under the checker
  in CI, so a regression that allocates on the audio thread fails the build.
- `malloc`/`free` and third-party allocations are not hooked. Plugins that allocate in
  `process` are their vendor's problem; our diagnostics panel reports callback time
  outliers so the user can find them.
