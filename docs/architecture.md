# Architecture

How Northlight is put together: the modules, the threads, and the ideas each part
is built on. It describes the system as it stands, not the order it was built in;
the reasons behind individual choices are in [decisions.md](decisions.md), and the
rules every real-time path follows are in [rt-rules.md](rt-rules.md).

## Principles

- **The audio thread never waits.** No allocation, locks, file or message traffic on
  a real-time thread, enforced in debug builds by an allocation hook and a real-time
  section guard ([rt-rules.md](rt-rules.md)).
- **The engine renders an immutable snapshot.** The project is compiled on the
  message thread into a self-contained graph and published atomically; edits never
  touch what the audio thread is reading.
- **Every edit is a command.** The model changes only through commands with an
  exact undo, and the engine learns about a change from the command's kind.
- **The core and the engine know nothing of JUCE.** JUCE is used where it is
  strongest (devices, plugin hosting, UI) and kept out of the model and the DSP, so
  both are unit tested without it.
- **Positions are samples.** The timeline is in samples at the project rate; bars,
  beats and tempo are a view of it, through the tempo map.
- **Design tokens, not literals.** Every colour, size and font in the UI comes from
  `docs/design/tokens.json`; a check fails the build when `src/ui` carries a literal
  colour.

## Modules

```
src/core        The project model, commands and undo, the tempo map, comping,
                JSON project files with versioned upgrades. No JUCE.
src/engine      The audio engine: snapshot compiler, renderer, worker pool,
                transport, streaming and disk thread, recording, peaks, MIDI
                scheduling, delay compensation, automation playback, metering and
                loudness, time-stretch, offline export DSP. No JUCE.
src/engine/rt   Real-time primitives: the guard and allocation hook, SPSC queue,
                snapshot publisher, callback statistics, denormal flushing.
src/plugins     The built-in processors behind the engine's insert interface:
                utilities and effects, the channel strip modules and presets, the
                instruments (Aurora, Pulsar, Nebula, Starbit, Sampler). No JUCE.
src/hosting     VST3 hosting through JUCE, CLAP hosting against its C ABI, the
                plugin registry and blocklist, the out-of-process scanner.
src/audio       The device layer (ASIO, Windows Audio, MIDI devices), the audio
                callback, latency calibration, media loading and peak building.
src/ui          Theme (tokens), look and feel, icons, components, the shell
                (header, status bar, panel hosts) and every view and editor.
src/app         The application: main window, project session, settings,
                bounce and export jobs, MIDI files, crash reports, migration.
northlight_plugin_scan   Inspects one plugin file per process for the scanner.
```

| Target | Kind | Notes |
|---|---|---|
| `northlight_options` | interface | Compiler flags and definitions shared by every target |
| `northlight_rt`, `northlight_rt_checks` | static, object | Real-time primitives; the checks hold the global `operator new` hook, so they are an object library the linker cannot discard |
| `northlight_juce` | static | The JUCE modules compiled once and re-exported, so every consumer sees one consistent set of module settings |
| `northlight_core` | static | The model; nlohmann/json privately |
| `northlight_engine` | static | The engine; depends on core and rt only |
| `northlight_plugins` | static | Built-in processors |
| `northlight_hosting` | static | Plugin hosting and scanning |
| `northlight_audio`, `northlight_audio_device` | static | Pure audio code; the JUCE device layer |
| `northlight_ui` | static | Views and components |
| `northlight` | GUI app | The application |
| `northlight_unit_tests`, `northlight_juce_tests` | console | Catch2 suites without and with JUCE |
| `northlight_loopback_tool`, `northlight_record_soak` | console | Hardware and soak tools, built but never run by CTest |

## Threads

| Thread | Does | Talks to others through |
|---|---|---|
| Message (UI) thread | The model, commands, snapshot compiles, all UI, plugin instance creation and destruction, plugin editors | Publishes snapshots; pushes transport commands and live values; polls meters and feedback |
| Audio callback | Drains transport commands, renders the current snapshot, captures recordings, schedules MIDI | Atomics and SPSC queues only |
| Render workers | Render tracks in parallel within a block | One packed atomic per batch; a counting semaphore to wake |
| Disk thread | Keeps every streaming window filled ahead of the play position | Position atomics per window |
| Record writer | Drains recording rings to WAV files, cuts takes, rewrites headers | Rings, a gap queue and a take-mark queue |
| Background jobs | Peak building, plugin scanning, time-stretch renders, pitch analysis, bounces, file browsing, reading and decoding a project being opened | Results posted back to the message thread |

