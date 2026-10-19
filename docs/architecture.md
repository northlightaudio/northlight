# Architecture notes

Living document. The intended architecture is in [PLAN.md](PLAN.md), section 3; this
file records what is actually built and the decisions made along the way.

## Current state (Phase 6 complete; Phase 8 MIDI in progress; Phase 7 waits for the design spec)

```
aural.exe
  src/app         JUCEApplication; MainWindow (transport + record bar, file and
                  import buttons, undo/redo, device summary, Audio Setup window);
                  ProjectSession (the open Project + UndoStack + AudioEngine +
                  DiskThread + PeakStore; recording flow and recovery);
                  UserSettings (per-user properties file)
  src/ui          Theme (design tokens), AuralLookAndFeel (dark, token-driven),
                  AudioSetupView (audio and MIDI devices), ArrangeView (tracks,
                  waveforms, MIDI clips, zoom, scroll, editing), TrackHeader
                  (input or MIDI input, arm, monitor), MixerView (strips, master,
                  inserts menu), InsertEditor (generic parameter window),
                  PianoRoll (one MIDI clip's notes and velocities)
  src/core        Project model (audio and MIDI tracks and buses on a shared
                  Channel, sends, fader groups, master, audio clips, MIDI clips
                  and notes, markers), Command + UndoStack, JSON files,
                  bars/beats helpers. No JUCE.
  src/engine      AudioEngine façade, Snapshot compiler + MediaCache + InsertCache,
                  Transport, Renderer, WorkerPool, AudioSource (memory + streaming),
                  DiskThread, Peaks, Recording (record rings + writer thread),
                  WavFile, Click (metronome), Insert interface, Meters, Loudness,
                  Midi (event list per span; the renderer schedules clips and
                  live input into instruments). No JUCE.
  src/plugins     Built-in processors (gain, EQ, compressor, limiter) and the
                  built-in synth behind the insert interface. No JUCE.
  src/hosting     PluginRegistry (known plugins, blocklist, scan times; no JUCE),
                  PluginScanner (drives aural_plugin_scan per file), HostedPlugin
                  (a VST3 instance behind the insert interface), HostFactory
                  (built-ins + hosted types for the engine)
  aural_plugin_scan  the out-of-process scanner (one plugin file per run)
  src/engine/rt   Real-time guard and allocation hook, SpscQueue, SnapshotPublisher,
                  CallbackStats, ScopedFlushDenormals
  src/audio       Pure:  AudioSettings, LatencyDetector, PassThrough
                  JUCE:  AudioDeviceLayer, MidiDeviceLayer, AudioCallback,
                         LatencyCalibrator, JuceMediaLoader, JuceStreamReader,
                         PeakStore

aural_unit_tests.exe
  tests/unit/rt      Catch2 tests for the guard
  tests/unit/audio   Settings serialisation, latency detection, pass-through
  tests/unit/core    Model, every command's undo/redo, JSON round trips
  tests/unit/engine  Queue/publisher, offline engine harness (sample-exact
                     playback, loop, solo/mute, pan, snapshot swaps under the RT
                     hook), streaming source against a live disk thread, peaks,
                     worker pool, callback stats, recording (sample-exact capture,
                     device stop mid-record, WAV repair, monitoring), MIDI (note
                     scheduling, stop/jump/loop note-offs, live input, the synth,
                     MIDI capture and take building)

aural_juce_tests.exe
  tests/unit_juce    JUCE-linked tests that need no device: stream reader
                     resampling, AudioCallback driving the engine through a fake
                     juce::AudioIODevice

aural_loopback_tool.exe, aural_record_soak.exe
  tests/hardware     Console tools: list devices / loopback calibration; the
                     offline recording soak (N inputs for M minutes through the
                     record path, verified sample-exactly). Built, never run by
                     CTest.
```

Build targets:

| Target | Kind | Depends on |
|---|---|---|
| `aural_options` | INTERFACE | Compiler flags and defines shared by everything |
| `aural_rt` | STATIC | `aural_options` |
| `aural_rt_checks` | OBJECT | `aural_rt`; holds the global `operator new` override so it is linked into executables rather than discarded from a static lib |
| `aural_juce` | STATIC | The JUCE modules, compiled once; re-exports their include paths and settings (see decisions log) |
| `aural_core` | STATIC | `aural_options`, nlohmann_json (private); the model |
| `aural_engine` | STATIC | `aural_rt`, `aural_core`; the engine, JUCE-free |
| `aural_audio` | STATIC | `aural_rt`; pure audio code with no JUCE dependency |
| `aural_audio_device` | STATIC | `aural_audio`, `aural_engine`, `aural_juce`; the device layer and media loading |
| `aural_ui` | STATIC | `aural_core`, `aural_audio_device`, `aural_juce` |
| `aural` | GUI app | `aural_ui`, `aural_audio_device`, `aural_engine`, `aural_core`, `aural_juce`, `aural_rt_checks` |
| `aural_unit_tests` | console app | Catch2, `aural_audio`, `aural_core`, `aural_engine`, `aural_rt`, `aural_rt_checks` |
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
It then calls `engine::AudioEngine::process` to render the project and adds the
input pass-through on top (`renderPassThrough` in accumulate mode) when monitoring
is on. Control values are atomics; peak meters are atomics the UI drains. Nothing in
the callback allocates, locks or posts messages; the debug build's allocation hook is
live for the whole callback. `audioDeviceAboutToStart`/`audioDeviceStopped` bracket
the engine's `prepare`/`release`, which is where the renderer's scratch is sized.

### Core model

`core::Project` is plain structs: tracks hold clips, clips reference media by path
and sit on the timeline in samples at the project rate. Every edit is a
`core::Command` with `apply`/`revert`; `core::UndoStack` owns the history and can
group commands into one step. `projectToJson`/`projectFromJson` read and write the
versioned `aural.project` format. Bars/beats are a display conversion
(`samplesToBarsBeats`) with a constant tempo for now.

### Engine

