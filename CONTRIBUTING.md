# Contributing to Northlight

Thanks for your interest. This page covers how to build and check a change, the
conventions the code follows, and what a good commit looks like.

By contributing you agree that your contribution is licensed under the GNU AGPLv3,
the project's licence ([LICENSE](LICENSE)).

## Before you start

- Read [docs/architecture.md](docs/architecture.md) for how the pieces fit together,
  and [docs/rt-rules.md](docs/rt-rules.md) before touching anything the audio thread
  runs.
- For anything user-facing, the screen's specification is in
  [docs/design/](docs/design/README.md). The spec is the definition of done.
- For a large change, open an issue first so the approach can be agreed.

## Build and check

[docs/building.md](docs/building.md) has the details. In short:

```powershell
.\scripts\build.ps1 -Test
```

A change is ready when:

- it builds without warnings (CI builds with warnings as errors);
- `ctest` passes: both test suites, and the check that `src/ui` holds no literal
  colours;
- every changed C++ file is formatted with `clang-format` (CI rejects unformatted
  files; the configuration is `.clang-format`);
- a change to the design tokens still passes `python scripts/contrast.py --check`,
  and `docs/design/contrast.md` is regenerated with
  `python scripts/contrast.py --out docs/design/contrast.md`;
- a dependency change is followed by `python scripts/third-party-notices.py`.

## Code

Most of this is enforced by `.clang-format` and the compiler. Anything not covered
follows the C++ Core Guidelines.

### Language

- C++20 on MSVC with `/permissive-`. Code should also compile with Clang, so a later
  macOS port is not a rewrite: keep MSVC-only intrinsics inside `#if` blocks.
- Exceptions are for genuinely exceptional failures (a corrupt file, out of memory) on
  non-real-time threads. Never on a real-time thread, never for control flow.
- A function that can fail returns its error (`std::optional` of the error, or of the
  result) rather than throwing; a programming error is a `jassert`.
- `dynamic_cast` in a hot path is a review flag.

### Formatting and naming

Allman braces, 4-space indent, 110 columns, pointers left-aligned (`int* p`).

| Thing | Style | Example |
|---|---|---|
| Namespaces | lower case | `northlight::engine` |
| Types | PascalCase | `TrackNode`, `ScopedRealtimeSection` |
| Functions, variables, parameters | camelCase | `compileSnapshot()`, `blockSize` |
| Private members | camelCase with a trailing underscore | `mainWindow_` |
| Constants and enumerators | camelCase | `maxChannels`, `ViolationKind::allocation` |
| Macros | `NORTHLIGHT_` prefix, upper case | `NORTHLIGHT_ASSERT_NOT_REALTIME` |
| Files | PascalCase, named after the main type | `RealtimeGuard.h` |

Everything lives in `northlight::`, with sub-namespaces that mirror `src/`. Headers
are included by path from `src/` (`#include "engine/rt/RealtimeGuard.h"`), never
with `../`. No `using namespace` in headers, and no `using namespace juce` anywhere.

### JUCE

JUCE is the platform layer and the UI toolkit, not the data model. `src/core` and
`src/engine` do not depend on it, so they are tested without a message loop. The
project model is plain structs and commands, not `juce::ValueTree`. Use the
non-deprecated JUCE 8 APIs; deprecation warnings fail CI.

### Real-time code

No allocation, locks, file I/O or UI on a real-time thread. Mark real-time functions
`noexcept`, and put `NORTHLIGHT_ASSERT_NOT_REALTIME` at the top of anything that is
not safe. Engine code never logs; it reports through status values the UI reads.
[docs/rt-rules.md](docs/rt-rules.md) has the full rules.

### User interface

Colours, sizes, spacing and fonts come from the design tokens through `ui::Theme`,
never literals. A new token goes into `docs/design/tokens.json` with a contrast pair
if it is text.

### Tests

- Non-UI code has Catch2 tests in `tests/unit/<module>/`; JUCE-linked code in
  `tests/unit_juce/`.
- Name tests for the behaviour ("Allocation on a real-time thread is reported"), not
  the function under test.
- Tests are deterministic and never depend on audio hardware. Tools that need a
  device live in `tests/hardware/` and are run by hand.
- A new command gets an apply, undo and redo test; the model's round-trip test covers
  it once it is in the project file.

## Commits

- Small commits with a one-line summary in the imperative ("Add a tempo track").
- A change to a real-time path says so in the body and names what makes it safe.
- Documentation changes in the same commit as the behaviour it describes:
  [docs/architecture.md](docs/architecture.md) for how a part works,
  [docs/decisions.md](docs/decisions.md) for a choice with a reason worth keeping,
  the design spec for anything a user sees.

## Third-party code and SDKs

Dependencies are fetched by CMake, never committed. The Steinberg ASIO SDK cannot be
redistributed: never add it, or any other SDK, to the repository. A new dependency
needs a licence compatible with the AGPLv3 and an entry in
`scripts/third-party-notices.py`.