The audio callback and the workers are time-critical threads. Everything they touch
is preallocated when a snapshot is compiled or a device is prepared.

The callback thread belongs to the device driver, which sets its priority. The
render workers join the MMCSS "Pro Audio" task (`rt/ThreadPriority.h`), which puts
them in the real-time priority band where no ordinary thread can reach them; without
MMCSS they fall back to `THREAD_PRIORITY_TIME_CRITICAL`. The disk thread runs at
`THREAD_PRIORITY_HIGHEST`, above normal work and below the workers. Diagnostics
shows how many workers joined MMCSS.

## Model and commands

`core::Project` is plain data: channels (audio, MIDI, instrument, aux and folder
tracks; buses and FX tracks; the master), clips and notes, sends, inserts and the
channel strip, automation lanes, markers, the tempo and meter changes, take groups,
fader groups. Tracks and buses share one `Channel` type, so faders, routing, sends,
inserts and automation work the same on both. Some of it is how the views show it,
saved with the project: row heights, whether a channel's automation lanes are open
and each lane's height, collapse and hide. Commands that change only those are
*cosmetic*: undoable, with no engine work. In the arrange view the automation
lanes are part of their row's block, after the row and its take lanes
(`ArrangeView::rows()`), drawn and edited by `ui::AutomationLanes`.

