# Aural

A native, high-performance digital audio workstation for Windows. ASIO, VST3 hosting,
multitrack recording, and a UI that is designed before it is built.

Status: **Phase 0 (foundation)**. See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## Building

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 or later with the "Desktop development with C++" workload
  (this brings MSVC, CMake 3.28+, Ninja and clang-format)
- Git

From any PowerShell window:

```powershell
.\scripts\build.ps1               # Debug build
.\scripts\build.ps1 -Test         # Debug build + unit tests
.\scripts\build.ps1 -Run          # Debug build + launch
.\scripts\build.ps1 -Preset windows-release
```

Or from a Developer PowerShell / Developer Command Prompt for VS:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The first configure downloads JUCE and Catch2 into `build/<preset>/_deps`.

### ASIO

The Steinberg ASIO SDK cannot be redistributed, so it is not in the repo. To enable
ASIO:

1. Download the ASIO SDK from Steinberg and unzip it somewhere outside the repo
   (for example `H:\Tools\asiosdk`).
2. Set `AURAL_ASIO_SDK_DIR` to that directory, either as an environment variable before
   configuring or as a CMake cache variable:

   ```powershell
   $env:AURAL_ASIO_SDK_DIR = 'H:\Tools\asiosdk'
   .\scripts\build.ps1
   ```

Without it the app builds with WASAPI only. VST3 hosting needs no external SDK; the
VST3 SDK ships inside JUCE.

### Build options

| Option | Default | Meaning |
|---|---|---|
| `AURAL_BUILD_TESTS` | ON | Build the Catch2 unit tests |
| `AURAL_RT_CHECKS` | ON (OFF in `windows-release`) | Hook operator new/delete to catch allocations on real-time threads |
| `AURAL_WARNINGS_AS_ERRORS` | OFF (ON in `ci`) | `/WX` |
| `AURAL_ASIO_SDK_DIR` | empty | Path to the ASIO SDK root |

## Layout

```
cmake/            CMake helpers
docs/             Plan, architecture notes, real-time rules, coding standards, design
scripts/          Developer scripts
src/app/          Application entry point and main window
src/engine/rt/    Real-time safety guard (grows into the engine in Phase 2)
tests/unit/       Catch2 unit tests
```

The full intended layout is in [docs/PLAN.md](docs/PLAN.md), section 9. Directories are
added when their first code lands.

## Documentation

- [docs/PLAN.md](docs/PLAN.md): goals, architecture, roadmap, open decisions
- [docs/rt-rules.md](docs/rt-rules.md): what real-time threads may and may not do
- [docs/coding-standards.md](docs/coding-standards.md): style and conventions
- [docs/architecture.md](docs/architecture.md): living architecture notes

## Licensing

Not yet decided (open decision 2 in the plan). Until it is, this repository is
all-rights-reserved. JUCE is used under its GPLv3 option during development, which is
why the JUCE splash screen is shown; a commercial JUCE licence or a GPL release of
Aural removes it.
