# AGENTS.md

Notes for coding agents working on Northlight, a Windows DAW in C++20 on JUCE 8
(formerly called Aural; the repository folder may still carry that name). The
human-facing docs are thorough; this file says what to read, how to build and check
a change, and the rules that are easiest to break.

## Read first

| When you touch | Read |
|---|---|
| Anything | [CONTRIBUTING.md](CONTRIBUTING.md) (conventions, checks, commits) and [docs/architecture.md](docs/architecture.md) |
| Code the audio callback or a render worker can reach | [docs/rt-rules.md](docs/rt-rules.md) |
| Anything a user sees | The screen's spec under [docs/design/](docs/design/README.md). The spec is the definition of done |
| A design choice that looks odd | [docs/decisions.md](docs/decisions.md) before changing it |
| Build options, tests, launch flags | [docs/building.md](docs/building.md) |

## Layout

```
src/core       Project model, commands and undo, tempo map, JSON project files. No JUCE.
src/engine     Audio engine (snapshots, renderer, workers, disk, recording, export DSP). No JUCE.
src/engine/rt  Real-time primitives: guard, allocation hook, SPSC queue, snapshot publisher.
src/plugins    Built-in processors, channel strip, instruments. No JUCE.
src/hosting    VST3 (via JUCE) and CLAP hosting, registry, out-of-process scanner.
src/audio      Device layer (ASIO, Windows Audio, MIDI), audio callback, media loading.
src/ui         Theme (design tokens), look and feel, components, views.
src/app        Main window, project session, settings, bounce/export, launch flags (Main.cpp).
tests/unit       Catch2, no JUCE, mirrors src/ by module.
tests/unit_juce  Catch2, JUCE-linked code. Never needs an audio device.
tests/hardware   Tools that need a device; built, never run by CTest.
docs/design      Brand, design system, tokens.json, per-screen specs.
scripts          build.ps1, token and contrast checks, notice and asset generators.
```

`src/core`, `src/engine` and `src/plugins` must not include JUCE. The model is plain
structs and commands, not `juce::ValueTree`.

## Build and test

MSVC, CMake, Ninja and clang-format come with Visual Studio and are usually **not on
PATH** in an agent's shell. Use the script, which finds Visual Studio and imports its
environment:

```powershell
.\scripts\build.ps1 -Test                    # windows-debug build, then ctest
.\scripts\build.ps1 -Preset ci -Test         # what CI runs: warnings as errors
.\scripts\build.ps1 -Test -LowPriority -Jobs 4   # when the user is using the machine
```

- `-LowPriority` runs the build and tests at Idle priority and `-Jobs <n>` caps the
  parallel compiles. Use both when the user says they are gaming or otherwise busy
  on the machine. Launch the app then only with `--offscreen`, which runs it with
  no window on screen (captures still work); otherwise wait until they are back.

- Presets: `windows-debug` (real-time checks on), `windows-relwithdebinfo`,
  `windows-release`, `ci`. Output goes to `build/<preset>/`.
- The executable is `build/<preset>/src/app/northlight_artefacts/<config>/Northlight.exe`.
- Test binaries: `build/<preset>/tests/unit/northlight_unit_tests.exe` and
  `build/<preset>/tests/unit_juce/northlight_juce_tests.exe`; both accept Catch2
  filters such as `"[engine]"` or a test name.
- ASIO is on in every build and uses the ASIO SDK header bundled with JUCE, so no
  SDK or environment variable is needed. `-DNORTHLIGHT_ASIO_SDK_DIR=<sdk>` swaps in a
  local SDK's header. Never copy any SDK into the repository.
- clang-format lives at `<VS>\VC\Tools\Llvm\x64\bin\clang-format.exe` if it is not on
  PATH.

## A change is done when

1. It builds with no warnings (CI uses `/WX`; JUCE deprecation warnings count).
2. `ctest` passes. That includes `scripts/check-ui-tokens.ps1`: no literal colours in
   `src/ui` outside `Theme.cpp`.
3. Every changed `.cpp`/`.h` is clang-formatted (`clang-format -i <files>`; CI runs
   `--dry-run --Werror` on all tracked sources).