Every change is a `core::Command` with `apply` and `revert`, kept by the
`core::UndoStack`, which groups commands into one step. Each command reports its
`ChangeKind`, which is how the engine learns what to do. `apply` and `revert`
return false, having changed nothing, when a command cannot run (what it names is
gone: a menu opened on a track an undo has since removed); the stack then drops it,
or leaves an undo or redo where it was, and a group puts back the members it had
already done. Most commands that set
one field of one object (a gain, a bypass, a lane's points) are thin subclasses of
`core::SetField<Target, &Object::field>`, which finds the target by id, keeps the
value it replaces and puts it back; the subclass only names itself and says what
it changes.

| Kind | Example | Engine work |
|---|---|---|
| Cosmetic | A row height, a folder opening, a name, a colour, a marker | None |
| Values | A fader or pan move | Push live values into the running graph |
| Parameters | An insert parameter | Set the processor's atomics |
| Structure | A clip, a route, an insert added | Compile and publish a new snapshot |

The views refresh separately from the engine: a values or parameters change only
updates values, and a command that changes labels (`rebuildsViews`: a name, a
colour, a marker) has them rebuild in full while the engine does nothing.

All edits go through `ProjectSession::run`: single commands, named groups, a
finished recording, a recovery, an automation pass. A slider drag is a gesture whose
commands become one undo step; inside it, a setter of the field the previous
command set merges into that command (`Command::mergeWith`), so a ten-second drag
keeps one command, not one per tick. The history is capped at 1000 steps and about
256 MB (each command estimates what it keeps, `Command::footprint`); a new step
past either drops the oldest, and a save point dropped with them can no longer be
reached, so the project stays modified. A test applies, undoes and redoes every command and
compares the whole project each time.

Project files are versioned JSON (`.northlight`). Each version upgrades step by step
from the oldest readable one. Processor parameters are saved with stable keys and
reconciled on load, so a processor can gain or reorder parameters without moving
saved values or automation. A built-in's key comes from its parameter's label unless
the parameter sets an `id` (`InsertProcessor::parameterKey`), so a label rename
would change the key: the renamed parameter takes its old key as its `id`. A test
pins every built-in's keys, so a rename that forgets fails it. Each built-in's
parameter table is filled by the `Param` enum its processor reads
(`p[threshold] = ...`), and a test checks every table's size against its enum.

## The engine

### Snapshots

The controller compiles the project into an immutable `Snapshot`: channel nodes with
mute and solo resolved, clips bound to sources (a muted clip is left out, so the
audio thread never sees clip mute), the master's inserts, fader and automation
(all through the master's one channel runtime: the fader's target and ramp, and the
volume lane that overrides it while it plays), insert chains bound to cached
processor instances, delay lines, automation, the tempo map, MIDI feeds. It is
published through `rt::SnapshotPublisher`. The audio thread advertises the epoch it
is using, and the controller frees superseded snapshots only once that epoch has
moved on.

State that must outlive snapshots lives in runtime caches keyed by model id: insert
instances (filter memory, envelopes), delay lines, per-channel live values and
automation state, per-track MIDI state. A recompile therefore never resets a
processor, empties a delay line or drops a held note.

### Rendering

Each block the renderer splits time at loop ends, punch edges and the count-in's end
so every boundary is sample-accurate. Tracks render pre-fader in parallel on the
worker pool, then the callback thread sums in a fixed order: track faders, sends,
buses in dependency order, the master. The fixed order makes the output
bit-identical whatever the number of workers.

Faders, pans and parameters are live values: targets the renderer ramps towards
across a block, so a move glides and needs no recompile. Automation evaluates each
lane at the span's end and drives the same targets; hosted plugin parameters are
automated from the message thread at 50 Hz, since plugins own their parameters.

Solo is in place and resolved at compile: a channel is heard if it is soloed, feeds a
soloed channel or is fed by one, and a channel heard only through a send keeps the
send but not its direct output. Folders fold their mute and solo into their tracks.
Routing cycles are refused when the edit is made.

### Delay compensation and sidechains

Each channel's chain reports its latency. At compile time the graph is solved so that
every edge (an output or a send) gets a fixed delay for the difference between what
its destination waits for and what its source already lags. Lines are cached by edge
across recompiles, and a changed delay starts a new line pre-filled from the old one,
so edits never cause a gap. Bypass keeps a processor's latency, so toggling it never
re-solves the graph.

Sidechain keys can tap a source before or after its fader and are delay compensated.
Tracks render in dependency levels so a keyed track sees its key source finished in
the same block.

### MIDI and instruments

MIDI tracks hold notes and controller events and play an instrument channel. The
renderer schedules each span's events at exact sample offsets. A per-track runtime
detects any discontinuity (stop, locate, loop wrap), ends held notes and chases
controllers to their correct values. Live input from devices is timestamped on
arrival and placed within the block it lands in, giving a constant one-block delay
instead of jitter. Instrument channels merge the events of every MIDI track that
plays them. Multi-output instruments expose extra stereo pairs as their own tracks.
A MIDI track can also drive an external device through the MIDI output queue.

### Tempo

`core::TempoMap` turns the tempo and meter changes (steps and linear ramps) into
conversions between samples, quarter notes, bars and beats, the editing grid and the
click. Changing the tempo remaps every musical position so it keeps its bar and beat;
audio clips that follow the tempo are re-stretched. Each insert receives a play head
with the tempo, signature and position for its block.

### Media

Files up to 30 seconds are decoded into memory; longer ones stream through a private
window per clip, which the disk thread keeps filled ahead of the play position. A
read outside the window returns silence and counts an underrun rather than wait.
An underrun after a locate is expected while the window is rebuilt. A *starvation*
is an underrun in steady playback: the window had filled to its read-ahead since the
last rebuild and playback ran off its end. Starvations are counted once per gap,
summed by the disk thread across every clip (including deleted ones), and added to
the status bar's dropout count.
Sample-rate conversion on load is a stateless Hermite resampler with no lookback
delay, so imported media stays aligned to the sample.

Waveform peaks are min/max mipmaps built on a background thread at the project rate
and cached beside the media. The finest level is 256 samples per bin, so zoomed in
past that the peak store also decodes the visible part of the file in 65536-sample
blocks on the same thread, ahead of any peak build, and the views draw those samples
instead (`WaveformView`). Decoded blocks are held in memory up to 64 MB; one drawn in
the last two seconds is never evicted, and a new block that would need to is dropped
(the view keeps drawing peaks there), so loading can never loop. Time-stretch and transposition render in the background
with Signalsmith Stretch; playback starts on the part already rendered. Vocal pitch
editing analyses a clip in the background and re-renders edited phrases with an
in-house PSOLA renderer.

### Recording

A recording session is built on the controller: one ring and one target file per
armed input, handed to the record writer and then to the audio thread. A track's
input is a device channel or a stereo input bus. Buses are defined per device in
Audio Setup (`audio::InputBus`, stored in the audio settings) and tracks name
them; `ProjectSession` resolves the names against the enabled channels whenever the
device changes and hands the engine a name-to-channel-pair map, which
`compileSnapshot` uses to give each track node its left and right inputs. A stereo
track monitors through its pan as a balance, and its ring holds interleaved frames
that the writer puts in a two-channel file. The audio
thread only pushes samples into rings and marks events (take boundaries, gaps); the
writer drains the rings, cuts takes at the marked samples, fills dropped blocks with
silence so placement stays exact, and rewrites the WAV headers every second. A
sidecar beside each file describes where it belongs, so after a crash the next launch
can repair the files and put them back where they were captured.

The master is a `Channel` with a fixed id (`core::masterId`), so the insert
commands, the engine's instance cache and the editors treat its inserts like any
channel's. The snapshot compiles them into a chain node of their own
(`Snapshot::masterChain`) that the renderer runs in place on the summed mix before
the master fader; its latency adds to the output latency, so playback, recording
placement and export stay compensated. Master inserts take no key, and a freeze
renders without them.

Recordings are placed back by the measured round-trip latency (from loopback
calibration, stored per device, rate and buffer size) or the driver's reported
latency, plus the graph's output latency. Loop recording produces takes in a take
group, and comping is an ordinary set of crossfaded clips derived from the takes, so
the engine knows nothing about takes.

### Metering and statistics

Meters are per-channel peak-hold and RMS atomics the UI drains. Loudness follows
BS.1770 on the master: momentary, short-term and gated integrated. Every callback's
duration is recorded as a fraction of its deadline in a lock-free histogram, from
which the status bar shows percentiles, overruns and driver dropouts.

A processor's own readings for its editor (`engine::ProcessorFeedback`: levels,
gain reduction, analyser taps) are measured only while something shows them: its
editor, or the mixer's strip row, each setting its own watcher flag. A processor
nobody looks at spends nothing on its meters.

