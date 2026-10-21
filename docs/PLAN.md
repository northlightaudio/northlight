# Aural — Development Plan

A native, high-performance digital audio workstation for Windows (first), built around
a lock-free real-time engine, first-class ASIO support, VST3 plugin hosting, and a UI
that is designed before it is coded.

This document is the working plan. It will drift as decisions get made; treat the
"Open decisions" section as the list of things that still need an answer.

---

## 1. Goals and non-goals

### Hard requirements (from the spec)

| Requirement | What it means in practice |
|---|---|
| ASIO compatibility | Native ASIO driver support with user-selectable buffer size, sample rate, and channel mapping. WASAPI (exclusive + shared) as the fallback so the app runs on machines with no ASIO driver. |
| VST support | Host **VST3** instruments and effects: scanning, insert chains, editor windows, state save/restore, plugin delay compensation. VST2 is excluded (Steinberg no longer licenses it to new hosts). CLAP is a cheap, worthwhile addition. |
| Highly performant | Glitch-free playback and recording at 64-sample buffers on a mid-range machine; multi-core graph processing; UI that never stalls the audio thread; cold project open in under a second. |
| Visually attractive, high design standard | A real design system (tokens, type, spacing, motion) designed in Figma before implementation; GPU-accelerated rendering; 60 fps meters and waveforms; high-DPI throughout. |
| Multitrack | Unlimited audio and MIDI tracks, buses, sends, groups, and per-track insert chains. |
| Multi-track recording | Arm any number of inputs and record them simultaneously, with input monitoring, correct latency compensation, punch in/out, and crash-safe disk writing. |

### Non-goals for v1

- macOS and Linux. The architecture keeps a platform seam (audio device layer, plugin
  window embedding, file dialogs) so a port is possible, but v1 ships on Windows only.
- Video, notation/score editing, cloud collaboration.
- A built-in plugin suite beyond a minimal set (gain, EQ, compressor, utility) needed to
  make the mixer usable without third-party plugins.
- VST2, AAX, AU hosting.

---

## 2. Technology decisions

### Language and framework: C++20 with JUCE 8

JUCE is the pragmatic choice because the three hardest platform problems are already
solved and battle-tested in it:

- **ASIO and WASAPI device I/O** (`juce::AudioDeviceManager`, with the Steinberg ASIO
  SDK supplied at build time).
- **VST3 hosting** (`juce::AudioPluginFormatManager`, `VST3PluginFormat`), including
  editor window embedding and state serialisation.
- **Plugin editor embedding** on Windows (HWND child windows), which is awkward to do
  from scratch and nearly impossible from non-native UI toolkits.

JUCE 8 also renders through Direct2D on Windows, which gives GPU-accelerated vector
drawing without writing a renderer.

**Rust was considered and rejected for v1.** `cpal` has ASIO support, but VST3 *hosting*
in Rust is immature (the ecosystem targets plugin authoring), and embedding plugin GUIs
is fragile. Rust may return for isolated components later (e.g. the plugin scanner).

### Engine: written in-house, not Tracktion Engine

Tracktion Engine (JUCE-based, open source) would get a working DAW running much faster.
It is rejected as the foundation because:

1. It is GPLv3 unless a commercial licence is bought, which decides the project's
   licensing before we have decided anything else.
2. "Highly performant" and "high design standard" both argue for owning the engine's
   threading model and the data model the UI binds to.
3. Its edit/clip model would shape the product more than we would like.

This is the single most consequential decision in the plan and is listed under Open
decisions. We should read Tracktion's source for ideas regardless.

### Supporting libraries

| Concern | Choice |
|---|---|
| Build | CMake 3.28+, Ninja, MSVC 2022 (`/std:c++20`), presets for Debug / RelWithDebInfo / Release. |
| Dependencies | JUCE via `FetchContent`; everything else via vcpkg manifest. |
| Audio file I/O | JUCE readers/writers for WAV, AIFF, FLAC, MP3 (decode), with libsndfile as the fallback for odd formats. |
| Resampling | r8brain-free or libsamplerate (quality for offline), JUCE's Lagrange for real-time varispeed. |
| Time-stretch | Rubber Band Library (GPL/commercial) or Signalsmith Stretch (MIT). Decide at Phase 9. |
| SIMD | `juce::dsp::SIMDRegister` where it fits, hand-written AVX2 paths for mixing and metering, with scalar fallbacks. |
| Lock-free structures | `farbot` or `moodycamel::ConcurrentQueue` for queues; `choc` for small RT-safe utilities; our own double-buffered state publisher. |
| Serialisation | Project file is JSON (via `nlohmann::json`), referenced media stays as files on disk. Versioned schema from day one. |
| Testing | Catch2 for unit tests, a loopback audio harness for integration tests, Tracy for profiling. |
| Crash reporting | Crashpad/Breakpad with symbol upload, opt-in. |
| Logging | spdlog, never called from the audio thread (RT thread posts to a lock-free log queue). |
| Plugin SDKs | Steinberg VST3 SDK and ASIO SDK, obtained by the developer and pointed to by CMake variables. They are **not** vendored into the repo; the licences forbid redistribution. |

