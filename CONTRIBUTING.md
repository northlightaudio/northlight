# Contributing to Northlight

Thanks for your interest. This page covers how to build and check a change, the
conventions the code follows, and what a good commit looks like.

By contributing you agree that your contribution is licensed under the GNU AGPLv3,
the project's licence ([LICENSE](LICENSE)), and to follow the
[code of conduct](CODE_OF_CONDUCT.md). Report security problems privately, as
[SECURITY.md](SECURITY.md) describes, not in an issue.

## Before you start

- Read [docs/architecture.md](docs/architecture.md) for how the pieces fit together,
  and [docs/rt-rules.md](docs/rt-rules.md) before touching anything the audio thread
  runs.
- For anything user-facing, the screen's specification is in
  [docs/design/](docs/design/README.md). The spec is the definition of done, and
  every screen must meet the
  [design bar](docs/design/principles.md#the-design-bar): all of it designed,
  nothing left looking like a toolkit default.
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
- a visible change meets the design bar, checked by looking at `--capture`
  screenshots at 100% and 200% of each state it touches;
- a change to painting stays within the UI performance budgets
  ([docs/ui-performance.md](docs/ui-performance.md)): the work budgets run in
  `ctest`; run the paint-time budgets
  (`.\scripts\build.ps1 -Preset windows-release -Test -Label perf`) too, and
  `scripts/perf.ps1` before merging a change to how the UI repaints;
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

Tokens alone do not make a screen finished. Every element and state is designed to
the design system, nothing is drawn by JUCE's defaults, and a pattern the design
system lacks is added there before it is built. The
[design bar](docs/design/principles.md#the-design-bar) is the full list.

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

## Versions and releases

Northlight's version is **MAJOR.MINOR.PATCH**, with the numbers defined by what a
user notices rather than by an API:

- **MAJOR**: a deliberate break, such as projects that older versions can no longer
  open. Rare.
- **MINOR**: a feature release.
- **PATCH**: fixes only, no new features and no change to the project file format.
  Safe to install in the middle of a project.

Until 1.0 the version stays at `0.x`, and moving to 1.0 is a promise: every 1.x opens
every 1.x project. The project file format has its own version number in the file,
which changes independently of the application's version. Until release there is no
upgrade chain: new fields are optional with defaults, so older files still open.

**One source.** The number is set in exactly one place, `project(... VERSION x.y.z)`
in the top-level `CMakeLists.txt`. A label is added by the `NORTHLIGHT_VERSION_LABEL`
CMake option: `dev` by default, a pre-release such as `beta.1`, or empty for a final
release. The build reads the git commit every time it runs, so a development build
names the commit it came from:

| Build | About box and splash | Crash reports and `version::full()` |
|---|---|---|
| Development | `0.2.0-dev · 1cb6f30` (`*` when there are uncommitted changes) | `0.2.0-dev+1cb6f30` (`.dirty` when there are uncommitted changes) |
| Pre-release | `0.2.0-beta.1` | `0.2.0-beta.1+9a8b7c6` |
| Release | `0.2.0` | `0.2.0+9a8b7c6` |

**Making a release.**

1. Set the version in `CMakeLists.txt` if it has not been set already.
2. Move the **Unreleased** section of [CHANGELOG.md](CHANGELOG.md) under a heading
   for the version and the date.
3. Commit, then tag the commit `vX.Y.Z` (or `vX.Y.Z-beta.N` for a pre-release) with
   an annotated tag.
4. Push the tag. The release workflow builds the portable zip and the installer
   from it, signed, and drafts a GitHub release with them; check it and publish.
   `scripts/package.ps1 -Label ''` (or `-Label beta.N`) makes the same files
   locally ([docs/building.md](docs/building.md#packaging)).
5. Raise the version on `main` to the next minor (`X.Y+1.0`) so development builds
   are never mistaken for the release.

Development happens on `main`. A `release/X.Y` branch is made from a tag only when
a release needs a patch while `main` has moved on. Every distributed build must say
where its source is (the AGPLv3 requires it); the tag is that source.

## Third-party code and SDKs

Dependencies are fetched by CMake, never committed; never add an SDK to the
repository. ASIO comes with JUCE, which bundles the ASIO SDK header under its GPLv3
option. A new dependency needs a licence compatible with the AGPLv3 and an entry in
`scripts/third-party-notices.py`.