`engine::AudioEngine` is the façade with a controller side and a real-time side.
The controller compiles the project into an immutable `Snapshot` (`compileSnapshot`:
tracks with mute/solo resolved, clips bound to `AudioSource`s through `MediaCache`)
and publishes it through `rt::SnapshotPublisher`; transport changes travel through
an `rt::SpscQueue<TransportCommand>`. The real-time `process()` drains the command
queue (bounded per block), acquires the current snapshot and calls `Renderer::render`,
then publishes position and playing state as atomics.

`Renderer` mixes every audible track to a stereo master: clip gain times track gain,
constant-power pan for mono sources and a balance law for stereo. The block is split
at the loop end so the loop wraps sample-accurately, as many times per block as the
loop length requires. Each `TrackNode` carries its own stereo output and scratch
buffers, allocated when the snapshot is compiled (the buffer pool, sized at compile
time), so tracks are independent jobs: `WorkerPool::run` hands them out through one
atomic that packs the batch's job count with the next index (a worker compares an
index only with the count it was taken with, so a late worker can never act on a
newer batch), to the worker threads and the callback thread alike; the sums happen
afterwards in track order, which makes the output bit-identical whatever the worker
count. Workers are time-critical threads that block on a semaphore between batches
and run each job inside a real-time section with denormals flushed; the callback
thread wakes them with one counted semaphore release (never blocking, never more
permits than workers), spins on the completion count with a short budget before
yielding, and waits for every worker to leave the batch. The recommended worker
count leaves two hardware threads free for the device and message threads. Sends,
buses and inserts arrive in Phase 4 as more job kinds in the same batch structure.

`rt::CallbackStats` records every callback's duration as a fraction of its deadline
into a lock-free histogram; the status bar shows p50/p99/max, overruns (callbacks
over their deadline) and the driver's dropout count.

`SnapshotPublisher` retires superseded snapshots on the controller side and frees
them only once the real-time thread advertises a newer epoch (or when no real-time
thread is running). Snapshots hold `shared_ptr`s to their sources, so media lives as
long as any snapshot references it; `MediaCache::prune` drops the rest.

### Media and streaming

`MediaLoader` is the engine's loading interface; `audio::JuceMediaLoader` implements
it. Files up to 30 s are decoded into a `MemoryAudioSource` (resampled on load);
longer ones become a `StreamingAudioSource` over a `JuceStreamReader` (resampled
chunk by chunk on the disk thread). Every clip of a streamed file gets its own
window: the loader installs a factory on the source and `MediaCache::getForClip`
creates one instance per clip id, kept across recompiles so an edit never empties a
window. `engine::DiskThread` is the single disk I/O thread: it services each
window, keeping a read-ahead in front of the position the real-time thread last
read and rebuilding the window after a locate. The real-time `read` never blocks:
outside the window it returns silence and counts an underrun, and it re-checks the
window after copying so a backward jump can never hand out torn samples.

Sample-rate conversion is `engine/Resampling.h`: 4-point Hermite interpolation
evaluated at each output sample's exact source position. It is stateless, so reads
can start anywhere, and it has no lookback delay, so imported media stays aligned
to the sample.

### Session and UI

`ProjectSession` ties the pieces together on the message thread: it owns the
project, the undo stack, the disk thread, the loader and the engine, republishes the
snapshot after every command, imports files as new tracks, saves/opens JSON project
files and drives the transport. An empty project adopts the device sample rate; a
project with media keeps its own and the mismatch is shown. `MainWindow` has the
transport bar (space to play/stop, Return/Home to return to start, Ctrl+Z/Y), import,
and `ArrangeView`.

### Waveforms and the arrange view

`engine::PeakData` holds mipmapped min/max peaks per channel: a base level of 256
samples per bin and five coarser levels, each four times coarser. `lookup(channel,
start, end)` picks the coarsest level whose bins still fit the span, so one pixel
column costs a handful of bin reads at any zoom. `audio::PeakStore` builds peaks on a
low-priority background thread (reading through `JuceStreamReader` at the project
rate so peaks line up with clip positions), caches them beside the media as
`<file>.<rate>.aural-peaks` while the cache is newer than the media, and broadcasts a
change on the message thread when a build finishes. The view asks for peaks while
painting and simply draws a flat block until they arrive.

`ui::ArrangeView` draws a two-row ruler (bars with beat ticks over time, spacing
chosen from the zoom), one row per track with its clips, a waveform column per pixel
from the peaks, and the playhead, which pages the view forward during playback.
Horizontal zoom is Ctrl+wheel around the pointer; the wheel scrolls tracks and
Shift+wheel (or the scrollbar) scrolls time; clicking locates; audio files dropped
on the view become tracks at the drop position. Clip editing is Phase 6.

### Recording

