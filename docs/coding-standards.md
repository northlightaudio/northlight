# Coding standards

Short, because `.clang-format` and the compiler enforce most of it. Anything not
covered here follows the C++ Core Guidelines.

## Language

- C++20, MSVC. No compiler extensions (`/permissive-`). Code should also compile with
  Clang so a future macOS port is not a rewrite; avoid MSVC-only intrinsics outside
  `#if` blocks.
- Exceptions are allowed on non-real-time threads for genuinely exceptional failures
  (file corrupt, out of memory). Never on real-time threads. Never for control flow.
- RTTI is on but `dynamic_cast` in hot paths is a review flag.

## Formatting

- `clang-format` is the authority. CI rejects unformatted files. Run it from the VS
  Developer prompt or the `Format Document` command in VS/VS Code; the config is in
  `.clang-format`.
- Allman braces, 4-space indent, 110 columns, pointers left-aligned (`int* p`).

## Naming

| Thing | Style | Example |
|---|---|---|
| Namespaces | lower | `aural::engine` |
| Types | PascalCase | `TrackNode`, `ScopedRealtimeSection` |
| Functions and methods | camelCase | `compileGraph()` |
| Variables and parameters | camelCase | `blockSize` |
| Private members | camelCase with trailing underscore | `mainWindow_` |
| Constants and enumerators | camelCase | `maxChannels`, `ViolationKind::allocation` |
| Macros | `AURAL_` prefix, SCREAMING | `AURAL_ASSERT_NOT_REALTIME` |
| Files | PascalCase matching the main type | `RealtimeGuard.h` |

One class per header where practical. Headers are included by path from `src/`
(`#include "engine/rt/RealtimeGuard.h"`), never relative `../`.

## Namespaces

- Everything lives in `aural::`. Sub-namespaces mirror `src/` directories: `aural::rt`,
  `aural::engine`, `aural::audio`, `aural::plugins`, `aural::ui`, `aural::core`.
- No `using namespace` in headers. `using namespace juce;` is banned everywhere; spell
  out `juce::`.

## JUCE usage

- JUCE is the platform layer and the UI toolkit. It is not the data model: the project
  model in `core/` and the engine in `engine/` do not depend on JUCE so they can be
  tested without a message loop and reused by the out-of-process scanner.
- `juce::ValueTree` is not used for the project model. The model is plain structs plus
  command objects; serialisation is explicit.
- Prefer `juce::FontOptions`, `juce::Colour::fromRGB` and other non-deprecated APIs;
  JUCE 8 deprecation warnings are errors in CI.

## Real-time code

See [rt-rules.md](rt-rules.md). In short: no allocation, locks, I/O, or UI on RT
threads; mark RT functions `noexcept`; put `AURAL_ASSERT_NOT_REALTIME` at the top of
anything that is not safe.

## Errors and logging

- Functions that can fail return `std::expected<T, Error>` (or `std::optional` when
  there is only one way to fail) rather than throwing, unless the failure is a
  programming error, in which case `jassert`.
- Logging goes through the `aural::log` facade (Phase 1) which is RT-safe on the
  posting side. Do not call `DBG` or `std::cout` from engine code.

## Tests

- Every non-UI module has Catch2 tests in `tests/unit/<module>/`. Tests are named for
  behaviour ("Allocation on a real-time thread is reported"), not for the function
  under test.
- Tests must be deterministic and must not depend on audio hardware. Hardware tests
  live in `tests/hardware/` (Phase 1) and are run manually.

## Commits and reviews

- Small commits with a one-line summary in the imperative ("Add RT allocation hook").
- A change that touches an RT path says so in the commit body and names what makes it
  safe.
- `docs/` is updated in the same commit as the behaviour it describes.
