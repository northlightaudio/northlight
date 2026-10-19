# Architecture notes

Living document. The intended architecture is in [PLAN.md](PLAN.md), section 3; this
file records what is actually built and the decisions made along the way.

## Current state (Phase 1)

```
aural.exe
  src/app         JUCEApplication; MainWindow (toolbar with device summary + "Audio
                  Setup..." button, placeholder body); UserSettings (the per-user
                  properties file, implements audio::AudioSettingsStore)
  src/ui          Theme (design tokens), AuralLookAndFeel (dark, token-driven),
                  AudioSetupView (the Audio Setup screen)
  src/audio       Pure:  AudioSettings, LatencyDetector, PassThrough
                  JUCE:  AudioDeviceLayer, AudioCallback, LatencyCalibrator
  src/engine/rt   Real-time guard: thread-local RT flag, violation handler,
                  debug operator new/delete hook

aural_unit_tests.exe
  tests/unit/rt      Catch2 tests for the guard
  tests/unit/audio   Settings serialisation, latency detection, pass-through
                     (including an allocation check under the RT hook)

aural_loopback_tool.exe
  tests/hardware     Console tool: list devices, open one, run the loopback
                     calibration. Built, never run by CTest.
```

Build targets:

| Target | Kind | Depends on |
|---|---|---|
| `aural_options` | INTERFACE | Compiler flags and defines shared by everything |
| `aural_rt` | STATIC | `aural_options` |
| `aural_rt_checks` | OBJECT | `aural_rt`; holds the global `operator new` override so it is linked into executables rather than discarded from a static lib |
| `aural_juce` | STATIC | The JUCE modules, compiled once; re-exports their include paths and settings (see decisions log) |
| `aural_audio` | STATIC | `aural_rt`; pure audio code with no JUCE dependency |
| `aural_audio_device` | STATIC | `aural_audio`, `aural_juce`; the device layer |
| `aural_ui` | STATIC | `aural_audio_device`, `aural_juce` |
| `aural` | GUI app | `aural_ui`, `aural_audio_device`, `aural_juce`, `aural_rt_checks` |
| `aural_unit_tests` | console app | Catch2, `aural_audio`, `aural_rt`, `aural_rt_checks` |
| `aural_loopback_tool` | console app | `aural_audio_device`, `aural_juce`, `aural_rt_checks` |

### Audio device layer

`audio::AudioDeviceLayer` is a message-thread-only wrapper around
`juce::AudioDeviceManager`. It enumerates device types (ASIO when the SDK was present
at build time, WASAPI shared/exclusive/low-latency always), opens a device from an
`AudioSettings` (type, device names, sample rate, buffer size, channel sets) through
`AudioDeviceManager::initialise` with a `DEVICESETUP` element so the whole change is
one open, and persists what the user chose through `AudioSettingsStore`.

Status (`AudioDeviceLayer::status()`) reports the open device, its format, the driver's
input and output latency in samples, the active channel indices in callback order,
xrun count and a human-readable message. The layer is a `juce::ChangeBroadcaster`;
the Audio Setup screen and the main window toolbar re-read status on every change.

Device loss and errors: `AudioDeviceManager` already reopens from its last explicit
settings (or a default device) when the device list changes under it. On top of that
the layer runs a 1 Hz health timer that (a) restarts the device when the callback has
raised the error flag, (b) retries the preferred settings every two seconds while no
device is open, and (c) after five failed retries opens the system default so the user
is not left in silence, while still polling for the preferred device.

### Audio callback

`audio::AudioCallback` is the only `juce::AudioIODeviceCallback`. Its first two
statements are `rt::ScopedRealtimeSection` and `juce::ScopedNoDenormals` (FTZ/DAZ).
Phase 1 behaviour: copy input channel `i % numInputs` to output `i` with a per-block
linear gain ramp (`renderPassThrough`), or silence when pass-through is off. Control
values are atomics; peak meters are atomics the UI drains. Nothing in the callback
allocates, locks or posts messages; the debug build's allocation hook is live for the
whole callback.

### Loopback calibration