The recording path follows PLAN.md 3.4. A track has an input channel (an index
into the device's active inputs), an armed flag and a monitor mode. Starting a
recording builds an `engine::RecordSession` on the controller thread with one
`RecordRing` (four seconds of samples) and one target file per armed track, hands it
to the `RecordWriter` thread (which opens the float WAVs) and then to the real-time
thread through the transport command queue. Every block, the real-time thread
pushes each captured input into its ring and the transport plays; the writer drains
the rings every 10 ms and rewrites the WAV headers every second, so a crash loses at
most a second of header bookkeeping and nothing of the audio. Overruns (a full ring)
drop the block rather than block: the real-time thread records a gap (captured
count plus length) through a lock-free queue and the writer fills it with zeros, so
a file's sample number is always the captured sample number and the take
boundaries after a dropout still land where they should. The writer pops samples
before it pops marks and gaps, because the real-time thread pushes an event before
the samples that follow it; the other order could write a take's first block into
the previous file. An open, write or header failure marks that input failed: the
writer stops touching it, counts the rest as lost and reports `ok = false` with
`samplesLost`; the session repairs and keeps whatever did reach the disk. The WAV
header saturates at 4 GB (about 6 h 12 m of float mono at 48 kHz); a longer take
still records, but only the first 4 GB is readable until it is split.

Loop recording: when the transport wraps while capturing, the real-time thread
marks a take boundary (the cumulative captured count plus the loop start) through a
lock-free queue; the writer closes the file at that sample and opens
`<name>-take<n>.partial.wav`. Every pass through the loop becomes its own clip at
the loop start, later takes on top of earlier ones on the same track (take lanes
with their own rows come with the editing phase).

Starting at or past the loop end (a locate there, or the loop enabled while
outside it) wraps to the loop start before the first capture, so the take is
placed where its audio belongs. `ProjectSession::startRecording` refuses to run
without a device or while the device rate differs from the project rate (the
files are stamped with the project rate), picks a base name no file in the media
folder already shares, and writes `<base>.recording.json` beside each input before
capture begins: track id and name, capture start, loop start, rate and the latency
in force. If the transport command queue is full the engine ends the session at
once and reports an error rather than leaving it pending.

Stop, locate or a device stop end the session on the real-time side; once the
writer has closed the files the session polls `collectRecording()`, renames the
`.partial.wav` files to `.wav` (never over an existing file), deletes empty takes,
removes the sidecars and adds the clips in one undo step. Placement is
`engine::placeRecording`: shifted back by `recordingLatencySamples()` (the loopback
calibration when it matches the device configuration, otherwise the driver's
reported input plus output latency), trimming the front of anything that would
land before zero. At startup and on open, the main window counts `.partial.wav`
files in the media folder and asks whether to recover them or set them aside as
`.discarded.wav`. Recovery repairs each file from its size with
`WavWriter::repair` and, with its sidecar, puts it back on its own track (if that
track still exists) at its capture position, later takes at the loop start, shifted
by the recorded latency; without a sidecar it lands at zero on a "Recovered" track.

A muted or un-soloed track still renders its clips and inserts (envelopes, tails
and latency buffers keep moving; its key output stays live for sidechains, though
a muted source's key reads as silence); the routing stage is what keeps it out of
the mix.

Input monitoring is part of the track render: a track in monitor mode "on" always
hears its input through its gain and pan; "automatic" hears it while armed and the
transport is not playing back (stopped or recording); "off" never. The Phase 1
pass-through in Audio Setup is still available as a global monitor and adds on top.
Hardware (driver-side direct) monitoring is not controlled from Aural: JUCE's ASIO
wrapper does not expose the input-monitor message, so an interface's own direct
monitoring is used by setting the track to "off". The header shows a green dot
while a track's input is being heard and the arm button reads REC during a take.

### Phase 3 milestone

The plan's milestone is 16 inputs for 30 minutes with no dropouts, then a kill and
recovery. The Volt has four inputs, so the 16-input run is the offline soak
(`aural_record_soak --inputs 16 --minutes 30 --speed 8`): synthetic inputs through
the real record path (rings, writer thread, header rewrites) at eight times real
time, every file verified sample-exactly afterwards. On the development machine it
passed with zero ring overruns (see the hand-over note for the figures). On the Volt,
two inputs were recorded and placed, a one-bar count-in delayed capture by exactly
two seconds, and a process killed mid-take left partial files that the next launch
offered and recovered.

### Mixer and routing

Tracks and buses share `core::Channel`: fader gain, pan, mute, solo, phase invert,
colour, an output bus (or the master) and aux sends (level, pre/post fader). The
snapshot compiler resolves this into `ChannelNode`s: bus cycles are broken (the
earlier bus in project order loses its forward edge and goes to the master), buses
get a dependency order (feeders first), and solo-in-place becomes a per-channel
`audible` flag: with any solo, a channel is heard if it is soloed, feeds a soloed
channel or is fed by one; mute always wins.

Rendering per span: tracks render pre-fader in parallel (clips with clip gain and
pan, monitored input through pan, phase invert). The summing stage on the callback
thread then applies each track's fader, adds it to its output bus accumulator or the
master mix, feeds its sends (pre-fader taps the pre-fader buffer, post-fader scales
it by the gain), walks the buses in dependency order (balance pan and phase on the
summed input, then the same fader/route/send step), and finally applies the master
gain. The order is fixed, so the mix is bit-identical whatever the worker count.

`rt::MeterBlock` holds one slot per channel plus the master: the real-time thread
writes post-fader peak (held until read) and RMS after every span; the mixer takes
readings at 30 Hz and draws them with its own ballistics. Slots follow project
order: tracks, then buses, then the master (`trackMeterSlot`, `busMeterSlot`,
`masterMeterSlot`).

`ui::MixerView` shows one strip per track and bus (name, output selector, sends
menu, inserts menu, pan, mute/solo/phase, fader in dB, stereo meter) and the master
strip with momentary, short-term and integrated LUFS, in a panel under the arrange
view (Mixer button, Ctrl+M). Every control issues a `core::Command`; the view
refreshes from the project.

Live values and smoothing: a fader, pan, master or insert-parameter command is
"live" (`Command::isLive`). The session applies it to the model (undo) and pushes
the model's values into the running graph (`AudioEngine::applyLiveValues`) instead
of recompiling: channel gains and pans are targets in a per-channel
`engine::ChannelRuntime` (kept in a `RuntimeCache` across compiles, like insert
instances and delay lines) that the renderer ramps towards across every block, so
a move glides instead of stepping; insert parameters and bypass are atomics on the
instance. A slider drag is a gesture (`beginGesture`/`endGesture`): every command
until release lands in one undo step. Structural edits (routing, inserts added or
moved, clips) still recompile the snapshot. Routing cycles are refused at the
command layer (`Project::wouldCycle`, reachability over outputs and sends,
including a bus to itself) and the output selector and sends menu do not offer
them; the compiler keeps its cycle breaking as a defence. Solo-in-place resolves
audibility per path: a channel lit only because it sends to a soloed bus keeps
the send and loses its direct output (`ChannelNode::audible` versus
`sendsAudible`), so soloing a send-fed reverb plays the reverb alone. The meters'
RMS integrates over the whole period between UI reads; the master's slot comes
from the engine's current snapshot, not the UI's model.

Fader groups are `core::Group`s (name, colour) that tracks and buses point at
through `Channel::group`. They are a model-level link applied by commands, not an
engine concept: `SetGroupGain` scales every member's gain by one ratio (a fader move
in dB applied to all, keeping their balance, clamped at the fader's top), and
`SetGroupMute` / `SetGroupSolo` set every member; each stores the members' own
previous values so undo restores them individually. The strip's Group button picks
a group, makes a new one (joining it in the same undo step) or removes it, and a
grouped strip's fader, M and S issue the group commands instead of its own.

### Inserts and the built-in processors

A channel's inserts are `core::Insert`s (type name, bypass, parameter values).
`engine::InsertProcessor` is the processing interface: parameters are atomics the
processor reads at the start of each block, `process` works in place on the stereo
pair, and `prepare` allocates. `engine::InsertCache` keeps one prepared instance per
model insert id across recompiles, so filter memory and envelopes survive edits;
each compile pushes the current parameter values and bypass. The renderer runs a
track's chain after its pan and phase (still in the parallel track render) and a
bus's chain after its balance and phase, both pre-fader.

Every built-in shares a base that handles bypass as a 10 ms crossfade between the
processed signal and the input, keeps the processor running while bypassed so its
state follows the audio, and for the limiter passes the bypassed signal through
the lookahead delay so the latency the graph was solved with holds. The EQ ramps
its biquad coefficients across the block when a parameter changes.

`src/plugins` holds the built-ins behind `plugins::BuiltInFactory`: Gain (ramped),
EQ (high-pass, low shelf, peaking mid, high shelf as RBJ biquads), Compressor
(log-domain peak detector, soft knee, makeup) and Limiter (a lookahead limiter: the
signal is delayed by the lookahead while the gain the coming samples need is
known, taken as a running minimum over the window with release, smoothed by a
moving average of the same length, so nothing crosses the ceiling and the gain
ramps instead of stepping; it reports the lookahead as latency for delay
compensation). `ui::InsertEditor` is a generic
window with a slider per parameter, used for every built-in; hosted plugins bring
their own editors.

`engine::LoudnessMeter` implements BS.1770 K-weighting (the RLB stage with the
standard's unnormalised numerator, so a full-scale 997 Hz sine in one channel reads
-3.01 LUFS) with 400 ms momentary and 3 s short-term windows on the master mix, and
integrated loudness per BS.1770-4: the 400 ms window at every 100 ms hop is kept
(two hours' worth, preallocated) and the controller computes the gated mean on
demand (absolute gate -70 LUFS, relative gate 10 LU under the mean of the rest).
Play from stop restarts it.

### Editing (Phase 6)

Clips carry linear fade-in and fade-out lengths that the renderer applies per
sample, so overlapping clips with matching fades crossfade at a constant sum. The
editing commands are `SplitClip` (the left part keeps the id and the fade-in, the
right part takes the fade-out and continues the source), `JoinClips` (contiguous
clips of one source become one), `DuplicateClip`, `CrossfadeClips` (fades over the
overlap), `SetClipFades`, `SetClipGain` and `ShiftClips` (every clip from a
position on moves by a delta: ripple editing). `core::gridSamples` picks the
finest musical division (bar down to an eighth of a beat) that is at least 16 px
wide at the zoom and `snapToGrid` rounds to it; both are pure and unit-tested.

`ui::ArrangeView` does the interaction and keeps the selection (a list of track
and clip ids, pruned when the project changes): click selects (Shift adds, Ctrl
toggles), a drag on empty space lassos, dragging a clip body moves the whole
selection (the dragged clip's start snaps, the rest follow by the same delta, rows
can change, nothing lands before zero), dragging an edge trims (the left edge is
limited by the source start), the handle at a top corner sets a fade, Ctrl+drag
sets the clip gain. Every edit is previewed from the view's own drag state and
committed on release as a list of commands that `ProjectSession::executeGroup`
runs as one undo step. Keys (the window forwards them to the view first): Delete
(with ripple on, later clips close the gap), Ctrl+D duplicate, S split at the
playhead (selected clips, or all under it), J join, X crossfade overlapping
neighbours, Ctrl+Shift+N normalise (the session reads the source's peak and sets
the gain), Left/Right nudge by a grid step, Ctrl+A, Escape, G snap, E ripple.

Markers and regions are `core::Marker`s (name, position, length; a length of
zero is a marker) kept sorted on the project, with `AddMarker`, `RemoveMarker`
and `SetMarker` (position, length and name together) as commands and
`markerBefore`/`markerAfter` for navigation. The arrange view draws them as
flags in the ruler's time row and as lines (regions as shaded spans) through the
lanes: M adds a marker at the playhead, Shift+M a region from the loop region,
comma and full stop jump to the previous and next marker, a click on a flag
selects and locates, a drag moves it (snapped), a double-click renames it in a
small dialog, Delete removes the selected one.

Bounce (`app::BounceJob`): the mix or one file per track (stems, the others
muted), over the whole project or a range (the loop region), to 32-bit float
stereo WAV at the project rate. Each render runs its own `AudioEngine` over a copy
of the project (with the live plugin states captured first), built on the message
thread because plugin instances must be created there, with media loaded whole
(`JuceMediaLoader` in memory-only mode: streaming would outrun the disk thread
offline). The blocks render on a background thread; the first
`outputLatencySamples()` of output are dropped and that much is rendered past the
end so the file starts and ends exactly on the range; the result carries the
integrated loudness of the mix. Export... on the toolbar offers the three.

### Plugin hosting (Phase 5)

Hosted plugins are inserts like the built-ins: the model insert's type is
`vst3:<identifier>` and `hosting::HostFactory` creates a `hosting::HostedPlugin`
(a `juce::AudioPluginInstance` behind `engine::InsertProcessor`) for it, so the
renderer, the insert cache and the mixer need no special case. A hosted plugin owns
its parameters: `parameters()` is empty, its own editor (or JUCE's generic one)
edits it in a `ui::PluginWindow` on the live instance, and the model keeps an
opaque base64 `state` blob that the cache restores once when the instance is
created and `ProjectSession::save` captures from every live instance. The channel
layout is negotiated once, when the instance is created: a stereo main bus in and
out (plus a stereo key on the second input bus when the plugin has one), else a
mono main bus (the pair is summed into it and its output goes to both sides), else
the load fails with an error the user sees and the type is not retried until the
next scan. `process` wraps the channel pointers in a `juce::AudioBuffer` (no
allocation) and an empty MIDI buffer; latency is read live for delay
compensation. Bypass crossfades over 10 ms between the processed signal and the
input, then runs the plugin's own bypassed path (`processBlockBypassed`, which
keeps the latency), so a tail never freezes and the graph is not re-solved. A
plugin window shares ownership of the instance it edits, so the engine's cache
dropping the instance can never free it under an open editor, and editors are
closed before a project is replaced; a removal captures the live plugin state
into the model first so undo brings back what the user had.

Each insert slot has a wet/dry `mix` (model, command, JSON): the renderer keeps a
dry copy of the slot's input, held back by the insert's latency through a cached
delay line so the halves stay aligned, and blends it with the processed signal;
at mix 0 the insert still runs on scratch (its meters and state keep moving) and
the chain sees only the dry path. The generic insert editor and the plugin window's
bar (bypass, mix, pin) both edit it. A plugin window remembers its position per
plugin type and the pin (always-on-top) preference in the user settings, and its
Presets menu lists the plugin's factory programs and the `.vstpreset` files under
the standard user folder (`Documents/VST3 Presets/<manufacturer>/<plugin>`), with
load and save through the format's preset extension. The
plugin browser (`ui::PluginBrowser`, from the mixer's Plugins... button or
`--plugins`) lists everything the registry knows with search, shows the blocklist
with reasons, and offers a scan for changes, a full rescan and clearing the
blocklist.

Sidechains: an insert slot can key from another channel (`Insert::sidechain`,
`SetInsertSidechain`, the inserts menu's Sidechain submenu, offered when the live
processor `acceptsSidechain()`). The key is the source's pre-fader signal (its
chain output; the fader does not matter, mute does, since a muted channel renders
silence). A track may key from a track; a bus from a track or from a bus. To make
the source ready in time the compiler groups tracks into levels (a keyed track
renders in a later level than its source; levels run in parallel one after the
other, and a cycle drops the keys that block it) and adds bus-to-bus key edges to
the bus ordering (a key that would form a cycle with the routing is dropped). The
built-in compressor keys its detector from the sidechain; a hosted plugin with a
second input bus gets it enabled as a stereo key bus and is driven with four
channels (silence on the key when no source is set).

CLAP plugins are hosted directly against the CLAP C ABI (JUCE does not host
CLAP) by `hosting::HostedClapPlugin`, behind the same insert interface, as
`clap:<id>` types; `HostFactory` picks the format from the type prefix, so the
chain, the cache, the window and the browser know nothing format-specific. The
host object implements log, thread-check, params, state, latency, audio-ports,
note-ports (no dialects yet), gui and timer-support. Main-thread calls (init,
activate, state, params flush while idle, gui) run on the message thread;
audio-thread calls (start/stop_processing, process) on the real-time thread, or on
the message thread while no audio runs, when the thread check vouches for it.
Parameter changes from the host are queued on an SPSC queue and become sample-0
events of the next block, or go through `params.flush` when the plugin is idle;
the plugin's own parameter events update the host's view. Audio goes through
preallocated `clap_audio_buffer` arrays in place (main stereo port; a second
input port is the key), events through preallocated lists: nothing allocates on
the real-time thread (tested under the hook). State is `clap.state` over a byte
vector, base64 in the project like VST3 state; presets are that blob in a
`.clappreset` file under `Documents/CLAP Presets/<vendor>/<plugin>`. The GUI
(`clap.gui`, win32) is embedded by `ui::ClapEditor`: a plain child window of the
JUCE window that JUCE keeps positioned over the component becomes the plugin's
parent, so the GUI sits under the window's bar; the plugin's resize requests are
honoured and `set_scale` carries the display scale. The scanner inspects `.clap`
files in the same child process (entry, factory, descriptors) and the registry
records each plugin's format; the browser shows it and filters effects from
instruments. A test effect (`AuralTestGain.clap`, built with the tests) exercises
the host end to end; no third-party CLAP plugin is installed on this machine, so
the GUI path is verified only by construction.

Delay compensation is solved at compile time over the routing graph. Every
channel's chain latency is the sum of its non-bypassed inserts' reported latency;
a track's output lags the timeline by its chain, a bus's by its slowest input plus
its chain (buses are solved in the same dependency order the renderer sums them
in), and the master by its slowest input. Each edge (a channel's output, each of
its sends) gets a fixed stereo `engine::DelayLine` of the difference between what
its destination waits for and what the source already lags, and the renderer reads
a delayed copy for edges that need one. The lines live in an `engine::DelayCache`
keyed by edge and persist across recompiles, so a fader move never empties one. A
line a published snapshot can reference is never resized: a changed delay gets a
fresh line pre-filled with the old one's most recent samples (the audio in flight
continues) and the old line retires with its snapshot. Lines run for muted and
un-soloed channels too (only the sum into the destination is skipped), so
unmuting replays nothing stale, and every line is flushed when the transport
locates. Latency counts whether or not a slot is bypassed, because a bypassed
processor still delays by it (plugins through `processBlockBypassed`, the limiter
through its lookahead): toggling bypass never re-solves the graph. `AudioEngine::outputLatencySamples()` is the
master's lag, which the session adds to the recording offset (a take played
against the compensated output lands that much later), and a plugin that changes
its latency after prepare is noticed on the session's poll and the graph re-solved.
The playhead and the position display follow the heard position (engine position
minus the lag).