4. New behaviour has tests in `tests/unit/<module>/` or `tests/unit_juce/`, named
   for the behaviour. A new command gets apply, undo and redo tests.
5. Docs change in the same commit as the behaviour: architecture.md for how a part
   works, decisions.md for a choice worth explaining, the design spec for anything
   visible, and the **Unreleased** section of CHANGELOG.md for user-facing changes.
6. Anything visible meets the design bar in
   [docs/design/principles.md](docs/design/principles.md#the-design-bar): every
   element and state is designed, nothing looks like a JUCE default. Check it with
   `--capture` at 100% and 200% and look at the PNGs; working is not enough.
7. Token changes pass `python scripts/contrast.py --check` and regenerate
   `docs/design/contrast.md`. Dependency changes rerun
   `python scripts/third-party-notices.py`.

## Rules that are easy to break

- **Real-time code:** no allocation, locks, waits, file I/O, logging, exceptions or
  UI calls on the audio callback or render workers. That rules out `juce::String`,
  growing a `std::vector` and `MessageManager::callAsync` too. Mark RT functions
  `noexcept`; put `NORTHLIGHT_ASSERT_NOT_REALTIME("why")` at the top of unsafe
  helpers. The RT thread never frees; retired snapshots go to a garbage queue.
  Debug builds hook `operator new` and fail when it is called on an RT thread.
- **No default-looking UI.** A stock JUCE control, an undesigned hover or disabled
  state, clipped text or a misaligned edge is a bug. If the design system has no
  pattern for what you need, add it to `docs/design/design-system.md` first.
- **UI values come from tokens.** Colours, sizes, spacing and fonts come from
  `ui::Theme`, never literals. A new token goes in `docs/design/tokens.json` (with a
  contrast pair if it is text).
- **Errors:** functions that can fail return them (`std::optional`), not throw.
  Exceptions only for truly exceptional failures on non-RT threads.
- **Naming:** `northlight::` namespaces that mirror `src/`; PascalCase types and
  files; camelCase functions, variables and constants; private members end in `_`;
  macros start with `NORTHLIGHT_`. Include by path from `src/`
  (`#include "engine/rt/RealtimeGuard.h"`), never `../`. No `using namespace juce`.
- **Portability:** keep MSVC-only code inside `#if` blocks so Clang can still compile it.
- **Project file format:** a model change that reaches the file raises
  `Project::schemaVersion` and adds a step to `migrate()` in
  `src/core/ProjectJson.cpp` (a no-op case when it only adds optional fields), and
  is covered by the model's round-trip test. Older files must keep opening.
- **Line endings:** the working tree is CRLF (`.gitattributes` stores LF). Text an
  agent writes may be LF, so normalise before matching multi-line anchors in edit
  scripts.

## Seeing the app without clicking it

Do not drive the GUI with synthetic mouse or keyboard input: the window may open
behind whatever the user is doing, and clicks land there instead. Instead:

- **Launch flags** set up any state from the command line: open panels, build demo
  material, drive the transport, run benchmarks. The full list is the comment block
  in `src/app/Main.cpp` above `applyLaunchFlags`. Example:
  `Northlight.exe --no-splash --demo-song "$env:TEMP\demo.northlight" --play --master-gain -120`
  (`--master-gain -120` keeps it silent). If a screen has no flag, add one.
- **`--capture <folder> [seconds]`** saves the window and its visible panels as PNGs,
  then quits. It uses its own settings folder (`Northlight Capture`, emptied each
  run) and mutes output, so it never touches the user's settings. Prefer this for
  screenshots. `--window-size` and `--capture-scale` control the output. With
  `--preferences` it also saves every Preferences page as `preferences-<page>.png`.
- Without `--capture`, the app reads and writes the user's real settings in
  `%APPDATA%\Northlight` and recordings in `Documents\Northlight`. A `--record` run
  records real input there; clean up what you created.

## Git

- Work on `main` unless told otherwise; small commits with a one-line imperative
  summary. A commit that changes a real-time path says so in the body and names what
  makes it safe.
- Do not commit or push unless the user asks.
- Never commit `build/`, SDKs, generated media or test recordings.