---

## 3. Architecture

### 3.1 Process and thread model

```
+--------------------------------------------------------------+
| aural.exe                                                    |
|                                                              |
|  UI thread (message loop, 60 fps paint, user input)          |
|       |  commands (lock-free queue)       ^ state snapshots  |
|       v                                   | + meters         |
|  Engine controller (owns the model, applies edits,           |
|                     publishes immutable graph snapshots)     |
|       |  swap-in of new graph (atomic ptr) |                 |
|       v                                   |                  |
|  Audio callback thread (ASIO)  <------>  worker pool         |
|       |  render graph, no locks/allocs     (N-1 cores)       |
|       v                                                      |
|  Disk I/O thread  <-- ring buffers -->  (read-ahead, record) |
|                                                              |
|  Background: plugin scanner results, waveform peak builder,  |
|              autosave, analysis                              |
+--------------------------------------------------------------+

+-------------------------+
| aural-scanner.exe       |  separate process: loads each plugin once,
| (plugin scanner)        |  reports metadata; a crash kills only itself
+-------------------------+
```

**Real-time rules** (enforced by code review and a debug-build allocation/lock detector):

- The audio callback and worker threads never allocate, lock, block, log, or touch the
  filesystem.
- All data the RT threads read is either immutable, atomically swapped, or owned by
  them. Edits from the UI arrive as commands; the controller builds a new graph
  snapshot and the RT thread picks it up at the next buffer boundary. Old snapshots are
  freed by a garbage thread, never by the RT thread.
- Plugins are called only from RT threads, and their editors only from the UI thread.
- Denormals are flushed (FTZ/DAZ) on every RT thread.

Plugins run **in-process** in v1. Out-of-process plugin sandboxing is a known
reliability win (Bitwig does it) but roughly doubles the hosting work; it is deferred,
and the plugin API boundary is kept narrow so it can be introduced later.

### 3.2 Audio graph

- Nodes: track input, clip player, insert (plugin or built-in), send, bus, master,
  hardware output. Each node declares latency; the graph solves plugin delay
  compensation globally.
- Scheduling: the graph is topologically sorted into levels; independent nodes at the
  same level are dispatched to the worker pool via a lock-free job list. The callback
  thread participates as a worker so a single-track project never pays thread hand-off
  cost.
- Buffer sizes: the engine processes in the device's block size but supports
  sub-block splitting at automation points and clip boundaries for sample accuracy.
- Sample rate: the engine runs at the device rate; media at other rates is resampled on
  import or streamed through a resampler, user's choice.

### 3.3 Data model

- `Project` -> `Track`[] -> `Clip`[] (audio, MIDI) plus `Lane`s for takes.
- `Track` has an input source, an insert chain, sends, output routing, and automation
  lanes.
- Every model edit is a command object with `apply`/`revert`, giving undo/redo for free
  and a natural audit stream for autosave.
- The model is UI-thread owned. The engine never reads it directly; it reads compiled
  snapshots.

### 3.4 Recording path

1. Arming a track opens its input channels and starts input monitoring (direct or
   through the insert chain, user choice; the UI shows the resulting monitoring latency).
2. On record, the RT thread writes input blocks into a per-track ring buffer; the disk
   thread drains to a temp WAV in the project's media folder with periodic header
   fixups so a crash loses at most a few seconds.
3. Recorded audio is placed on the timeline shifted by the measured round-trip latency
   (input latency + output latency, confirmed by a one-time loopback calibration the
   user can run from Preferences).
4. On stop, the temp file is finalised and becomes a clip; punch regions and loop
   recording create take lanes.

### 3.5 UI layer

- JUCE components with a fully custom `LookAndFeel`, driven by a token file (colours,
  spacing, radii, type scale, motion durations) so the implementation mirrors the Figma
  design system one-to-one.
