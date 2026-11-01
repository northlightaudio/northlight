# Building Northlight

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 or later with the **Desktop development with C++** workload,
  which brings MSVC, CMake 3.28 or later, Ninja and clang-format
- Git
- Python 3, which `ctest` uses for its source checks (no literal colours in
  `src/ui`, no thread ever killed). Without it the configure warns and skips them.

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
| `package` | Release | Warnings as errors; built by `scripts/package.ps1` into `build/package` |
| `ci` | RelWithDebInfo | Warnings as errors |
| `linux-debug`, `linux-relwithdebinfo` | Debug, RelWithDebInfo | Linux (experimental, see [Linux](#linux-experimental)) |
| `linux-ci` | RelWithDebInfo | Linux, warnings as errors |

The application is `build/<preset>/src/app/northlight_artefacts/<config>/Northlight.exe`.
The build copies the design tokens, fonts, icons, brand files, the licence and the
third-party notices beside it.

## ASIO

ASIO is on in every build. It uses the ASIO SDK header that JUCE bundles under the
SDK's GPLv3 option, so there is nothing to download, and distributed builds include
ASIO (see [decisions.md](decisions.md)). Set `NORTHLIGHT_ASIO=OFF` for a build with
Windows Audio only.

To use the header from your own copy of Steinberg's ASIO SDK instead, pass the SDK's
root (the folder that contains `common/`) as a cache variable:

```powershell
cmake --preset windows-debug -DNORTHLIGHT_ASIO_SDK_DIR=C:/SDKs/asiosdk
```

The presets do not read a `NORTHLIGHT_ASIO_SDK_DIR` environment variable. A build
folder keeps the value it was configured with; configure with
`-DNORTHLIGHT_ASIO_SDK_DIR=` to go back to the bundled header.

VST3 hosting needs no external SDK; the VST3 SDK comes with JUCE.

## Linux (experimental)

Windows is the supported platform. The tree also builds with GCC on Linux, with no
warnings from its own code, and its tests pass (checked on Arch under WSL, GCC 16),
but the result is not a usable DAW yet. CI builds it on Ubuntu with GCC 14, warnings
as errors and no precompiled headers (the `linux-ci` preset), and runs the tests
under a virtual display (`xvfb-run`).

You need CMake 3.28 or later, Ninja, a C++20 compiler, Python 3 and JUCE's Linux
dependencies: the ALSA, FreeType, fontconfig, X11 (Xrandr, Xinerama, Xcursor, Xext)
and OpenGL development packages. Install a sans-serif, a serif and a monospaced
font (DejaVu covers all three): the tests run without the app's bundled fonts and
use the system's, and a debug build asserts when one of the three is missing.

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

On Ubuntu 24.04 the packages are `ninja-build g++-14 libasound2-dev libfreetype-dev
libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
libxext-dev libxrender-dev libxcomposite-dev libgl1-mesa-dev libjack-jackd2-dev
fonts-dejavu-core` (the `linux` job in `.github/workflows/ci.yml` installs the same).

Audio runs through ALSA, or JACK when the JACK headers were found at configure time
(otherwise the configure warns). JACK covers JACK servers and PipeWire, through its
JACK layer (`pipewire-jack`); the app loads libjack when it runs, so it is not a
dependency. With no saved device, Northlight opens JACK when a server is running and
ALSA's default otherwise.

Render workers run real-time (`SCHED_FIFO`, or `SCHED_RR` from rtkit) when the user may set it themselves
(membership of a group with an `rtprio` limit, usually `audio`) or when rtkit grants
it; the disk thread is raised to nice -10 the same ways. Diagnostics shows how many
workers got real-time scheduling. Without either, they run as normal threads and a
busy machine can make a block late.

What is missing on Linux:

- CLAP plugin windows. Only the win32 window API is hosted, so CLAP plugins show no
  editor. VST3 editors go through JUCE and are untested.
- Crash containment around plugin calls (`CrashGuard`) does nothing; a crashing
  plugin takes the app down. Scanning still runs in a separate process.
- Crash reports (minidumps) and the "reduce motion" and high-contrast system
  settings are Windows only.
- No installer or package; `scripts/package.ps1` makes the Windows ones only.

## Options

| Option | Default | Meaning |
|---|---|---|
| `NORTHLIGHT_BUILD_TESTS` | ON | Build the unit tests |
| `NORTHLIGHT_BUILD_HARDWARE_TESTS` | ON | Build the hardware tools (never run by CTest) |
| `NORTHLIGHT_RT_CHECKS` | ON (OFF in `windows-release`) | Hook allocation to catch it on real-time threads |
| `NORTHLIGHT_WARNINGS_AS_ERRORS` | OFF (ON in `ci` and `linux-ci`) | Treat warnings as errors |
| `NORTHLIGHT_ASIO` | ON | ASIO audio devices (Windows only) |
| `NORTHLIGHT_ASIO_SDK_DIR` | empty | Optional: an ASIO SDK root whose header replaces the one bundled with JUCE |
| `NORTHLIGHT_VERSION_LABEL` | `dev` | The version's label: `dev`, a pre-release such as `beta.1`, or empty for a release (see [CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases)) |
| `NORTHLIGHT_PCH` | ON (OFF in CI) | Precompile the standard, JUCE and Catch2 headers (see [Build speed](#build-speed)) |
| `NORTHLIGHT_COMPILER_CACHE` | OFF (ON in CI) | Compile through sccache, which must be on PATH (see [Build speed](#build-speed)) |

## Build speed

Most of a build's time is spent parsing the same large headers again in every file:
JUCE in the UI, app, device and hosting code, and Catch2 in the tests. Two things
cut it.

**Precompiled headers** (`NORTHLIGHT_PCH`, on by default). The targets that include
JUCE or Catch2 each compile those headers once, and every file in the target starts
from the result. The set is in `cmake/NorthlightPrecompiled.cmake`: standard, JUCE
and Catch2 headers only, never the project's own, so editing a project header still
rebuilds only the files that include it. The JUCE-free libraries (`src/core`,
`src/engine`, `src/plugins`) and `tests/unit` never get the JUCE set. The headers are
force-included, so a file that forgets an include still compiles locally; the CI
build, which has them off, catches it.

**A compiler cache** (`NORTHLIGHT_COMPILER_CACHE`, for CI). Every CI run starts from
an empty build folder, so without a cache it compiles everything, about 45 minutes on
GitHub's four-core runner. With the option on, every compile goes through
[sccache](https://github.com/mozilla/sccache), and the workflow keeps sccache's
folder between runs with `actions/cache`, so a run recompiles only the files whose
code or headers changed. sccache cannot cache a compile that uses a precompiled
header, so CI turns `NORTHLIGHT_PCH` off; and it caches an MSVC compile only when
the debug information is inside the object file, so the option switches it from a
shared PDB (`/Zi`) to `/Z7`.

Measured on a 16-core PC in October 2026, a full `ci` build took 256 s; 177 s with
precompiled headers; and 57 s from an empty build folder with a warm compiler cache
(99.6% of compiles were cache hits; the cache was 354 MB). Filling an empty cache
adds about 10%.

To try the CI configuration locally, put sccache on PATH and configure with both
options:

```powershell
cmake --preset ci -DNORTHLIGHT_PCH=OFF -DNORTHLIGHT_COMPILER_CACHE=ON
```

## Packaging

`scripts/package.ps1` makes what people download: a portable zip and an installer,
both from the same files.

```powershell
.\scripts\package.ps1                        # a dev package, unsigned
.\scripts\package.ps1 -Label ''              # a final release (clean tree, at the tag)
.\scripts\package.ps1 -Label beta.1 -Sign    # a signed pre-release
```

It builds the `package` preset (Release, warnings as errors) in `build/package`,
which no other preset shares, runs the tests except the timed `perf` budgets
(giving a failed test one more run, for the shutdown hang described under "Tests";
CI gives none), copies the app folder to `build/package/stage`, signs the executables when asked,
and writes to `build/package/dist`:

| File | What it is |
|---|---|
| `Northlight-<version>-x64.zip` | The portable app: one folder to unzip anywhere and run. The executables, `assets/`, the licence and the notices |
| `Northlight-<version>-Setup-x64.exe` | The installer ([installer/Northlight.iss](../installer/Northlight.iss), Inno Setup 6) |
| `SHA256SUMS.txt` | Checksums of both, for `sha256sum -c` |

The version is `0.1.0-dev+1cb6f30` for a dev package, `0.1.0-beta.1` or `0.1.0` for
the others. Any label other than `dev` refuses to package uncommitted changes, so a
release is exactly its tag. `-SkipBuild` packages an existing build again and
`-LowPriority` and `-Jobs` work as in `build.ps1`.

The C++ runtime is linked into the executables, so neither the zip nor the
installer needs the Visual C++ Redistributable. Settings and recordings stay in
`%APPDATA%` and `Documents` either way (see [User data](#user-data)), so the zip is
portable only in that it needs no install.

The installer needs **Inno Setup 6**. The script uses the `ISCC.exe` that
`-InnoSetup <path>` or the `NORTHLIGHT_INNO_SETUP` environment variable names,
otherwise the one on PATH or in the usual install folders. The installer:

- installs for the current user by default (no administrator), with an option
  to install for everyone in Program Files;
- adds a Start menu shortcut, an optional desktop shortcut, and opens
  `.northlight` files with Northlight (a task the person can untick);
- closes a running Northlight before an upgrade, and replaces `assets/` whole so
  nothing a newer version dropped lingers;
- leaves settings, plugin lists and recordings when uninstalled.

### Signing

Signing uses [Azure Trusted Signing](https://learn.microsoft.com/azure/trusted-signing/)
(Microsoft is renaming it Artifact Signing) through `signtool` and its plug-in, so
there is no certificate file or hardware token. `-Sign` signs both executables
before they are zipped, then the installer and its uninstaller, every one with an
RFC 3161 timestamp so the signatures stay valid after the certificate expires,
and checks each signature. It needs:

- the plug-in, `Azure.CodeSigning.Dlib.dll`, from the
  `Microsoft.Trusted.Signing.Client` NuGet package: `-SigningDlib` or
  `NORTHLIGHT_SIGNING_DLIB`;
- a metadata file naming the account: `-SigningMetadata` or
  `NORTHLIGHT_SIGNING_METADATA`:

  ```json
  { "Endpoint": "https://<region>.codesigning.azure.net",
    "CodeSigningAccountName": "<account>",
    "CertificateProfileName": "<profile>" }
  ```

- an Azure sign-in with the *Trusted Signing Certificate Profile Signer* role:
  `az login` on a PC, or `AZURE_TENANT_ID`, `AZURE_CLIENT_ID` and
  `AZURE_CLIENT_SECRET` for an app registration.

`signtool` comes with the Windows SDK in the Visual Studio C++ workload.

### Releases from GitHub

Pushing a tag `vX.Y.Z` or `vX.Y.Z-beta.N` runs `.github/workflows/release.yml`: it
checks the tag against the version in `CMakeLists.txt`, runs `package.ps1` with the
tag's label and attaches the zip, installer and checksums to a **draft** release to
be reviewed and published by hand. Running the workflow by hand makes a dev package
and keeps it as a workflow artifact.

The workflow signs once the repository variable `NORTHLIGHT_SIGNING` is `true`. It
then reads the account from the variables `TRUSTED_SIGNING_ENDPOINT`,
`TRUSTED_SIGNING_ACCOUNT` and `TRUSTED_SIGNING_PROFILE`, and signs in with the
secrets `AZURE_TENANT_ID`, `AZURE_CLIENT_ID` and `AZURE_CLIENT_SECRET`.

## Tests

```powershell
ctest --preset windows-debug
```

runs both test suites and the source checks, eight tests at a time, each stopped
and failed after five minutes so a hang names itself instead of blocking the run
(the test presets in `CMakePresets.json` set both; a test that must run alone, the
paint-time budgets, says so with `RUN_SERIAL`). The suites can also be run directly:

- `build/windows-debug/tests/unit/northlight_unit_tests.exe`: the model and the
  engine, with no JUCE and no audio device.
- `build/windows-debug/tests/unit_juce/northlight_juce_tests.exe`: JUCE-linked
  code, still without a device.

**When a test hangs or crashes,** it says where. Both suites include
`tests/unit/StackReport.cpp`, which prints the test case's name and the stack of
every thread, with file and line, then ends the process with a failure. It does
this when a test case crashes, calls `abort()` (as a failed real-time check does),
or is still running after 240 seconds (`NORTHLIGHT_TEST_HANG_SECONDS` changes the
limit; 0 turns it off). It also turns off Windows' error dialogs and its error
reporting for the suites, so a failing test never waits on either. CTest shows the
report as the test's output.

The report prints each frame first as `module+offset`, using nothing but the
system's unwinder: a hung thread can hold a lock the symbol library needs, and on
CI loading symbols once blocked until CTest's timeout. It then names the frames
with dbghelp, giving up after 30 seconds. Between the two passes it says which
thread holds Windows' loader lock, which loading and unloading a DLL needs; a holder
that no longer exists was killed while holding it, and the lock is never released.
When only the first pass made it, the offsets still lead to lines. A failed CI run uploads the test programs and their
PDBs as the `test-programs-<commit>` artifact; download it, then:

```powershell
& "<VS>\VC\Tools\MSVC\<version>\bin\Hostx64\x64\llvm-symbolizer.exe" --obj=northlight_juce_tests.exe --relative-address 0x1a2b3c 0x4d5e6f
```

**A hang that was fixed.** Now and then a JUCE test program hung after its test had
finished: `ScopedJuceInitialiser_GUI`'s destructor unloads Direct2D's DLLs, and
`FreeLibrary` waited for the loader lock for ever. The report's loader-lock line
showed why: its holder had been killed. JUCE kills a thread that misses
`stopThread`'s time limit, so no code stops a thread with one any more
(architecture.md, "Threads"). Until CI has run clean for a while, a release package
still gives a failed test a second run.

**Paced tests.** A test that runs in real time, and so needs a free core when it
wakes, carries the `[paced]` tag; the unit suite's tags are CTest labels. On a busy
machine such a test can fail for want of a core, so CI and `scripts/package.ps1`
run the other tests in parallel and then the paced ones one at a time:

```powershell
ctest --preset ci --label-exclude paced
ctest --preset ci --label-regex paced --parallel 1
```

The UI performance budgets ([ui-performance.md](ui-performance.md)) are in three
layers: the work budgets run with every `ctest`; the paint-time budgets are a
Release-only test labelled `perf`; the frame benchmarks run in a window against this
machine's baseline:

```powershell
.\scripts\build.ps1 -Preset windows-release -Test -Label perf
.\scripts\perf.ps1
```

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
with what each does is the table in `src/app/LaunchRun.cpp`; `Northlight.exe --help`
prints it. Work a flag leaves for later (a capture, a save, a pointer once the window
has settled) goes through one schedule that is dropped as the app shuts down.

### Listening to an instrument

`--render-phrase <type> <file.wav>` renders a fixed phrase through a built-in
instrument (single notes up the keyboard at three velocities, chords, a repeated
note, a run under the pedal) to a 24-bit WAV at 48 kHz, writes its cue sheet beside
it (`<file>.cues.txt`) and quits. Use the Release build: the phrase renders offline,
but you will want to listen to it. `scripts/analysis/ep-measure.py` measures the
render's notes (pitch, harmonics, bell, decay and damping) and compares them with
reference recordings:

```powershell
Northlight.exe --offscreen --render-phrase northlight.lumen "$env:TEMP\lumen.wav"
python scripts/analysis/ep-measure.py phrase "$env:TEMP\lumen.wav" --out lumen.csv
```

`scripts/analysis/lumen-calibrate.ps1 -Listing <notes.csv>` renders Lumen at a
reference set's notes and velocities (the hidden `[.calibrate]` test of the Release
unit tests), measures both and prints the difference by register and strike.

### Captures while the machine is in use

`--offscreen` runs Northlight with no window on screen at all: no splash, and no
native window behind the main window, Audio Setup, Preferences, plugin windows or
floating panels. They are still built, laid out, ticked and painted, so `--capture`
saves the same PNGs, but nothing can appear, take the focus or switch a virtual
desktop. Use it for captures while someone works or plays on the machine:

```powershell
Northlight.exe --offscreen --demo-song "$env:TEMP\demo.northlight" --play --master-gain -120 --capture "$env:TEMP\shots" 6
```

`--screen-reader` makes the views expose their painted content (clips, notes,
markers, automation points) to UI Automation as they do when a screen reader is
running, so Inspect (from the Windows SDK) or a UI Automation script can read the
tree without Narrator speaking.

A capture starts from an empty settings folder, so it knows no scanned plugins.
`--capture-plugins` copies the user's `plugins.json` into it first; then `--insert
vst3:<id>` (the id as `plugins.json` gives it) adds a real VST3 to the capture.

(An earlier `--other-desktop` put the window on another virtual desktop. Windows
follows a window's activation to its desktop, and the app grabs the keyboard focus
in places, so it could switch the user's desktop; it was removed.)

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
| Design token overrides | `tokens.json` (yours) and `appearance-tokens.json` (Preferences) beside the settings file |
