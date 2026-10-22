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

To keep the machine usable while it builds (for a game, or a recording session),
add `-LowPriority` to run the compilers, linker and tests at Idle priority, and
`-Jobs <n>` to compile at most n files at once (every core by default). `-Run` still
starts Northlight at normal priority.

```powershell
.\scripts\build.ps1 -Test -LowPriority -Jobs 4
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

## Linux (experimental)

Windows is the supported platform. The tree also builds and its tests pass with GCC
on Linux (checked on Arch under WSL, GCC 16), so portability breaks show up early,
but the result is not a usable DAW yet.

You need CMake 3.28 or later, Ninja, a C++20 compiler and JUCE's Linux dependencies:
the ALSA, FreeType, fontconfig, X11 (Xrandr, Xinerama, Xcursor, Xext) and OpenGL
development packages.

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

What is missing on Linux:

- CLAP plugin windows. Only the win32 window API is hosted, so CLAP plugins show no
  editor. VST3 editors go through JUCE and are untested.
- Crash containment around plugin calls (`CrashGuard`) does nothing; a crashing
  plugin takes the app down. Scanning still runs in a separate process.
- Render workers run at normal priority (no `SCHED_FIFO` or rtkit request).
- Audio is ALSA only; JACK is not enabled.
- Crash reports (minidumps) and the "reduce motion" system setting are Windows only.
- The UI token check (`check-ui-tokens.ps1`) runs only on Windows.
- GCC's `-Wextra` warns about partly initialised `ParameterInfo` aggregates, so a
  warnings-as-errors build does not pass yet.

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

### Finding where clicks come from

`--output-tap <folder>` records exactly what Northlight hands the audio driver,
until the app quits:

```powershell
Northlight.exe --output-tap "$env:TEMP	ap" --open "song.northlight" --play
```

The folder gets `output.wav` (32-bit float, the first two outputs), `events.csv`
(every jump or gap found in that audio, every late callback and overrun, each with
its time from the start of the file and its offset in the device block) and, on
quit, `summary.txt`. A click you heard that is in `output.wav` was made by Northlight
or a plugin; jumps at block offset 0 point at a block-boundary bug. A click that is
not in the file happened after it, in the driver, the system or the hardware; late
callbacks at the same time point at system timing (LatencyMon shows which driver).
The file grows by about 23 MB a minute at 48 kHz.

To compare buffer sizes unattended, `--buffer-size <n>` runs the device at that size
for one run without changing the saved settings, and `--quit-after <s>` quits (and so
finishes the tap's files) after that many seconds:

```powershell
Northlight.exe --open "song.northlight" --buffer-size 256 --output-tap "$env:TEMP	ap-256" --play --quit-after 40
```

The tap only sees what Northlight produced. To catch clicks made after it, connect an
output to an input with a cable and record that input at the same time.

## User data

| What | Where |
|---|---|
| Settings, plugin registry, autosaves, crash reports | `%APPDATA%\Northlight` |
| Templates, recordings of unsaved projects | `Documents\Northlight` |
| A project's recordings and media | `<project> Media`, beside the project file |
| Design token overrides | `tokens.json` beside the settings file |