- Waveforms are drawn from a mipmapped peak cache (built once per file on a background
  thread, cached beside the media). Zoom level picks the mip; nothing is recomputed
  during scrolling.
- Meters and playhead update from a lock-free snapshot published by the RT thread,
  painted on a 60 Hz timer, with ballistics computed on the UI side.
- Plugin editors open in floating or docked native windows.
- High-DPI from day one; every asset is vector or multi-resolution.

---

## 4. Visual design process

The spec asks for a high level of design, which means design cannot be a late polish
pass. The process:

1. **Phase 0:** write a one-page product brief (who it is for, what the first-run
   experience feels like) and a mood board. Study DAWs that get this right (Bitwig,
   Ableton Live 12, Logic, Studio One) and plugins with strong visual identity.
2. **Design system in Figma before any screen:** colour (dark theme primary, light theme
   later), type scale, spacing grid, corner radii, elevation, iconography, motion.
   Export tokens to the JSON file the `LookAndFeel` consumes.
3. **Screens in priority order:** arrange view, mixer, transport, plugin browser,
   preferences/audio setup, record workflow. Each is designed in Figma, then
   implemented, then reviewed against the design with a side-by-side screenshot.
4. **Interaction design:** hover/press/drag states, keyboard-first operation, drag
   feedback, snapping visuals, zoom behaviour. Documented in `docs/design/`.
5. **Perf budget for the UI:** 16 ms frame at 4K with 64 tracks visible. Measured in
   CI on a reference machine once one exists.

---

## 5. Roadmap

Phases are sequential where there is a dependency, and each ends with a demonstrable
milestone. Estimates assume one experienced developer working full time; adjust as
needed.

### Phase 0 — Foundation (2 weeks)

- Repo, CMake, vcpkg manifest, JUCE fetched, SDK path variables, CI (GitHub Actions,
  Windows runner building all presets and running unit tests).
- Coding standards and the real-time rules document.
- Debug-build RT-safety checker (allocation/lock hooks that assert on RT threads).
- Product brief, mood board, Figma file started.

**Milestone:** an empty window opens, CI is green, `docs/` has the RT rules and brief.

### Phase 1 — Audio device layer (2 weeks)

- Device enumeration and selection for ASIO and WASAPI; buffer size and sample rate
  controls; channel naming; ASIO control panel launch.
- Audio setup screen (first designed screen).
- Pass-through from input to output; measured latency displayed.
- Loopback latency calibration tool.

**Milestone:** select an ASIO device, hear your input at 64 samples, see the measured
round-trip latency.

### Phase 2 — Engine core and playback (4 weeks)

- Project/track/clip model with command-based undo.
- Graph compiler, snapshot swap, worker pool, RT job scheduler.
- Clip player with disk streaming (read-ahead ring buffers) and peak cache builder.
- Transport: play/stop/locate, loop, tempo and time signature, sample-accurate
  position, bars/beats and timecode display.
- Arrange view with tracks, clips, waveforms, playhead, zoom and scroll.

**Milestone:** import 32 WAVs onto 32 tracks and play them glitch-free at 64 samples
while scrolling and zooming.

### Phase 3 — Multitrack recording (3 weeks)

- Input routing per track, arm, input monitoring, software and hardware monitor modes.
- Disk writer thread, crash-safe temp files, latency-compensated placement.
- Punch in/out, loop recording into take lanes, count-in, pre-roll, metronome.
- Record workflow screen states (armed, recording, monitoring indicators).

**Milestone:** record 16 inputs simultaneously for 30 minutes with zero dropouts;
kill the process mid-record and recover the audio.

Status 2026-10-21: take groups and comping in the model (core/Comping.h):
takes, comp segments materialised as crossfaded clips, commands with undo for
swipe, whole take, delete, boundary move, flatten, rename, lanes and move to new
track; project schema 6 (design/14-comping.md).

Status 2026-10-21: loop recording, punch recording and recording over a take
group make takes in a group (newest playing) instead of stacked clips; MIDI
loop recording still makes a clip per pass.

Status 2026-10-21: take lanes in the arrange view: disclosure chip, lanes with
the comp lit, swipe comping, comp boundary drags, lane header and menus, Alt+Up /
Alt+Down / Alt+T (design/14-comping.md); a comp moves and deletes as one with its
takes. MIDI take lanes are not built.

### Phase 4 — Mixer and routing (3 weeks)

- Gain, pan, mute, solo (with solo-in-place), phase, track colour and naming.
- Buses, aux sends (pre/post), groups, master.
- Peak/RMS/LUFS metering from the RT snapshot.
- Built-in utility plugins: gain, EQ, compressor, limiter, to make the mixer useful.
- Mixer view.

