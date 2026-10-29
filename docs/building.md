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
| `package` | Release | Warnings as errors; built by `scripts/package.ps1` into `build/package` |
| `ci` | RelWithDebInfo | Warnings as errors |

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
| `NORTHLIGHT_ASIO` | ON | ASIO audio devices (Windows only) |
| `NORTHLIGHT_ASIO_SDK_DIR` | empty | Optional: an ASIO SDK root whose header replaces the one bundled with JUCE |
| `NORTHLIGHT_VERSION_LABEL` | `dev` | The version's label: `dev`, a pre-release such as `beta.1`, or empty for a release (see [CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases)) |

## Packaging

`scripts/package.ps1` makes what people download: a portable zip and an installer,
both from the same files.

```powershell
.\scripts\package.ps1                        # a dev package, unsigned
.\scripts\package.ps1 -Label ''              # a final release (clean tree, at the tag)
.\scripts\package.ps1 -Label beta.1 -Sign    # a signed pre-release
```

It builds the `package` preset (Release, warnings as errors) in `build/package`,
which no other preset shares, runs the tests except the timed `perf` budgets,
copies the app folder to `build/package/stage`, signs the executables when asked,
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

The UI performance budgets ([ui-performance.md](ui-performance.md)) are in three
layers: the work budgets run with every `ctest`; the paint-time budgets are a
Release-only test labelled `perf`; the frame benchmarks run in a window against this
machine's baseline:

```powershell
.\scripts\build.ps1 -Preset windows-release -Test -Label perf
.\scripts\perf.ps1 -Project <file>
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
Northlight.exe --no-splash --open "<project>.northlight" --play --master-gain -120
```

opens a project, plays it and keeps it silent. The options open
windows and panels, build example material (tracks, instruments, automation, takes,
tempo changes), drive the transport and recording, and run benchmarks. The full list
with what each does is the table in `src/app/LaunchRun.cpp`; `Northlight.exe --help`
prints it. Work a flag leaves for later (a capture, a save, a pointer once the window
has settled) goes through one schedule that is dropped as the app shuts down.

### Captures while the machine is in use

`--offscreen` runs Northlight with no window on screen at all: no splash, and no
native window behind the main window, Audio Setup, Preferences, plugin windows or
floating panels. They are still built, laid out, ticked and painted, so `--capture`
saves the same PNGs, but nothing can appear, take the focus or switch a virtual
desktop. Use it for captures while someone works or plays on the machine:

```powershell
Northlight.exe --offscreen --open "<project>.northlight" --play --master-gain -120 --capture "$env:TEMP\shots" 6
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
