# Aural

A native, high-performance digital audio workstation for Windows. ASIO, VST3 hosting,
multitrack recording, and a UI that is designed before it is built.

Status: **Phase 6 (editing) complete; Phase 8 (MIDI and instruments) in progress;
Phase 7 (design fidelity pass) waits for the design spec**. See
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
**Mixer** (Ctrl+M) opens strips for every track and bus with output routing, sends,
inserts (built-in gain, EQ, compressor, limiter with an editor window each), pan,
mute, solo, phase, fader and meters, plus the master with LUFS; **+ Bus** adds a bus.
A strip's **Group** button puts it in a fader group: grouped faders move together
(keeping their balance) and mute and solo act on the whole group.
**Inserts ▸ Add** lists the built-ins and every VST3 and CLAP plugin found by
**Scan for plugins...** (the standard VST3 and CLAP folders, each file inspected in
a separate process so a bad plugin cannot crash Aural); a hosted plugin opens its own editor window
and its state is saved with the project; its window has bypass, a wet/dry mix and a
pin (always on top), a Presets menu (factory programs and `.vstpreset` files), and
remembers where you left it. **Plugins...** opens the
browser (search, blocklist, scan). Every insert has a wet/dry mix, and a compressor (built-in or a plugin with a key
input) can be sidechained from another track via its **Sidechain** submenu. Insert latency is
compensated across tracks, buses and sends, and recordings allow for it.
Recordings go next to the project file in a `<project> Media` folder, or to
`Documents\Aural\Recordings` for an unsaved project, each with a small
`.recording.json` beside it while it is in progress. If the app dies while
recording, the next launch offers to repair the unfinished files and put them back
on their tracks where they were captured (or on "Recovered" tracks when the project
has changed), or set them aside. Recording needs the device at the project's sample
rate.
**Editing** in the arrange view: click a clip to select it (Shift adds, Ctrl
toggles), drag on empty space to lasso, drag a clip to move it (across tracks too),
drag its edge to trim, drag the handle at a top corner for a fade, Ctrl+drag up or
down for clip gain. Moves and trims snap to the grid (**G** toggles). **Delete**
removes (with ripple on, **E**, later clips close the gap), **Ctrl+D** duplicates,
**S** splits at the playhead, **J** joins, **X** crossfades overlapping neighbours,
**Ctrl+Shift+N** normalises, **Left/Right** nudge by a grid step, **Ctrl+A** selects
all, **Escape** clears the selection. **M** drops a marker at the playhead and
**Shift+M** a region from the loop region; **,** and **.** jump between markers; click
a marker's flag in the ruler to select it, drag it to move it, double-click to
rename it, **Delete** to remove it.
**Export...** bounces the mix (or the loop region) to a float WAV at the project
rate, or every track to a stems folder, offline, compensated for insert latency;
the status line shows the progress and the integrated loudness of the result.
Projects are `.aural` files: **New**, **Open...**, **Save** (Ctrl+N / Ctrl+O / Ctrl+S,
Ctrl+Shift+S to save as).

For unattended checks: `Aural.exe --import <file> [--import <file>] --play` (or
`--open <project>`), and
`Aural.exe --record <inputs> [--loop <from> <to>] [--no-count-in] --stop-after <seconds>`; `--mixer`
opens the mixer panel and `--insert <type>` adds a built-in (for example `aural.eq`) to
every track and opens its editor. `--scan-plugins` runs a full plugin scan, `--plugins` opens the browser, `--marker <seconds>` drops a marker; `--recover` recovers unfinished recordings without
the prompt and `--save <file>` writes the project (so a check can read the placed
clips); `--save` runs two seconds after a preceding `--stop-after`, else half a
second after launch.
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