**Milestone:** full signal flow from tracks through sends and buses to master with
accurate metering.

Added 2026-10-20: FX tracks (buses fed by sends, shown in the arrange view) with
a built-in Reverb and Delay.

Added 2026-10-20: a Cubase-style channel strip on every track and bus (gate,
parametric EQ with analyser, compressor, de-esser, saturation, limiter), switched
on in place and edited in the Editor panel with real-time displays; see
design/12-channel-strip.md and architecture.md.

### Phase 5 — VST3 hosting (4 weeks)

- Out-of-process scanner with blocklist and incremental rescans.
- Insert chains with per-slot bypass, drag reorder, wet/dry.
- Editor windows (floating, always-on-top option, remembered positions).
- Parameter model, state save/restore in the project file, preset browsing.
- Plugin delay compensation across the whole graph.
- Sidechain inputs.
- Plugin browser screen.

**Milestone:** load a commercial VST3 effect and instrument on tracks, save, reopen,
and the project sounds identical; a crashing plugin in the scanner does not take down
the app.

### Phase 6 — Editing (3 weeks)

- Clip move/trim/split/join/duplicate, fades and crossfades, gain handles, normalise.
- Snap grid, ripple edit, multi-select, lasso, keyboard-first editing.
- Markers and regions.
- Audio export/bounce (offline render of master or stems).

**Milestone:** edit a full song from raw takes to a bounced mix without leaving the app.

Status 2026-10-21: export is built: an export sheet (mix,
every track's stems or the selected tracks'; project, loop or a marker region),
WAV/AIFF/FLAC/Ogg Vorbis at 16/24-bit or 32-bit float, offline windowed-sinc
rate conversion, TPDF or noise-shaped dither, and loudness normalisation to a
LUFS target under a true-peak or sample-peak ceiling (architecture.md, Export).
Not yet checked by ear beyond the unit tests; MP3 is not offered.

### Phase 7 — Design fidelity pass (3 weeks)

- Implement every screen to the Figma spec; motion, hover states, empty states, drag
  previews, tooltips.
- Light theme if desired; user colour accent.
- Accessibility baseline: keyboard navigation, screen-reader names on controls,
  contrast checked.
- Frame-time profiling at 4K; fix anything over budget.

**Milestone:** screenshots match the design; 60 fps with 64 tracks at 4K.

### Phase 8 — MIDI and instruments (4 weeks)

The spec says "support VSTs", and most VST instruments need MIDI. This is the first
phase that could reasonably be cut from v1 if audio-only is acceptable.

- MIDI device I/O, MIDI tracks and clips, piano roll editor, quantise.
- VSTi hosting with multi-output instruments.
- MIDI recording with the same arm/monitor/punch model.

**Milestone:** record a MIDI performance into a VST instrument, edit it, bounce it.

Status 2026-10-19: device I/O (inputs, output, hot-plug), model, commands and JSON,
note scheduling with note-offs on stop/jump/loop, the built-in synth, VST3 and
CLAP instruments, MIDI recording with takes and latency placement, MIDI clips in
the arrange view and the piano roll are built and tested offline (see
architecture.md). Multi-output instruments feed aux tracks. The piano roll
auditions notes through the clip's track, and a MIDI track can play the MIDI
output device on a channel (2026-10-20).

