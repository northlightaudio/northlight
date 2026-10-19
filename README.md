# Aural

A native, high-performance digital audio workstation for Windows. ASIO, VST3 hosting,
multitrack recording, and a UI that is designed before it is built.

Status: **Phase 3 (multitrack recording), in progress**. See
[docs/PLAN.md](docs/PLAN.md) for the roadmap and
[docs/architecture.md](docs/architecture.md) for what is built.

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
| `AURAL_BUILD_HARDWARE_TESTS` | ON | Build the manual hardware test tools (never run by CTest) |
| `AURAL_RT_CHECKS` | ON (OFF in `windows-release`) | Hook operator new/delete to catch allocations on real-time threads |
| `AURAL_WARNINGS_AS_ERRORS` | OFF (ON in `ci`) | `/WX` |
| `AURAL_ASIO_SDK_DIR` | empty | Path to the ASIO SDK root |

## Layout

```
cmake/            CMake helpers
docs/             Plan, architecture notes, real-time rules, coding standards, design
scripts/          Developer scripts
src/app/          Application entry point, main window, project session, settings
src/audio/        Device layer (ASIO/WASAPI), audio callback, calibration, media loading
src/core/         Project model, commands/undo, JSON project files (no JUCE)
src/engine/       Engine: snapshots, transport, renderer, streaming (no JUCE)
src/engine/rt/    Real-time safety guard, SPSC queue, snapshot publisher
src/ui/           Theme tokens, LookAndFeel, Audio Setup screen
tests/unit/       Catch2 unit tests
tests/hardware/   Manual tools that need a real audio device
```

The full intended layout is in [docs/PLAN.md](docs/PLAN.md), section 9. Directories are
added when their first code lands.

## Using the current build

Click **Import Audio...** to put files on tracks (one track per file, starting at 0),
**Play** or the space bar to play, **|<** or Return to go back to the start, and
click in the arrange view to locate, or drop audio files onto it to add tracks at the
drop position. Ctrl+wheel zooms around the pointer; the wheel scrolls tracks and
Shift+wheel or the scrollbars scroll. The status bar shows the audio callback load
(p50/p99/max of the deadline) and any overruns or dropouts. Undo/redo are Ctrl+Z /
Ctrl+Y. Files longer than 30 s
stream from disk; everything is resampled to the project rate (the device rate of
an empty project). Waveform peaks are built in the background and cached next to
each media file as `<file>.<rate>.aural-peaks`.

To record, pick an input in a track's header, arm it with **R** (the **I** button
cycles input monitoring off / auto / on) and press **Rec** or Ctrl+R; **Stop** or the
space bar ends the take. Clips land on the track shifted back by the measured
round-trip latency (or the driver's reported latency when no calibration matches).
**Click** turns the metronome on, **Count-in** cycles 0/1/2 bars of click before a
take, dragging in the ruler sets a region that **Loop** repeats and **Punch**
records into (rolling from one bar before it).
With **Loop** on, each pass through the region becomes a new take on the track.
Recordings go next to the project file in a `<project> Media` folder, or to
`Documents\Aural\Recordings` for an unsaved project. If the app dies while recording,
the next launch offers to repair the unfinished files and add them as "Recovered"
tracks, or set them aside.
Projects are `.aural` files: **New**, **Open...**, **Save** (Ctrl+N / Ctrl+O / Ctrl+S,
Ctrl+Shift+S to save as).

For unattended checks: `Aural.exe --import <file> [--import <file>] --play`, and
`Aural.exe --record <inputs> [--loop <from> <to>] [--no-count-in] --stop-after <seconds>`.
The offline recording soak is `aural_record_soak --inputs 16 --minutes 30 --speed 8`.

### Audio setup

Launch `Aural.exe` and click **Audio Setup...** in the toolbar. Pick the driver type
(ASIO, or one of the Windows Audio modes), the device, sample rate, buffer size and the
channels to enable. With **Pass inputs through to outputs** on you hear the inputs on
the outputs; the Latency section shows the driver's reported input and output latency.

To measure the real round trip, connect one output to one input with a cable, choose
that pair under **Loopback** and click **Calibrate**. The result is stored per device,
sample rate and buffer size and is used to place recorded audio in Phase 3.

Settings live in `%APPDATA%\Aural\Aural.settings`. `Aural.exe --audio-setup` opens the
Audio Setup window on launch.

The same can be done from the command line with the hardware tool:

```powershell
.\build\windows-debug\tests\hardware\aural_loopback_tool_artefacts\Debug\aural_loopback_tool.exe --list
.\build\windows-debug\tests\hardware\aural_loopback_tool_artefacts\Debug\aural_loopback_tool.exe --type ASIO --device "Focusrite USB ASIO" --buffer 64 --out 0 --in 0
```

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
