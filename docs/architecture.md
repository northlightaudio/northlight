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
                versioned JSON project files. No JUCE.
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
                bounce and export jobs, MIDI files, crash reports.
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
it changes. A command whose `undoable()` is false (arming, input monitoring and a
track's audio or MIDI input) is applied by the session outside the history: it
marks the project changed (`changedOutsideHistory_`) and saves, but Ctrl+Z skips it.

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

Project files are versioned JSON (`.northlight`). There is no upgrade chain before
release: fields added since the oldest readable version are optional with defaults,
so an older file loads as it is. Processor parameters are saved with stable keys and
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
send but not its direct output. A solo-safe channel is heard whatever is soloed.
Folders fold their mute and solo into their tracks. Routing cycles are refused when
the edit is made.

A send's level, pan (a balance on what it sends) and mute are live values in its
`SendRuntime`: a mute ramps the level target to zero and back, so it never
recompiles. A channel on a hardware output (`Channel::hardwareOutput`, a stereo
pair of the device's active outputs) sums past the master into one of the
renderer's per-pair buffers (`Snapshot::maxHardwarePairs`, allocated in
`prepare`), at the span's offset in the block; they are added to the device
outputs at the end of the block. The compiler is told how many pairs the device
has (`AudioEngine::setOutputPairs`): a pair beyond them, and every one offline
(where it is 1), falls back to the channel's bus or the master, so an export
mixes them in.

### Delay compensation and sidechains

Each channel's chain reports its latency. At compile time the graph is solved so that
every edge (an output or a send) gets a fixed delay for the difference between what
its destination waits for and what its source already lags. Lines are cached by edge
across recompiles, and a changed delay starts a new line pre-filled from the old one,
so edits never cause a gap. Bypass keeps a processor's latency, so toggling it never
re-solves the graph. A hardware output's edge waits for the master's output latency
(its inserts included), so it comes out with the master; one slower than the master
cannot be pulled earlier and plays late.

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
in-house PSOLA renderer. The two compose in `MediaCache::sourceForClip`: the vocal hook
runs first and returns a `PitchEditedSource` (the rendered phrases over the media),
then a stretched or transposed clip goes through the stretch hook, which takes that
source's spans into its render key and lays them over the media it loads itself before
stretching, so playback, bounce and freeze (which build their own engine with both
services) hear the edits through the stretch. A new set of spans renders again in the
background; the `StretchedSource` reads the clip's previous render where the new one
is not ready yet (two atomic loads and copies, no allocation), and the stretch service
asks for a recompile when it has finished, so the old render is let go.

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
the engine knows nothing about takes. MIDI passes (`engine::buildMidiTakes`) go by the
MIDI track's record mode: takes make a MIDI take group (`TakeGroup::midi`, each take's
notes and controller events kept with it, the comp materialised as MIDI clips by
`core::compMidiClips`); merge and replace go through `core::RecordMidiPasses`, which
writes the passes into the clip under each and keeps the track's MIDI clips as they
were for undo. While recording, the piano roll draws the incoming notes from the
arrange view's `engine::LiveRecording`.

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
  modules, and the instruments. One registry, `builtInTypes()` in `plugins/BuiltIns.h`,
  lists every insert type with its menu name, whether it is an instrument, how to
  make it and its factory presets (`plugins/BuiltIns.cpp` is the registry and the factory
  alone; the first built-ins, gain, EQ, limiter, reverb and delay, are in
  `plugins/Classic`); the factory, the menus and `EffectPresets` read
  it (the strip modules keep their own list, `strip::moduleTypes()`, with the slots and
  `strip::create` in `plugins/ChannelStrip.cpp`; each module's processor is a file in
  `src/plugins/strip` (`Gate`, `Eq`, `Compressor`, `Opto`, `DeEsser`, `Saturation`,
  `Limiter`), and the curves their editors draw are in `strip/Curves.cpp`). They share a base that crossfades bypass and keeps
  state running while bypassed, and they publish real-time feedback for their
  editors through rings of atomics: a five-value frame every 5 ms (input, output,
  reduction and two processor-specific values) and two sample taps. Every
  editor is a `ui/strip/ModuleCard`, the hub of three parts: `CardFrame` (the
  header chrome, the dimming of a switched-off body, the collapsed tab),
  `ParamBinder` (the values the card shows, held to their ranges, the controls
  bound to them, and only the model's changes taken in, so a refresh never pulls
  a control off what automation plays) and `LiveFollower` (the engine's instance,
  its frames in a `FeedbackHistory` ring, the meters' ballistics; it marks the
  processor watched only while the card reads it). The editors
  analyse a tap with `ui/strip/TapSpectrum` (a Hann window, an FFT, one smoothed
  value per column of a log-frequency plot: the EQ's analyser, the exciter's
  spectrum) and draw that axis with `drawFrequencyGrid` (`ui/strip/StripStyle.h`).
  `FeedbackBuiltIn` (`plugins/MeteredBuiltIn.h`) owns that block for every built-in
  and strip module, and `MeteredBuiltIn` on it measures a block's peaks and taps
  around the processing; bypassed, a watched built-in measures what passes, unless
  its bypassed upkeep shows what it would do (the exciter's harmonics, the
  widener's image, Bloom's and Vesper's tails). The larger
  effects (Glue, Patina, Exciter, Bloom, Tape Echo, Stereo Widener, Corona, and the guitar pedals
  Gravity, Flare, Comet, Tide and Polaris, declared together in `plugins/Pedals.h`) each have a file
  of their own and a card in `src/ui/strip`; what their frame values and taps carry
  is described at the top of each processor. Patina (`plugins/Patina.cpp`) composes its six
  stages, each a class in `plugins/patina/` with its own `prepare`, `reset`, `controls`
  (once a 32-sample sub-block) and per-sample step: `Heat`, `Chamber` (its room, springs
  and plate), `Drift`, `Grain`, `Wear` and `Crush`, sharing the small parts in
  `patina/Parts.h`; the processor keeps the switches' fades, Age and Breath, the output
  section and the telemetry, and `patina/Curves.h` holds the mappings the card draws. Bloom's reverb core is
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
  the Listener in front of three prepared networks, one per space (their tunings in
  `plugins/vesper/Spaces`, Halo's strings in `vesper/HaloBank`; the block runs in
  named steps, from `beginBlock` to `endBlock`): each Listener
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
  ([ui-performance.md](ui-performance.md)). `HardwareKey` is the bakelite key.
  `ui/strip/CardParts` holds what the flat cards share: the display's share of a
  body (`takeDisplay`), the handle disc (`drawHandle`), big knobs on tiles
  (`placeBigKnobs`) and captioned knob groups (`KnobGroups`, Bloom's and
  Vesper's); the strip's limiter card serves the built-in limiter too, laid out
  by a `LimiterLayout`. `ui/strip/FaceParts` holds the parts several faces share: the VU meter's needle
  under its rendered hood and glass (the tape echo's and Orbit's, each with its
  own `Look`; their ballistics and needle angle are `hardware::VuBallistics` and
  `hardware::vuAngle`), Corona's jewel as a pilot lamp (Patina's, the pedals') and
  Corona's footswitch (the pedals' without its LED). `RenderedFace` carries the
  compact print helpers (`nameSize`, `nameTracking`, `nameFor`) and the one
  `value` hook a card sets for what each control shows. Every built-in effect's
  factory presets are in `plugins/EffectPresets` (by parameter name, resolved
  against the effect's parameter table); Patina and the instruments build theirs
  by parameter index with `PresetBuilder` (`plugins/ParamHelpers.h`, beside the
  `choice`, `onOff` and `percent` parameter helpers), and both report a value out
  of range through `factoryPresetErrors()`; the registry serves Patina's and the
  instruments' lists with the effects'. `ui/strip/EffectPresetLibrary` adds the
  user's own (JSON files keyed by parameter name) and tells every `PresetBar` when
  the list changes, so the card's header, the window's, an instrument's face and a
  face's own control agree. A chosen preset is one `core::SetInsertParameters`
  command (`ModuleCard::applyValues` on a card), so one undo step and one update of
  the engine and the views.
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
  anti-aliasing step), `Dynamics.h` (the soft-knee gain computer Glue and
  Gravity share; the strip compressor keeps its own, whose knee includes its top
  edge) and `Denormals.h` (`flush`, a state to zero below a
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
  Edits a plugin makes itself (its editor, its output events) reach the message
  thread through one `hosting::ParameterEditQueue`, a bounded multi-producer queue
  that never blocks (a VST3 listener can fire on any thread, the audio thread
  among them). The rest of the app reaches a hosted plugin's editor, presets and
  main-thread service through `HostFactory` (`createEditor`, `presetsFor`,
  `serviceHostedPlugins`) without knowing its format; preset files for both
  formats live in `Documents/<format> Presets/<vendor>/<plugin>`.
- **Scanning** runs one plugin file per child process with a timeout. A crash, hang
  or bad result blocklists the file with its reason, so a bad plugin cannot take the
  application down while it is inspected. The registry records modification times,
  so a rescan only inspects new or changed files.
- **Crash containment.** A hosted plugin's processing call runs under structured
  exception handling (only processing: its editor, state calls and destructor run
  unguarded on the message thread). A plugin that crashes is never called again,
  its slot passes audio through, and the user is told, with "Reload plugin":
  `ProjectSession::reloadInsert` makes the engine forget the instance
  (`InsertCache::forget`), so the next compile makes a fresh one from the model's
  state, which a crash never overwrites (`captureInsertStates` skips crashed
  instances). The crashed instance is never destroyed: the cache moves every
  crashed instance it lets go of into a list that is never freed.
- **Load failures.** `HostFactory` remembers why a type failed to load and does not
  retry it until a rescan or a Retry (`clearFailure`). The slots read the failure,
  the registry and the instance's crash flag through `ui::insertTrouble`; the
  session tells the user once per type ("Blocklist" blocks the plugin's file) and,
  after a project opens, names every plugin missing or failed in one notice.
- **State.** A hosted plugin owns its parameters and its state. The model keeps the
  plugin's state blob and only the parameters a command touched, so the plugin's own
  presets survive edits elsewhere. A preset or program loaded from the window or
  the card's menu is one `SetPluginState` step (the state before and after); undo
  and redo hand the model's state to the instance (`Command::pushesInsertState`).
  A save also records each hosted plugin's name (`Insert::pluginName`, so a missing
  one is still named) and which editor windows are open (`Insert::windowOpen`,
  reopened with the project once each plugin has loaded).

Plugin instances are created and destroyed on the message thread. A plugin window
shares ownership of its instance, so the engine dropping it can never free it under
an open editor.

## Offline rendering and export

A bounce runs its own engine over a copy of the project, with media loaded whole, on
a background thread, as fast as it can. Export then converts the sample rate with an
offline windowed-sinc converter, measures integrated loudness and true peak,
normalises, and dithers (TPDF, optionally noise-shaped) to 16 or 24 bits. Stems are
normalised together so they still sum to the mix. Freezing a track uses the same
render and plays the result in place of the track's instrument and inserts. A
cancel is seen within one render block and within a few thousand samples of the
conversion, measurement and write, so closing during an export waits only that long.

### Opening a project

A project opens in steps so the window keeps painting (`ProjectSession::beginOpen`
and `stepLoad`; `app/ProjectLoad`). A thread of its own reads the file, parses it,
and decodes each audio file once at the project's rate through the media
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
  chosen in Preferences goes in `appearance-tokens.json` beside it, applied after it,
  and High contrast applies the token file's `highContrast` variant last
  (`Theme::applyHighContrast`), so it never compounds with the accent. A change to either applies at once: `Theme::reloadColours` loads the
  colours again into the one theme every view holds (by reference), the look and feel
  sets its stock colours again, `NorthlightLookAndFeel::remapColours` updates the few
  components handed a token at construction (labels, placeholders), and every window
  gets `sendLookAndFeelChange`, which repaints and drops cached images (rendered
  faces invalidate on it). The user's rebound keys are the `shortcuts.bindings`
  setting (`ui::Shortcuts::save`), loaded at launch and whenever it changes. The settings Preferences shows are one schema (`core/Settings.h`: each
  key, its type and its default); Preferences binds its controls to it and the app
  reads them through its typed `settings::Reader`, so no default is written twice.
- **Autosave** writes unsaved projects to the settings folder while edits pause. The
  plugin states and parameter keys are taken on the message thread; the copy is
  turned into JSON and written on a worker, and a save or close waits for a write in
  flight. A copy is deleted only when the user saved or chose Don't save
  (`ProjectSession::discardChanges`); a project replaced or closed any other way
  (Windows ending the session, a path that did not ask) leaves a fresh copy, so
  anything left at launch is offered back, and a restored copy stays on disk until
  the project is saved or discarded. Instances share the folder: each holds
  `<instance>.lock` there open for its lifetime (Windows will not delete a file
  another process holds open, and a crash closes it), names its copies after itself
  and records itself as their owner, so another running instance's live copy is
  never offered or deleted.
- **Unsaved changes**: everything that replaces or closes the project goes through
  `FileActions::confirmDiscard` (the close-window sheet): New, Close project, Open,
  a recent or dropped file, a file from Explorer, New from template, Restore,
  Revert, the window's close button, the app menu's Quit and Windows ending the
  session (`systemRequestedQuit`, which writes an autosave before the sheet shows).
- **Saving** keeps the file it replaces as `<name>.northlight-bak` (Preferences,
  General) and writes through `File::replaceWithText`, which writes a temporary file
  beside the target and moves it over, so a failed save leaves the old file whole.
  Before writing, media is brought into the project's media folder as one undoable
  step (`SetMediaPaths`, "Bring media into the project folder"): an untitled
  project's recordings in the shared `Documents\Northlight\Recordings` always, and
  everything outside the folder when imports are copied (a first save) or Save As
  was told to bring it along. The originals are copied, not moved, so undo keeps
  working; the recordings this session made in the shared folder go to the Recycle
  Bin once the project that used them is closed saved or discarded.
- **Media**: imports are copied into a saved project's media folder when
  Preferences say so (`ProjectSession::takeInMedia`). A project that opens with
  files missing gets a toast with Locate…, as does each missing clip in the arrange
  view; locating one relinks it (`SetMediaPaths`, "Relink media") and offers to
  relink the others found by name in the same folder. **Templates** are project
  files without clips. **Media management** gathers outside media into the project
  folder and moves unused files to the Recycle Bin (for an untitled project, only
  its own recordings in the shared folder).
- **Instances and the shell**: a launch whose only argument is a project path (as
  Explorer's Open with passes it) hands the path to a running Northlight through
  JUCE's instance lock and quits; the running one opens it (after the unsaved
  changes question). Launches with developer flags never take part. Project files
  dropped on the window open the same way.
- **Crash reports** are minidumps written to the settings folder from buffers
  prepared at start-up. Nothing is sent anywhere.
- **Notices** are what the session tells the user once each (a recording's
  outcome, a plugin that crashed, an export or a freeze finishing): it pushes a
  typed `Notice` (kind, text, a file to reveal) and the window drains the queue on
  its next refresh into toasts, or the status bar for one still in progress.

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
notifies one listener, which keeps every view on the same channel. The window's
tick reads the engine's meter block once (`ui::MeterReadings`) and hands the
readings to the mixer's strips and the arrange view's headers alike, and the
arrange view reports its snap, grid and ripple when they change rather than
being asked every frame.

The main window's content (`app/MainWindow.cpp`) lays the parts out, wires them,
ticks them at 30 Hz and dispatches the window's keys from the shortcut table; the
rest is in collaborators of its own, each handed only what it needs: `FileActions`
(new, open, save, templates, media, imports, MIDI export, recent files),
`LoadDriver` (a project load in steps behind the loading sheet), `ExportController`
(the export sheet and the bounce it starts), `AppMenus` (the app, project and snap
menus, every item with its action), `SessionNotices` (the session's notices and the
dropout warnings), `FirstRun` (the first-run sheets), `Benchmarks` (the developer
benchmarks) and `LaunchRun` (the launch flags). The `MainWindow` facade holds its
content by a typed pointer.

Every insert slot, wherever it shows (a mixer strip, an Editor panel card, a
short strip's list), takes its menu, its state and its name from one place,
`ui/InsertMenu`: `addInsertMenuItems` builds the menu (open, bypass, presets,
replace, order, wet/dry, sidechain, outputs, copy and paste, latency, the way out
of trouble), `insertTrouble` says whether its plugin is missing, failed or
crashed, and the copied inserts live there. What a slot does beyond commands
(reload, retry, blocklist, the browser, a group of commands) comes through one
`ui::PluginActions` the shell owns and hands to the mixer and the Editor panel,
as `ui::PluginSources` gives every view the same plugin lists. Inserts move
between mixer strips through JUCE's drag and drop ("insert:<channel>:<insert>",
beside the browser's "plugin:<type>"); the strips and the master are drop targets.

The timeline views (the arrange view and the lanes it hosts, the piano roll, the
audio editor) map time to pixels through one `ui::TimelineViewport`
(`src/ui/timeline/`, no JUCE): the x where time starts, the first visible sample
and the samples per pixel, with one rounding rule both ways (a sample's column is
its exact x rounded down; a column's first sample is the first that maps into it).
The take lanes, automation lanes and tempo track keep a reference to the arrange
view's, so they cannot drift from it, and Ctrl+wheel zooms every view by the same
smooth factor around the pointer, each within its own range. The bar, beat and
sub-beat lines come from one iterator over the tempo map and a viewport
(`ui::forEachGridLine`, with one set of density thresholds), drawn by one grid and
one ruler painter (`ui/timeline/TimeGrid`); the audio editor gives them its axis
in timeline samples. Snapping has one rule too (`ui::snapStepAt`): a beat and its
divisions are counted in the signature where the position falls. Waveforms are
drawn by one painter (`ui::paintWaveform`: a min/max column per pixel at the
level the clip plays, its gain through its fades, clamped to full scale after
them), and a clip's body, header band, hatching, outline and name by
`ui::clips` (`ClipPainter`), for audio, MIDI and recording clips and takes alike.
The arrange view's edits are built by plain functions in `ui::edit` (`ClipOps`, no
JUCE, unit tested): the keys and menus (delete, split, join, mute, paste) and the
drags it commits on release (move, with the take-group and same-kind rules,
copy, the automation that follows moved clips, trim, stretch, fade and gain, and
the time range's delete, copy and paste); the view only previews a gesture and
emits what they return. The track and range edits they need are commands of
their own in `core/ArrangeCommands` (duplicate a track, move tracks as a block
into or out of folders, reorder buses, a clip's name and colour, remove a time
range), and `core::placeAfter` says where a new track goes. The arrange view keeps
the set of selected tracks beside the shared `ui::Selection` (which holds the one
in focus), and `AutomationLanes` keeps its own point selection and clipboard,
which the arrange view's Delete, cut, copy and paste ask first. The snap grids
(straight, triplet, dotted) are one table, `ui/timeline/SnapGrids.h`, that every
view's snapping and the snap menu read.

The arrange view itself is split by job under `src/ui/arrange/`: `ArrangeLayout`
(the rows' heights and tops, no JUCE, tested), `ArrangeView::Painter` (one per
paint, for the area repainted), one `ArrangeView::Gesture` per kind of drag
(`ArrangeGestures`: each keeps its own preview and commits through `ui::edit` on
release), `ArrangeMenus` (every item carries its action), `MarkerController`
and `ArrangeKeymap` (the keys it acts on). Every key the app answers is in one
registry, `ui::Shortcuts`: each command's id, name, section, context (global,
arrange, piano roll, audio editor), default keys and current keys. The main window
dispatches its global commands from it (`Shortcuts::dispatch`), the panels take
their keys from it (`PanelManager::setShortcut` after a rebinding), the arrange
keymap, piano roll and audio editor match through it, and the Shortcuts page
(`ui::ShortcutEditor`, which rebinds), tooltips (`Shortcuts::tag` and `retag`) and
menu items (`AppMenus`, `ArrangeMenus`) print its current keys. The mixer's strips live in
`src/ui/mixer/`, the piano roll's controller lanes are components of their own
(`ui::ControllerLane`, one per controller lane the roll stacks), its note edits
(clipboard, quantise with swing, humanise, velocity, legato, split, join, mute) are
plain functions in `ui::noteops` (`src/ui/edit/NoteOps`, no JUCE, unit tested) that
return what to set, add and remove, and its grid values are one table
(`ui/edit/NoteGrid.h`). The audio editor's Correct sheet, pitch grid and pitch rules
are in `src/ui/audioeditor/`; the key menu the piano roll, the audio editor and the
Correct sheet share is `ui::keymenu`. The names an instrument gives its notes (a drum
kit's pads) come from the built-in registry (`plugins::builtInNoteName`).

Accessibility (design/accessibility-performance.md 11.2 and 11.3) runs through
JUCE's handlers, which Windows reads as UI Automation. The shared pieces are in
`ui/components/Accessibility.h`: `AccessibleValue` makes a custom control a slider
with a readable value, `TooltipNamedHandler` names a control from its tooltip when
it has no title, `readoutHandler` exposes a lamp or needle as read-only text,
`showToggle` marks a model-driven button checkable, `titleForTrack` names a
per-track control after its track, and `FocusReturn` gives the focus back when a
window closes. `NorthlightLookAndFeel` decides when focus rings show (a focus change
made with Tab or an arrow key, in any window, turns them on; a click turns them
off) and draws them, outside a control or inside a region (`drawRegionFocusRing`,
used by `FocusListBox` and the editors).

The main window's content is a keyboard focus container whose traverser is
`ui/shell/FocusCycle`: JUCE's order (the regions carry explicit focus orders) with
the arrange view moved after its own children, so the lanes follow the track
headers, and every header but the selected track's skipped
(`ArrangeView::skippedByTab`). The arrange view and each track header take the
focus themselves; their keys still reach the window's handler, which dispatches
the keyboard map. Shift+F10 (and the Menu key, which Windows sends only as a key
state change) asks `ui::KeyboardMenu::showFor` for the menu of the focused
component or its nearest parent that implements `KeyboardMenu`: the arrange view,
a track header and the piano roll do.

What a view paints rather than builds from components (clips, notes, markers,
tempo changes, automation points, take lanes, the mixer's strip cells) reaches
UI Automation through `ui/components/AccessibleItems`: a transparent layer over
the view holding one invisible stand-in component per item on screen, each with a
handler that reads its item (a role, everything spoken in its title, selected or
checked, and Invoke and Show menu actions that do what a click and a right-click
do). The view describes its items in a builder (`ArrangeView::accessibleItems` in
`arrange/ArrangeAccess.cpp`, and one each in `AutomationLanes`, `TempoLane`,
`TakeLanes`, `PianoRoll` and the mixer's `StripRow`); a list item groups its
children, which read in the builder's order (by time) rather than JUCE's top to
bottom. The layer is empty unless a client is listening
(`ui::accessibilityClientsActive`: Windows' screen reader flag or
`UiaClientsAreListening`, checked once a second; `--screen-reader` forces it).
While one listens, the view's paint asks for a rebuild, which runs at most every
100 ms, reuses stand-ins by key and moves a stand-in only when its bounds change.
When the item the keyboard is on changes while the view has the keyboard focus,
the reader's focus moves to its stand-in (the keyboard focus stays on the view),
so a reader says the clip or note that Ctrl+Left / Right just selected.

High contrast and reduce-motion are each a System / on / off choice
(`settings::followsOrChooses`). The main window resolves them in `applyPreferences`:
contrast reloads the colours with or without the variant, and reduce-motion sets
`Theme::Motion::reduceMotion`, which every animation reads as it starts. Windows'
side comes from `Theme::systemHighContrast` and `systemReduceMotion`;
`app/SystemSettingsWatcher` keeps a hidden window that receives
`WM_SETTINGCHANGE` and applies the preferences again when either value changed. A
view reads `Theme::highContrast` only for what colour cannot say (the clip light's
rim, a flagged note's dashes).

The specification for every screen is in [design/](design/README.md).

## Testing

| Suite | Covers |
|---|---|
| `northlight_unit_tests` | The model and every command's undo and redo, JSON round trips, the tempo map, the engine offline (sample-exact playback, loops, routing, delay compensation, MIDI, recording, streaming, export DSP, pitch editing) under the real-time allocation hook |
| `northlight_juce_tests` | JUCE-linked code without a device: the audio callback through a fake device, media reading, plugin hosting against a test CLAP, themes, UI components, MIDI files |
| CTest checks | No literal colours in `src/ui`; formatting |
| Hardware tools | Loopback latency calibration on a real device; an offline recording soak (many inputs for many minutes through the real record path, verified sample-exactly) |

Tests never depend on audio hardware. See [CONTRIBUTING.md](../CONTRIBUTING.md) for
how to run them.