Added 2026-10-21: a built-in Sampler instrument (one sample across the keys,
loop with crossfade, one-shot, filter, glide; the file kept in the insert's
state). Right-click an audio clip, Open in Sampler, to play it from a new MIDI
track. The Browser panel has a Files tab: places (project, Music, added
folders), search, click to audition through an engine preview, drag to the
arrange view or a Sampler. See architecture.md and design/07-plugins.md 7.4f-g.
Status 2026-10-21: MIDI clips hold controller events (CC, pitch bend, channel
and poly pressure) with bulk undoable edits and JSON (project version 6). The
engine schedules them, chases each controller's value after a jump, loop or
start, rests bend and pedals on stop, records them from live input, and
delivers them to VST3, CLAP, the MIDI output and the built-in synths (bend, mod
wheel vibrato, sustain pedal). The piano roll's lane switches between velocity
and a controller (draw freehand or a line, select, drag, erase), and a Scale
toggle highlights the project key on the keys and rows. Standard MIDI Files
import (types 0 and 1, by drop or menu, through the file's tempo map) and
export (type 1, 960 PPQ).

### Phase 9 — Automation, stretch, polish (4 weeks)

- Automation lanes (read/write/touch/latch) for every plugin and mixer parameter.
- Time-stretch and pitch-shift for clips; tempo mapping.
- Project templates, autosave and crash recovery, recent files, media management
  (consolidate, clean unused).
- Installer, code signing, crash reporting, telemetry opt-in, update checks.

**Milestone:** public beta candidate.

Status 2026-10-20: recent files existed already; autosave with crash restore is
built (architecture.md, Session and UI). Automation lanes with read, touch, latch
and write are built for gain, pan, built-in and strip parameters and hosted
plugin parameters (VST3 and CLAP, including moves made in a plugin's own window). Project templates and media management
(gather into the project folder, remove unused) are built, and a crash leaves
a local minidump that the next launch points to (sending reports is not built).

Status 2026-10-21: folder tracks (nested, with mute and solo for what they hold,
open and closed in the arrange view) and track freezing (a background render of
the track before its fader, played instead of its chain) are built
(architecture.md, "Folder tracks and freezing").

Status 2026-10-21: tempo mapping and time-stretch are built: a tempo track with
tempo changes, ramps and time signature changes (MIDI, markers, automation and
tempo-following clips keep their bars and beats through tempo edits), clip
time-stretch and transposition rendered in the background with Signalsmith Stretch
(MIT), Alt+drag to stretch, and "Stretch to project tempo" with a tempo guessed from
the file name, the beats and the length (architecture.md, "Tempo track and
time-stretch").

### Phase 10 — Beta hardening (ongoing)

- Soak tests: 24-hour playback, 1000-plugin scans, 200-track projects.
- Driver matrix testing (Focusrite, RME, UA, Steinberg, ASIO4ALL, FlexASIO).
- Performance regression suite in CI.

### Phase 11 — Vocal pitch editing

Melodyne-style editing of monophonic vocal clips. The design and the reasoning are
in [design/13-pitch-editing.md](design/13-pitch-editing.md).

1. Audio clip editor panel (waveform, gain, fades), opened from an audio clip like
   the piano roll.
2. Pitch analysis (pYIN pitch curve, pitch marks, notes, breaths) in a background
   `PitchStore` with a sidecar cache.
3. `VocalEdit` on audio clips: notes and their parameters in the model, with
   commands and JSON.
4. In-house PSOLA renderer behind a `PitchEditedSource`, re-rendering changed
   phrases in the background; bounce renders them first.
5. Pitch mode UI: note blobs, pitch curves, pitch, split, drift, vibrato,
   transition and breath tools.
6. Correct to key: a project key and scale, key detection, and the correction
   pass that keeps vibrato and slides.
7. Breaths: mute or reduce, in bulk or one by one.

**Milestone:** a vocal tuned to the key that sounds sung, not snapped, with the
unedited parts bit-identical to the original.

Status 2026-10-20: all seven steps built on the `pitch-editing` branch (tests in
tests/unit/engine/PitchTests.cpp). Not yet checked by ear on a real vocal
recording; the checks so far use synthetic voices.

Rough total: about 8 months of focused work to a beta, excluding the design time itself.

---

## 6. Performance strategy

- **Measure first.** Tracy instrumentation on every RT callback from Phase 2. Track
  callback time distribution (p50/p99/max) and dropouts in a diagnostics panel that
  ships in the product.
- **Multi-core by default.** The graph scheduler is built in Phase 2, not retrofitted.
- **Memory.** Preallocated buffer pools sized at graph compile time. No `new` on RT
  threads, enforced by the debug checker.
- **Disk.** Read-ahead sized from buffer size and track count; recording writes in
  large aligned chunks; all media I/O is on one thread with explicit priorities.
- **UI.** Waveform mip cache, dirty-rect repainting, meters on a separate timer from the
  arrange view, no layout work on paint.
- **Startup.** Plugin scan results cached; project open streams peaks lazily; aim for
  under one second to an interactive window.
- **Reference machines.** Define a minimum (e.g. 4-core, 8 GB, integrated graphics) and
  a target (the dev machine) and test both.

---

## 7. Testing strategy

| Layer | Approach |
|---|---|
| DSP and model | Catch2 unit tests; golden-file tests for mixing math, PDC, fades. |
| RT safety | Debug hooks assert on allocation/locks on RT threads; CI runs the engine under them. |
| Engine integration | Virtual audio device that runs the engine offline faster than real time; assert sample-exact output for scripted sessions (record, edit, bounce). |
| Plugin hosting | A set of free VST3s (Surge XT, Vital, Valhalla Supermassive, TDR Nova) in a CI cache; load/save/compare round trips. |
| Hardware | Manual matrix of ASIO drivers; loopback latency test automated where a cable can be looped. |
| UI | Screenshot comparison against design exports for key screens; manual review of motion. |
| Performance | Benchmarks for graph render at 8/32/128 tracks; regression threshold in CI. |

---

## 8. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Engine from scratch takes longer than estimated | Schedule slip | Phases 2-3 are the critical path; re-evaluate Tracktion Engine at the end of Phase 2 if the milestone is not met. |
| JUCE UI feels generic despite custom styling | Fails the design requirement | Token-driven `LookAndFeel` from Phase 1; design review each phase; escape hatch is a custom Direct2D/Skia renderer behind the same component API. |
| ASIO driver quirks (sample-rate switching, control panel reentrancy, channel limits) | Dropouts, crashes | Driver matrix testing from Phase 1; defensive device re-open logic; ASIO4ALL and FlexASIO as worst-case drivers. |
| Third-party plugins crash or misbehave in-process | App crashes | Scanner is out of process from day one; blocklist; crash reporter captures the plugin name; sandboxing is the long-term fix. |
| SDK licensing | Legal | ASIO and VST3 SDKs are never committed; licences are signed by the project owner; CLAP (MIT) is supported as the open alternative. |
| Scope creep into MIDI/automation before audio is solid | Nothing ships | Phases 8-9 are explicitly after the audio-only milestone; v1 can ship audio-only. |
| Single developer bus factor | Project stalls | Docs in `docs/` are kept current enough that a second person could start at any phase. |

---

## 9. Repository layout

```
aural/
+-- CMakeLists.txt
+-- CMakePresets.json
+-- vcpkg.json
+-- cmake/                 # toolchain helpers, SDK discovery (ASIO_SDK_DIR, VST3_SDK_DIR)
+-- docs/
|   +-- PLAN.md            # this file (moved here once the repo has structure)
|   +-- rt-rules.md        # real-time thread rules
|   +-- architecture.md    # living architecture notes
|   +-- design/            # design system tokens, screen specs, exported Figma frames
+-- src/
|   +-- core/              # model, commands, undo, project serialisation
|   +-- engine/            # graph, scheduler, transport, RT utilities
|   +-- audio/             # device layer (ASIO/WASAPI), file I/O, resampling, peaks
|   +-- plugins/           # VST3/CLAP hosting, scanner client, built-in plugins
|   +-- ui/                # components, LookAndFeel, views, theme loader
|   +-- app/               # main, window, preferences, command manager
+-- tools/
|   +-- scanner/           # aural-scanner.exe
+-- tests/
|   +-- unit/
|   +-- integration/
|   +-- bench/
+-- assets/                # icons, fonts, default theme tokens
```

---

## 10. Open decisions

These need an answer before or during the phase noted.

1. **Own engine vs Tracktion Engine** (Phase 0). Plan recommends own engine; see section 2.
2. **Licensing of Aural itself** (Phase 0). Proprietary, GPL, or dual? Affects the
   time-stretch library choice and whether Tracktion is viable.
3. **CLAP hosting** (Phase 5). Recommended yes; small cost on top of VST3.
   **Decided 2026-10-19: yes.** Hosted directly against the CLAP C ABI (JUCE does
   not host CLAP), behind the same insert interface, as "clap:<id>" types.
4. **MIDI in v1** (end of Phase 7). Audio-only is a legitimate v1; MIDI roughly adds a
   month. **Started 2026-10-19** (Phase 8 brought forward while Phase 7 waits for
   the design spec).
5. **Plugin sandboxing** (post-v1). Decide after seeing beta crash reports.
6. **Time-stretch library** (Phase 9). Rubber Band (best quality, GPL/commercial) vs
   Signalsmith (MIT, very good).
7. **Minimum hardware spec** (Phase 2). Needed to set the performance budget.
8. **Name and visual identity** (Phase 0). "Aural" is the working name.

---

## 11. Immediate next steps

1. Answer open decisions 1, 2 and 8.
2. `git init`, add this file, set up the Phase 0 skeleton (CMake + JUCE + CI).
3. Obtain the ASIO and VST3 SDKs from Steinberg and set the CMake variables.
4. Start the Figma file with the colour and type foundations.
5. Build the Phase 1 audio setup screen as the first real piece of both engine and
   design.