`rt::CallbackStats` also times when each callback starts, because a callback can do
its work quickly and still arrive late (another driver holding the CPU, power
saving, a USB stall), which is heard as a click with no overrun counted. It keeps
the time the next callback is due (the previous start plus the previous block's
length). A callback is never early, so an early one moves the due time back to it;
a slightly late one moves it a little, which absorbs drift between the audio and
CPU clocks; one half a block and at least 2 ms late counts as a late callback and
re-anchors the due time. The 2 ms floor is for USB drivers, which hand out small
blocks on the bus's 1 ms frames: at 64 samples every callback is up to half a block
off the ideal schedule without anything going wrong. A block that starts within a quarter of the previous block's length
is the same device period split up, or the driver catching up, and is not judged.
Late callbacks are shown in Diagnostics but not added to the status bar's dropouts,
since bursty drivers (Windows Audio in shared mode) can trip them without a gap.

Driver dropouts are the device's own count (`AudioIODevice::getXRunCount`), which is
-1 when the driver cannot report them, as most ASIO drivers cannot; Diagnostics then
says "not reported" rather than showing a reassuring 0. `AudioDeviceManager`'s count
is not used: it hides the -1 and adds its own callback-time overruns, which
`CallbackStats` already counts.

### Output tap

`--output-tap <folder>` attaches an `engine::OutputTap` to the callback, which hands
it every finished output block with its lateness and load. The audio thread pushes
the first two outputs into a `RecordRing` and a block record (frame count, sample
rate, lateness, load) into an SPSC queue, frames first, so the writer always finds
a record's frames; neither push allocates and a full ring drops the block, which is
logged. The writer thread writes `output.wav` (a new file when the sample rate
changes), runs `engine::GlitchDetector` over it and writes `events.csv`: jumps and
gaps found in the audio, and late callbacks and overruns, each with its time and its
offset in the device block. `summary.txt` is written when the tap stops.

`GlitchDetector` fits an order-16 linear predictor to each 1024-frame frame of each
channel and flags samples whose prediction error is over 12 times the frame's median
error (and over 0.03): most music is predictable at that order and a discontinuity
is not. A waveform with hard edges (a naive sawtooth or square) has a step every
period, so an error that repeats the shape of the error one period earlier (16
samples to 25 ms) is excused, except at multiples of the device block size, which
is what a block-boundary bug looks like. Instant attacks (a drum hit, a note with no
attack) are still reported: they are steps. Samples within 16 of an exact zero are
skipped, since the predictor has nothing to go on after silence; short runs of exact
zeros inside a signal are reported as gaps instead. A click Northlight made is in the file; one
that is not happened in the driver, the system or the hardware.