`audio::LatencyCalibrator` (message thread) and the calibration section of
`AudioCallback` (audio thread) share a capture buffer through a three-state phase
(`idle`, `running`, `done`) held in an atomic. The message thread fills the plan and
buffers in `idle`, flips to `running`; the audio thread plays the 10 ms chirp from
`makeCalibrationSignal` on the chosen output at a known sample index while recording
the chosen input, mutes pass-through (a loopback cable would otherwise howl), and
flips to `done` when the one-second capture is full; the message thread analyses with
`detectLatency` (cross-correlation against the chirp, magnitude so an inverted cable
still matches, confidence = peak / RMS of the correlation) and flips back to `idle`.
Five trials are combined with `combineMeasurements` (median). The result is stored in
`AudioSettings::calibration` together with the device, sample rate and buffer size it
was measured at; `AudioDeviceLayer::currentCalibration()` only returns it when the
open configuration matches. Phase 3 uses that value to place recorded audio.

### Settings

`UserSettings` wraps `juce::ApplicationProperties`; the file is
`%APPDATA%\Aural\Aural.settings` (XML). Each subsystem writes namespaced keys
(`audio.*`) via `AudioSettings::toKeyValues()`, so the audio layer has no JUCE
dependency for serialisation and the format is trivially diffable.

### UI

`ui::Theme` is a plain struct of tokens (colours, spacing, radii, type sizes, control
sizes). `ui::AuralLookAndFeel` derives from `LookAndFeel_V4`, sets every colour id
from the theme and overrides the drawing of buttons, toggles, combo boxes, popup menu
backgrounds, linear sliders and scrollbars. Views keep a reference to the theme and
never hard-code a colour or a pixel size. All dialogs are secondary windows or async
launches; `JUCE_MODAL_LOOPS_PERMITTED` stays 0.

## Decisions log

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | C++20 + JUCE 8 over Rust | VST3 hosting and plugin-window embedding are mature in JUCE and immature in Rust; the hybrid (Rust engine, JUCE shell) stays an option after Phase 2. |
| 2026-10-19 | Own engine rather than Tracktion Engine | Licensing freedom and control of the threading/data model. Re-evaluate at the end of Phase 2 if the milestone slips. |
| 2026-10-19 | JUCE and Catch2 via `FetchContent`, no vcpkg yet | Nothing yet needs a package with a real build step. vcpkg (bundled with VS) comes in when spdlog or a resampler lands. |
| 2026-10-19 | RT checks via global `operator new` override in an OBJECT library | A static library's override can be discarded by the linker; OBJECT library objects are always linked. |
| 2026-10-19 | `JUCE_MODAL_LOOPS_PERMITTED=0` | Forces async dialogs from day one so the message thread never re-enters during a modal loop, which is a classic source of host/plugin deadlocks. |
| 2026-10-19 | JUCE modules compiled once into `aural_juce` and re-exported | JUCE modules are INTERFACE targets that add their sources to every consumer; with three consumers (app, device library, hardware tool) that means duplicate symbols. `aural_juce` links the modules PRIVATE and re-exports `INCLUDE_DIRECTORIES` and `COMPILE_DEFINITIONS` so consumers see one consistent set of module settings. |
| 2026-10-19 | Audio settings are a JUCE-free value type serialised to a flat key/value map | Lets the settings and the calibration record be unit tested without JUCE, and keeps the on-disk format simple. The app maps the keys onto its properties file. |
| 2026-10-19 | Device changes go through `AudioDeviceManager::initialise` with a `DEVICESETUP` element | `setCurrentAudioDeviceType` opens the type's default device first (and sleeps 1.5 s), then `setAudioDeviceSetup` opens the chosen one: two opens per change. The XML path applies type, device, rate, buffer size and channels in one open. |
| 2026-10-19 | Calibration signal is a 10 ms windowed chirp found by cross-correlation | Single-sample impulses are smeared by converter filters and easily lost in noise; a chirp survives band-limiting and its autocorrelation peak is one sample wide, so the match is sample-accurate on real hardware. Matching on magnitude tolerates polarity-inverting cables. |
| 2026-10-19 | Calibration capture buffer handed between threads by an atomic phase, not a queue | The data is one block of a second of audio moved once per trial; a lock-free queue adds nothing. The audio thread only touches the buffer in `running`, the message thread only in `idle`/`done`, and `audioDeviceStopped` resets to `idle` so a device restart cannot leave the buffer owned by a dead thread. |
| 2026-10-19 | Measured latency is stored with the device, rate and buffer size it was measured at | Round trip changes with all three; a stale number silently misplaces recordings. The UI shows "not measured for this configuration" rather than a wrong value. |
| 2026-10-19 | Theme tokens are a C++ struct for now, not a loaded JSON file | The Figma token export does not exist yet. The struct is the contract; Phase 7 adds the loader without touching the views. |