Scanning is out of process from day one: `aural_plugin_scan <file>` loads one
file and prints its `PluginDescription`s as XML, and `hosting::PluginScanner` runs
it per candidate on a background thread with a timeout: the child's output is
drained on a helper thread (a JUCE child-process read blocks until the child
exits, so the waiting thread holds the deadline and kills a hung child, which
unblocks the reader). A crash, hang or garbage result blocklists the file (with the reason) until
the user clears the list; a clean result replaces what the registry held for that
file. `hosting::PluginRegistry` (JUCE-free, `plugins.json` beside the user
settings) records each file's modification time, so a rescan only inspects files
that are new or changed; the mixer's Add menu lists the registry and offers a
full rescan, and `--scan-plugins` does the same unattended. Plugin instances are
created on the message thread (during a snapshot compile) and destroyed there
too (the cache prunes from the session's timer); a plugin window is closed before
its insert leaves the model so the editor never outlives the instance.

### MIDI and instruments (Phase 8)

A track is audio or MIDI (`Track::kind`); a MIDI track has `midiClips` of
`MidiNote`s (clip-relative start and length in samples, pitch, velocity) and a
`midiInput` device name (empty = every enabled input). Commands cover clips
(add, remove, move between MIDI tracks, trim with the notes keeping their
timeline place, split, duplicate) and notes (add, remove, set, quantise to a grid
with a strength); the JSON writes notes as compact arrays.

The engine side is `engine::MidiEvent` / `MidiEventList` (a fixed-capacity list
per track, sized at compile) and `InsertProcessor::acceptsMidi/processMidi`: the
first insert in a MIDI track's chain that takes MIDI is its instrument and gets
the span's events with its audio; the rest of the chain, the pan (a stereo
balance applied after the instrument) and the fader follow as on any channel.
`Renderer::scheduleMidi` collects the events for each span: note-ons and
note-offs from the clips at exact sample offsets while playing, the live input
(filtered by the track's device) while the track monitors (armed and not
playing, or recording, or monitor on), and the note-offs a discontinuity
demands. A `MidiRuntime` per track (held notes, where the previous span ended)
outlives snapshots like the channel runtimes, so a stop, a locate, a loop wrap
or play-from-elsewhere ends every held note at the top of the next span without
the engine having to know why the position moved. Live MIDI enters through
`AudioEngine::pushMidiInput` (device threads, a mutex on the producer side of an
SPSC queue, drained once per block and delivered in the block's first segment)
tagged with a per-device-name index the snapshot resolves for each track.

Instruments: the built-in `aural.synth` (16 voices, polyBLEP saw/square,
state-variable low-pass, ADSR; events partition the block so note timing is
sample-exact), hosted VST3 plugins that accept MIDI (the events go into the JUCE
MIDI buffer as raw bytes) and CLAP plugins with a note input port (CLAP note
events, or MIDI 1.0 events when that is the only dialect the port takes, after
the block's parameter events). The test CLAP plugin has a note port that marks
the audio at each note so the host's timing is tested. Multi-output instruments
are not yet routed: extra output buses stay disabled and the main pair is taken.

Recording: an armed MIDI track joins the record session (`addMidiTrack`); the
engine stamps each live event it delivers to that track with its timeline
position and take index on the session's queue (a loop wrap marks a new take
with the previous take's end), and `engine::buildMidiTakes` turns the collected
events into one clip per track and take, pairing note-ons with their offs (a
retrigger ends the first, a note still held ends at the take's end) and moving
the take earlier by `ProjectSession::midiRecordingLatencySamples` (the device
output latency plus the master's delay compensation: the player plays to what
they hear, so the input side of the round trip does not apply). A MIDI-only
recording needs no audio input and writes no files.

Devices: `audio::MidiDeviceLayer` (owned by the audio device layer) opens the
inputs the settings enable (every input by default), forwards their messages to
the engine with the device name, keeps one chosen output open, and re-reads the
device list on JUCE's hot-plug notification; devices are identified by name,
which is what tracks and settings store. Audio Setup shows the inputs as toggles
and the output as a menu. The UI: MIDI clips draw on their lanes with a bar per
note and take part in the arrange editing that applies to them (select, move
onto MIDI tracks only, trim, split, duplicate, nudge, delete); double-clicking
one opens `ui::PianoRoll` in its own window (keyboard, bar/beat ruler on the
timeline, grid at a chosen division, velocity lane; the keys match the arrange
view's). `+MIDI` adds an armed MIDI track with the synth. Launch flags:
`--midi-track`, `--piano-roll`.

### Metronome, count-in, loop and punch

`engine::ClickGenerator` adds a 30 ms decaying sine burst on every beat (1.5 kHz on
bar starts, 1 kHz otherwise) at multiples of the beat length from zero, so the same
code serves the count-in before the start position (negative timeline positions)
and playback. The snapshot carries tempo and time signature.

The transport holds a count-in length, a punch region and the loop region.
`AudioEngine::process` cuts every block into segments at the count-in end and the
punch edges (on top of the renderer's own split at the loop end): during the
count-in the position holds, monitored inputs and clicks are heard and nothing is
captured; capture starts and ends on the exact punch samples. The session rolls the
transport to one bar before the punch-in so the take has a run-up. The loop and
punch regions are one region in the session, set by dragging in the ruler.

### Phase 2 milestone

32 stereo WAVs on 32 tracks on the Universal Audio Volt at 48 kHz / 64 samples:
the debug build (allocation hook live) ran with no real-time violation, no overruns
and no dropouts; the RelWithDebInfo build measured p50 3.9%, p99 3.9%, max 19% of
the 1.33 ms deadline on the development machine (Ryzen 9 class, 15 workers).

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
| 2026-10-19 | Timeline positions are samples at the project rate, not beats | Sample-accurate placement and recording with no rounding; tempo is a display concern until automation and tempo maps arrive (Phase 9). |
| 2026-10-19 | Snapshot retirement by epoch rather than an RT garbage queue | The RT thread advertising the epoch it uses lets the controller free old snapshots without the RT thread ever touching a queue or a free list, and handles the "RT thread never ran" case (nothing is retained). |
| 2026-10-19 | nlohmann::json via FetchContent for project files | Header-only, so it needs no package manager; the project file is small enough that parse speed is irrelevant. |
| 2026-10-19 | Streaming window guarded by position, with a grace period on seeks | A full lock-free ring with per-sample ownership would be over-engineered for one reader and one writer; a guard distance behind the read position plus a few ms of quiescence after emptying the window gives the same safety with trivial code. |
| 2026-10-19 | Files under 30 s load into memory, longer ones stream | Short clips are the common case for recording takes and one-shots, and memory avoids any disk latency; long material streams so project size is not bounded by RAM. |
| 2026-10-19 | Device sample rate vs project rate: empty projects follow the device, others warn | Silently resampling a whole project on a device change would hide a real problem; warning keeps the user in control until per-project rate conversion lands. |
| 2026-10-19 | Peaks are mipmapped by four from 256 samples per bin and cached beside the media | Six levels cover from 5 ms to about 5 s per bin, so every zoom level reads a few bins per pixel; caching next to the file means a project reopened from another folder still finds its peaks, and the cache is small (about 2% of 16-bit audio). |
| 2026-10-19 | Peaks are built at the project rate, not the file rate | Clip positions and lengths are project-rate samples, so peaks in the same units need no scaling in the view; the cost is one cache per rate a file is used at. |
| 2026-10-19 | Per-track buffers live in the snapshot, not in a global pool | The snapshot already knows every track and is built on the controller thread, so sizing the buffers there needs no separate pool manager and no RT-side bookkeeping; a snapshot of 32 tracks costs about 4 MB. The engine caps a render call at 8192 samples and splits larger device blocks. |
| 2026-10-19 | Workers woken by a counting semaphore, completion by spinning | A semaphore release is the cheapest non-blocking wake available on Windows; the callback thread spins on the remaining-jobs count because the wait is bounded by one track's render on a time-critical thread. Work stealing and a dependency graph come with buses in Phase 4. |
| 2026-10-19 | Callback load as a histogram of deadline fractions | Percentiles from a fixed histogram need no sorting and no allocation, and a fraction of the deadline is the figure a user actually wants: 50% means half the headroom is gone whatever the buffer size. |
| 2026-10-19 | Recordings are 32-bit float mono WAV per track input | Float matches the engine's pipeline exactly and never clips; one file per input keeps placement and recovery trivial. Stereo inputs become two tracks for now; linked stereo tracks come with the mixer. |
| 2026-10-19 | The record session lives outside the snapshot | A snapshot swap (any edit while recording) must not disturb the rings or restart capture; the session is handed to the real-time thread once and ended by a transport command, so editing during a take is safe. |
| 2026-10-19 | Crash safety by periodic header rewrite plus repair from file size | The standard DAW approach: a valid file at every second boundary, and the trailing data recovered by trusting the file size. A journal or a second file would add complexity without saving more audio. |
| 2026-10-19 | Recordings placed by calibration when it matches, else reported latency | The measurement is the truth when it exists for this device/rate/buffer; falling back to the driver's figures keeps placement roughly right instead of silently wrong. |
| 2026-10-19 | Count-in and punch handled by segmenting the block on the real-time thread | A count-in that ends on a block boundary would be up to one buffer late (1.3 ms at 64 samples, 20 ms at 1024); segmenting keeps capture sample-accurate at any buffer size with no extra state. |
| 2026-10-19 | One region serves both loop and punch | Two overlapping regions on a ruler are hard to read and rarely differ in practice; the toggles decide what the region means. Separate regions can come with the editing phase if users ask. |
| 2026-10-19 | Worker pool batch state packed into one atomic | Review found that separate count/next fields let a worker waking late for batch A compare its stale index with batch B's count and run a job against a dead context. One 64-bit fetch_add makes index and count inseparable; counting wake permits keeps the semaphore within its limit however many spans a block issues. |
| 2026-10-19 | One streaming window per clip, not per file | Two clips of one long file (or a loop longer than the window) share nothing but the decoded file; a shared window seeks back and forth and plays silence. A private window per clip costs one file handle and a few MB each, cached by clip id so edits do not reset it. |
| 2026-10-19 | Own Hermite resampler instead of juce::LagrangeInterpolator | The Lagrange interpolator carries a ~2.5-sample lookback delay and sequential state, which misaligned imported media and made chunked seeks awkward. A stateless resampler evaluated at exact positions is simpler and sample-aligned; a windowed-sinc converter for offline bounces is still planned for Phase 9. |
| 2026-10-19 | Loops shorter than 64 samples are refused | Every loop pass inside a block costs a render span and a worker wake; a one-sample loop would issue hundreds per block for no musical purpose. |
| 2026-10-19 | Takes are split by the writer at marks from the audio thread, into one file per pass | The audio thread must not open files; marking the cumulative sample count is one lock-free push, and the writer, which already owns the files, cuts them exactly there. One file per take keeps every take a plain clip. |
| 2026-10-19 | Dropped record blocks become silence in the file, not a shorter file | A shorter file would shift every later sample and take boundary; silence of the right length keeps placement exact and the dropout audible and reportable. |
| 2026-10-19 | A recording sidecar per input, written before capture | Only the session knows the track, start, loop and latency, and after a crash it is gone; a small JSON next to the file lets recovery place the take instead of guessing zero on a new track. |
| 2026-10-19 | Crash recovery asks before importing | A recovered take is usually wanted, but silently adding tracks to whatever project opens next (possibly an unrelated one) would surprise; the prompt keeps it a one-click choice and sets aside what is declined rather than deleting it. |
| 2026-10-19 | 16-input soak is offline, not on hardware | The Volt has four inputs and the engine's record path is identical for synthetic and device inputs after the callback; the offline soak exercises rings, writer and header rewrites at eight times real time, which is a stricter disk test than real time. |
| 2026-10-19 | Tracks and buses share one Channel type and one set of fader commands | The fader section, routing and sends are identical on both; one command per property keeps undo uniform and the mixer draws both with the same strip. |
| 2026-10-19 | Solo-in-place propagates both up and down the routing | Soloing a bus should play everything feeding it and soloing a track should keep its bus and the master alive; resolving this at compile time keeps the real-time summing a plain flag check. |
| 2026-10-19 | Bus summing on the callback thread, tracks in parallel | Bus count is small and their dependency order is serial anyway; parallelising tracks (the many, heavy nodes) captures the benefit without a dependency-aware scheduler. Buses join the pool when inserts make them heavy. |
| 2026-10-19 | Meters as peak-hold plus last-block RMS in atomics | The UI needs a peak that cannot be missed between frames and a level that falls naturally; holding the peak until read and letting the UI apply ballistics gives both without any queue. LUFS needs K-weighting and a 400 ms window and is a separate pass. |
| 2026-10-19 | Insert parameters are atomics on a persistent instance, not part of the snapshot | Recreating a filter on every edit would reset its state and click; pushing values into atomics keeps the instance and its memory while the snapshot stays immutable. Sample-accurate parameter changes come with automation in Phase 9. |
| 2026-10-19 | Built-ins live in src/plugins behind the engine's insert interface | The engine must not depend on any particular processor, and VST3 hosting (Phase 5) implements the same interface; the app injects the factory. |
| 2026-10-19 | Fader groups are commands over channel values, not engine state | The engine already consumes plain channel gains; a group only needs to move several of them together, and doing that in a command keeps the snapshot unchanged and undo exact. Relative (ratio) moves preserve the members' balance, which is what a fader group is for. |
| 2026-10-19 | Live values travel as targets the renderer ramps to; structural edits recompile | A fader move must glide, not step, and must not rebuild the graph on every mouse-move; per-channel runtime state that outlives snapshots gives both, and the same state later carries automation. |
| 2026-10-19 | Routing cycles are refused at the command layer | Letting the compiler silently rewire a cycle shows the user one graph and plays another; refusing the edit (and not offering it) keeps the mixer honest. |
| 2026-10-19 | Edits are previewed in the view and committed as command groups | The view owns the drag state and draws the preview itself, so a drag issues no commands until release; the release becomes one undo step however many clips it touched. The model never sees a half-finished edit. |
| 2026-10-19 | Fades are linear | Linear fades crossfade at constant sum for correlated material (the common editing case: the same take split and rejoined); an equal-power option can come with the design pass. |
| 2026-10-19 | CLAP is hosted against the C ABI, VST3 through JUCE, both behind one insert interface | JUCE does not host CLAP, and the insert interface already hides the format from the chain, the cache and the UI; a "clap:" type beside "vst3:" needed no change in the engine. The thread rules are the spec's, answered truthfully by the thread-check extension because plugins assert on it. |
| 2026-10-19 | Plugin scanning runs in a separate executable, one file per process | A plugin that crashes or hangs while being inspected must not take the DAW with it; a process per file isolates the damage, lets a timeout kill it, and leaves the file blocklisted with the reason. |
| 2026-10-19 | A hosted plugin's state is an opaque blob the plugin owns | Mirroring every plugin parameter into the model would fight the plugin's own editor and presets; capturing its state on save and restoring it on load is what every host does and keeps the model small. Automation (Phase 9) adds parameter access on top. |
| 2026-10-19 | Insert chains are stereo pairs; plugins get a stereo main bus and no sidechain yet | The engine's channels are stereo and the chain interface is two pointers; asking every plugin for stereo in and out keeps hosting simple and correct for effects. Sidechain routing needs a graph change and comes later in Phase 5. |
| 2026-10-19 | Delay compensation per graph edge, solved at compile, lines cached across compiles | Delaying edges rather than nodes handles a source that feeds several destinations with different needs (a send to a latent bus and a direct output); solving at compile keeps the real-time path a fixed delay per edge; caching the lines by edge is what makes a recompile on every edit inaudible. |
| 2026-10-19 | Sidechain keys are pre-fader and tracks render in dependency levels | A key should not change when the source's fader moves, and a keyed track must see its source finished in the same block; levels keep the parallel render for everything unkeyed and cost nothing when no sidechain is set. Bus sources for track inserts would need one block of latency and are not offered. |
| 2026-10-19 | MIDI note-offs on stop, locate and loop wrap come from the track's own continuity check, not from transport commands | The renderer sees every span's start position; comparing it with where the previous span ended catches every discontinuity (a locate, a wrap, play from elsewhere, a stop) in one place, so no transport path can forget to end a note. |
| 2026-10-19 | MIDI events are a fixed list per track per span, sized at compile | An instrument reads the span's events with the audio in one call; building the list in the track's render job keeps it off the shared path and allocation-free, and the instrument sees the events in time order. |
| 2026-10-19 | Recorded MIDI is placed by the output-side latency only | A played note arrives when it is played, but the player plays to what they hear, which lags by the output path; subtracting the input side too (as for audio) would place MIDI early. |
| 2026-10-19 | MIDI devices are identified by name | The name is what the user recognises and what a project can carry between machines; JUCE's identifier is stable but opaque. Two devices of one name are both opened and both heard. |
| 2026-10-19 | A built-in synth ships with the engine | A MIDI track must make a sound without a third-party instrument, both for the user and for offline tests of the whole path (schedule, instrument, bounce). |
| 2026-10-19 | Trimming a MIDI clip keeps its notes where they are on the timeline | Matches audio clips (the source stays put under the clip edges) and lets a trim be undone by trimming back without losing notes. |
| 2026-10-19 | The limiter has no lookahead (superseded the same day: lookahead added once delay compensation landed) | Lookahead adds latency that needs delay compensation across the graph, which Phase 5 brings with plugin hosting; until then a fast-release limiter with a hard ceiling is honest about what it does. |
