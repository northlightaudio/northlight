# Building Northlight

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 or later with the **Desktop development with C++** workload,
  which brings MSVC, CMake 3.28 or later, Ninja and clang-format
- Git

The first configure downloads JUCE, Catch2, nlohmann/json, CLAP and Signalsmith
Stretch into `build/<preset>/_deps`. Nothing else needs installing.

## Build

From any PowerShell window, `scripts/build.ps1` finds Visual Studio's tools and runs
CMake for you:

```powershell
.\scripts\build.ps1                          # Debug build
.\scripts\build.ps1 -Test                    # Debug build, then the tests
.\scripts\build.ps1 -Run                     # Debug build, then launch
.\scripts\build.ps1 -Preset windows-release  # Release build
```

From a Developer PowerShell for Visual Studio you can use the presets directly:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

| Preset | Build type | Notes |
|---|---|---|
| `windows-debug` | Debug | Real-time checks on |
| `windows-relwithdebinfo` | RelWithDebInfo | For profiling |
| `windows-release` | Release | Real-time checks off |
| `ci` | RelWithDebInfo | Warnings as errors, no ASIO |

The application is `build/<preset>/src/app/northlight_artefacts/<config>/Northlight.exe`.
The build copies the design tokens, fonts, icons, brand files, the licence and the
third-party notices beside it.

## ASIO

Steinberg's licence does not allow the ASIO SDK to be redistributed, so it is not in
the repository. Without it Northlight builds with Windows Audio only. To enable ASIO:

1. Download the ASIO SDK from Steinberg and unzip it outside the repository.
2. Point `NORTHLIGHT_ASIO_SDK_DIR` at it, as an environment variable before
   configuring or as a CMake cache variable:

   ```powershell
   $env:NORTHLIGHT_ASIO_SDK_DIR = 'C:\SDKs\asiosdk'
   .\scripts\build.ps1
   ```

VST3 hosting needs no external SDK; the VST3 SDK comes with JUCE.

## Options

| Option | Default | Meaning |
|---|---|---|
| `NORTHLIGHT_BUILD_TESTS` | ON | Build the unit tests |
| `NORTHLIGHT_BUILD_HARDWARE_TESTS` | ON | Build the hardware tools (never run by CTest) |
| `NORTHLIGHT_RT_CHECKS` | ON (OFF in `windows-release`) | Hook allocation to catch it on real-time threads |
| `NORTHLIGHT_WARNINGS_AS_ERRORS` | OFF (ON in `ci`) | Treat warnings as errors |
| `NORTHLIGHT_ASIO_SDK_DIR` | empty | The ASIO SDK root |
| `NORTHLIGHT_VERSION_LABEL` | `dev` | The version's label: `dev`, a pre-release such as `beta.1`, or empty for a release (see [CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases)) |

## Tests

```powershell
ctest --preset windows-debug
```

runs both test suites and the source checks. The suites can also be run directly:

- `build/windows-debug/tests/unit/northlight_unit_tests.exe`: the model and the
  engine, with no JUCE and no audio device.
- `build/windows-debug/tests/unit_juce/northlight_juce_tests.exe`: JUCE-linked
  code, still without a device.

Two tools under `tests/hardware` need a real device or a long run and are only run by
hand:

- `northlight_loopback_tool` lists devices and measures round-trip latency through
  a loopback cable (`--list`; `--type ASIO --device "<name>" --buffer 64 --out 0 --in 0`).
- `northlight_record_soak` pushes synthetic inputs through the real recording path
  faster than real time and verifies every file (`--inputs 16 --minutes 30 --speed 8`).

## Launch options for unattended checks

`Northlight.exe` accepts options that set up a session without anyone touching it,
so a change can be checked, screenshotted or benchmarked from a script. For example:

```powershell
Northlight.exe --no-splash --demo-song "$env:TEMP\demo.northlight" --play --master-gain -120
```

writes and opens the demo song, plays it and keeps it silent. The options open
windows and panels, build example material (tracks, instruments, automation, takes,
tempo changes), drive the transport and recording, and run benchmarks. The full list
with what each does is at the top of `src/app/Main.cpp`.

## User data

| What | Where |
|---|---|
| Settings, plugin registry, autosaves, crash reports | `%APPDATA%\Northlight` |
| Templates, recordings of unsaved projects | `Documents\Northlight` |
| A project's recordings and media | `<project> Media`, beside the project file |
| Design token overrides | `tokens.json` beside the settings file |