## Plugins

Every processor, built-in or hosted, sits behind one interface,
`engine::InsertProcessor`. The engine, the insert cache, the mixer and the editors
know nothing format-specific.

- **Built-ins** (`src/plugins`): utility and effect processors, the six channel strip
  modules, and the instruments. They share a base that crossfades bypass and keeps
  state running while bypassed, and they publish real-time feedback for their
  editors through rings of atomics: a five-value frame every 5 ms (input, output,
  reduction and two processor-specific values) and two sample taps. The larger
  effects (Glue, Patina, Exciter, Bloom, Tape Echo, Stereo Widener, Corona, and the guitar pedals
  Gravity, Flare, Comet, Tide and Polaris, declared together in `plugins/Pedals.h`) each have a file
  of their own and a card in `src/ui/strip`; what their frame values and taps carry
  is described at the top of each processor. Bloom's reverb core is
  `plugins/FeedbackNetwork`, for any reverb that wants one (Vesper next,
  [plans/vesper.md](plans/vesper.md)): eight modulated lines with an allpass each, a
  Householder mix, input diffusers, damping and low cut as shelves in the loop,
  freeze, shimmer and the energy governor. An effect gives it a `Tuning` (line and
  allpass lengths, how far the lines stretch, room for shimmer), sets it once a
  block and passes its own smoothed controls in each sample, so pre-delay, tone,
  ducking and mix stay the effect's. `plugins/VocalListener` hears a vocal for
  Vesper (voice and pitch, consonants and sibilance, breaths, line ends, the singing
  rate), deciding every 2.5 ms from the past alone; its pitch is YIN from
  `plugins/Yin.h`, which the tuner (Polaris) uses too. Vesper (`plugins/Vesper`) is
  the Listener in front of three prepared networks, one per space: each Listener
  frame sets the send's cut, the duck, Ends and Follow's high-pass, and the samples
  between follow them smoothly. Its card reads `vesper::Telemetry`, a frame every
  5 ms of what was heard and done (published only while an editor watches), as
  Patina's does; both keep their frames in an `rt::TelemetryRing`
  (`engine/rt/TelemetryRing.h`), a lock-free ring of float frames any number of
  readers follow with their own cursors. With Lookahead the Listener hears the input as it
  arrives while the rest runs on it delayed: frames wait in a fixed queue until the
  delayed audio reaches them (less a 6 ms lead), and a line end schedules its throw
  and Ends for when its last word gets there. A display that needs more than a frame
  holds (Patina's six stage readouts) publishes its own telemetry block the same
  way, so the shared frame stays small. Patina's card is a hardware face: the
  drawing shared by such faces (pointer knobs, printed legends, images from
  `assets/<effect>/`) is in `ui/components/HardwareParts`. Faces built from
  renders (the tape echo, Corona, Patina) share three more layers: `HardwareParts` draws
  renders shrunk to the pixels they land on (`drawScaled`, `drawScaledRotated`,
  `drawPlate`) and holds the printed ticks, the legend font and the fit
  arithmetic; `ui/components/RenderedFace` is the face itself, drawn at one
  design size and scaled to fit, its art cached once per scale and its words
  printed straight to the screen at every paint. Words are costly to set (each
  is measured by its glyphs' outlines), so a repaint only sets those in the area
  being repainted (`drawCrispText` skips the rest, and a face may skip a whole
  control's print), a knob that turns repaints only round itself, and hovering
  repaints nothing at full size; and `ui/strip/RenderedCard` is
  the card round it, sizing itself to the face and binding rendered knobs and
  levers (`HardwareLever`) to parameters; its `repaintChanged()` turns a parameter
  change into repaints round the knobs whose print moved, so neither a turned knob
  nor the model's refresh repaints the whole face. `ui/PaintStats` counts the words
  a face sets and its art rebuilds for the UI work budgets
  ([ui-performance.md](ui-performance.md)). `HardwareKey` is the bakelite key. Every built-in effect's
  factory presets are in `plugins/EffectPresets` (by parameter name, resolved
  against the effect's parameter table); Patina and the instruments build theirs
  by parameter index with `PresetBuilder` (`plugins/ParamHelpers.h`, beside the
  `choice`, `onOff` and `percent` parameter helpers), and both report a value out
  of range through `factoryPresetErrors()`; `ui/strip/EffectPresetLibrary` adds the
  user's own (JSON files keyed by parameter name) and tells every `PresetBar` when
  the list changes, so the card's header, the window's and a face's own control
  agree.
- **The DSP kit** (`src/plugins/dsp`, namespace `plugins::dsp`) holds the small
  header-only building blocks the built-ins share, all real-time safe: `Random.h`
  (xorshift32 and LCG noise with the float mappings the processors use, and
  `Drift`, a smooth random curve), `Smoothing.h` (one-pole coefficients by name:
  `poleFor…`/`stepFor…` for a time constant in seconds or milliseconds, a corner
  frequency or a once-a-block update, and `decay60ForMs`), `DelayLine.h` (a
  power-of-two ring with linear and Hermite reads, the reads on a caller's own
  ring, and the kernels), `Svf.h` (the trapezoidal state-variable filter),
  `PolyBlep.h` (the band-limited saw and square), `SpringTank.h` (a spring's
  dispersive loop, tuned by a `SpringTuning`), `Shapers.h` (`logCosh`, the
  biased-tanh `tube` curve and their antiderivatives, and the `adaa1`
  anti-aliasing step) and `Denormals.h` (`flush`, a state to zero below a
  tolerance, which `Biquad::flushDenormals` uses). Each processor keeps its own
  seeds, constants and tolerances, so moving onto the kit never changes its sound.
- **The guitar pedals** share one card, `ui/strip/PedalCard`, which reads a
  pedal's layout (its finish, knobs, toggle, and whether it is the tuner) from a
  table, and Corona's rendered footswitch, jewel and toggle. Flare uses the
  `Oversampler` below. Polaris publishes what it reads through three atomics
  (`polaris::readingOf`), not the feedback frame, as a pitch does not fit a
  frame's largest-of-the-frame rule.
- **Corona, the amp** (`plugins/Amp`) is built from shared parts: `Oversampler` (cascaded polyphase IIR half-bands, 2x to 8x,
  minimum phase, so no latency), `Triode` (a stage's transfer curve solved once
  from Koren's equations on its load line, with grid conduction, read from a table
  with antiderivative anti-aliasing), `ToneStack` (the passive tone stack's exact
  third-order transfer function from a nodal solve, discretised by the bilinear
  transform) and `Convolver` (zero-latency uniform-partitioned convolution: the
  first partition direct, the rest by FFT on signalsmith-linear, crossfading when
  the kernel changes). The cabinet (`plugins/Cabinet`) builds a minimum-phase
  response from a model of the speaker, the mics and the room. Building takes a
  few milliseconds, so it never runs on the audio thread: one shared background
  thread (`plugins/BackgroundJobs`) polls each amp about every 15 ms, rebuilds when
  the cabinet's parameters (moved by the card or by automation) differ from what it
  last built, and hands the kernel over through an `rt::SnapshotPublisher`. The
  published slot also holds the previous kernel, so the convolver can fade from it
  without the audio thread ever owning or freeing one. A user's response file is
  the insert's state (the Sampler's encoding of a path), decoded by the Sampler's
  loader.
- **VST3** is hosted through JUCE. **CLAP** is hosted directly against its C ABI,
  following its thread rules, with parameter events queued into the next block.
- **Scanning** runs one plugin file per child process with a timeout. A crash, hang
  or bad result blocklists the file with its reason, so a bad plugin cannot take the
  application down while it is inspected. The registry records modification times,
  so a rescan only inspects new or changed files.
- **Crash containment.** A hosted plugin's processing call runs under structured
  exception handling. A plugin that crashes is never called again, its slot passes
  audio through, and the user is told to save and restart.
- **State.** A hosted plugin owns its parameters and its state. The model keeps the
  plugin's state blob and only the parameters a command touched, so the plugin's own
  presets survive edits elsewhere.

Plugin instances are created and destroyed on the message thread. A plugin window
shares ownership of its instance, so the engine dropping it can never free it under
an open editor.

## Offline rendering and export

A bounce runs its own engine over a copy of the project, with media loaded whole, on
a background thread, as fast as it can. Export then converts the sample rate with an
offline windowed-sinc converter, measures integrated loudness and true peak,
normalises, and dithers (TPDF, optionally noise-shaped) to 16 or 24 bits. Stems are
normalised together so they still sum to the mix. Freezing a track uses the same
render and plays the result in place of the track's instrument and inserts.

### Opening a project

A project opens in steps so the window keeps painting (`ProjectSession::beginOpen`
and `stepLoad`; `app/ProjectLoad`). A thread of its own reads the file, parses and
migrates it, and decodes each audio file once at the project's rate through the media
loader, while the current project stays live. On the message thread a timer then calls
`stepLoad`: it swaps the project in (the commit point; until then the load can be
cancelled), hands the decoded sources to the engine's media cache, and makes the
processors one plugin per step (built-ins in batches within a 12 ms budget) through
the engine's insert cache, so the window can repaint between them. The window takes
no step that makes a third-party plugin until its loading sheet is fully on screen
(`loadHasSlowStepsAhead`): its appear delay and entrance run on timers, which cannot
fire while a plugin holds the thread. The final step
compiles the snapshot, which finds every source and processor already made. The step
holds what it made until then, so garbage collection cannot drop it. `open()`,
`newFromTemplate()` and `restoreAutosave()` run the same steps to the end at once.
Measured on a 40-track project with eight UADx plugins (debug build): reading 50 ms,
audio 3.6 s, plugins 6.8 s (about 850 ms each), snapshot 25 ms. Before, all of it
blocked the message thread; now the longest stall is one plugin.

## Application services

- **Settings** are an XML properties file in `%APPDATA%\Northlight`. Design tokens can
  be overridden from a `tokens.json` beside it, which only the user edits; the accent
  and contrast chosen in Preferences go in `appearance-tokens.json` beside it, applied
  after it.
- **Autosave** writes unsaved projects to the settings folder while edits pause. A
  normal close removes them, so anything left at launch is offered back. The plugin
  states and parameter keys are taken on the message thread; the copy is turned into
  JSON and written on a worker, and a save or close waits for a write in flight.
- **Templates** are project files without clips. **Media management** gathers outside
  media into the project folder and moves unused files to the Recycle Bin.
- **Crash reports** are minidumps written to the settings folder from buffers
  prepared at start-up. Nothing is sent anywhere.
- **Migration** brings settings and folders across from the project's earlier name,
  Aural, once.

## User interface

`ui::Theme` loads the design tokens: embedded at build time as the defaults, shipped
beside the executable, and overridable per user. `ui::NorthlightLookAndFeel` draws
every stock control from the tokens, including the tactile layer of gradients, insets
and glows (rendered once per size and colour into cached images). Dedicated
components cover what stock controls cannot: fader, knob, meter, segmented control,
numeric field, toasts and sheets (dialogs without a modal loop;
`JUCE_MODAL_LOOPS_PERMITTED` is 0).

The main window draws its own title bar. The header bar answers Windows'
hit-testing, so snapping and resizing stay native. Below it are the arrange view, a
dock and a side panel, and the status bar. Every secondary view is a panel owned by
exactly one host at a time (the dock, the side panel or a floating window), so it
keeps its state across moves and can never be lost. Selection has one owner that
notifies one listener, which keeps every view on the same channel.

The specification for every screen is in [design/](design/README.md).

## Testing

| Suite | Covers |
|---|---|
| `northlight_unit_tests` | The model and every command's undo and redo, JSON round trips and upgrades, the tempo map, the engine offline (sample-exact playback, loops, routing, delay compensation, MIDI, recording, streaming, export DSP, pitch editing) under the real-time allocation hook |
| `northlight_juce_tests` | JUCE-linked code without a device: the audio callback through a fake device, media reading, plugin hosting against a test CLAP, themes, UI components, MIDI files |
| CTest checks | No literal colours in `src/ui`; formatting |
| Hardware tools | Loopback latency calibration on a real device; an offline recording soak (many inputs for many minutes through the real record path, verified sample-exactly) |

Tests never depend on audio hardware. See [CONTRIBUTING.md](../CONTRIBUTING.md) for
how to run them.
