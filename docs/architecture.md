# Architecture notes

Living document. The intended architecture is in [PLAN.md](PLAN.md), section 3; this
file records what is actually built and the decisions made along the way.

## Current state (Phases 6 and 8 complete; Phase 7, the design spec, in progress: design system and shell done)

```
aural.exe
  src/app         JUCEApplication; MainWindow (the shell: header bar, arrange
                  view, dock and side panel hosts, status bar, toasts, sheets,
                  the keyboard map, menus, Audio Setup window);
                  ProjectSession (the open Project + UndoStack + AudioEngine +
                  DiskThread + PeakStore; recording flow and recovery);
                  UserSettings (per-user properties file)
  src/ui          Theme (design tokens), AuralLookAndFeel (dark, token-driven),
                  Icons (the icon set as paths), components/ (Fader, Knob, Meter,
                  SegmentedControl, NumericField, PanelHeader, Toast, Sheet),
                  shell/ (HeaderBar, StatusBar, Panels: hosts, floating windows,
                  the PanelManager), panels/ (EditorPanel, InspectorPanel,
                  DiagnosticsPanel), strip/ (the channel strip's module cards
                  and their real-time displays),
                  AudioSetupView (audio and MIDI devices), ArrangeView (tracks,
                  waveforms, MIDI clips, zoom, scroll, editing), TrackHeader
                  (input or MIDI input, arm, monitor), MixerView (strips, master,
                  inserts menu), InsertEditor (generic parameter editor),
                  PluginBrowser (search, filters, sections, favourites, scan),
                  MediaBrowser (the browser's Files tab: places, audio files,
                  previews, drags),
                  PluginWindow (the bar over a hosted plugin's editor),
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
  src/plugins     Built-in processors (gain, EQ, compressor, limiter), the
                  built-in synth and the channel strip modules (gate, parametric
                  EQ, compressor, de-esser, saturation, limiter) behind the
                  insert interface. No JUCE.
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

Auditioning a file (the browser's Files tab) is `engine::PreviewPlayer`, owned by
the engine and mixed into the device outputs after the render: over the mix and
outside it (no fader, meters, recording or bounce), at its own gain. The session
loads the file with the same loader as clips (a long file streams through its own
disk window) and hands the source over in an `rt::SnapshotPublisher`. Nothing is
cut: a stop or another file sets the id to fade, the audio thread fades it out
over 5 ms and reports it done, and only then does `pump()` (the engine's garbage
collection, and the browser's timer while it previews) publish the next source,
which fades in. The source is released on the controller side once the audio
thread has moved past it.

### Session and UI

`ProjectSession` ties the pieces together on the message thread: it owns the
project, the undo stack, the disk thread, the loader and the engine, republishes the
snapshot after every command, imports files as new tracks, saves/opens JSON project
files and drives the transport. An empty project adopts the device sample rate; a
project with media keeps its own and the mismatch is shown.

Autosave: while the project has unsaved changes the session writes it to
`<settings folder>/Autosave` every interval (Preferences, General; 2 minutes by
default, 0 off), once edits have paused for a second and no gesture is open.
The copy is named after the project file (name plus a hash of the path) or, for
an unsaved project, `Untitled-<id>`; it is written to a temp file and moved over
the last one, with a JSON sidecar naming the project file and the time. Saving,
starting or opening another project and a normal close delete it, so whatever
is in the folder at launch was left by a session that did not close; the main
window offers the newest with a toast (Restore / Discard). A restored project
belongs to its file but stays unsaved until the user saves it.

Templates are project files in `Documents/Aural/Templates` with the clips, MIDI
clips, markers and automation points stripped (and nothing armed); "New from
template" loads one as an unsaved, untitled project. Media management works on
the project's media folder (`<project> Media`): `mediaReport` lists media used
from outside it, files in it no clip uses (unfinished `.partial.wav` recordings
excluded: they belong to recovery) and missing files; `consolidateMedia` copies
the outside media in (renaming on a clash) and repoints the clips with one
`SetMediaPaths` command; `removeUnusedMedia` moves unused files and their peak
caches to the Recycle Bin rather than deleting them, because an undo can bring
back a clip that used one. `MainWindow` is the
shell of 03-shell.md (see "UI" below).

Selection has one owner, `ui::Selection` (in the shell): the selected channel
(a track, an FX track or a bus) and the audio clip on it the inspector shows.
Everything that selects calls the shell's `select`; the selection notifies one
listener, which brings the arrange view, the Editor panel, the inspector and the
mixer to it, so no view can be left on another channel. After every structural
change it validates itself: a channel that is gone falls back to the first track,
a clip that is gone is dropped. The arrange view keeps its own set of selected
clips for editing and reports the one in focus.

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

`ui::ArrangeView` is laid out to 04-arrange.md: a 220 px header column (the
boundary drags 160..360), a 32 px ruler whose top 14 px row holds the marker
flags and the punch band and whose bars row holds the numbered bar lines, beat
and sub-beat ticks (shown once a beat is 12 and 48 px wide), the loop band with
draggable ends and the time selection; lanes at the row height (28, 56 or 112
px from the corner stepper, or the track's own height from 28 to 240 px: drag a
row's bottom edge under its header or across its lane, double-click the edge to
go back to the default; `Channel::height`, saved with the project, set by the
undoable `SetChannelHeight`) with alternating fills, the grid under the clips,
hover and selected fills and loop/punch bands at half alpha. Clips are drawn in
the track's palette colour: an 18 px header at the header alpha with the name,
the body at the body alpha, the waveform (one min/max column per pixel from the
peaks, scaled by the clip gain) at the waveform alpha over a centre line, a
hatched body and dimmer waveform when muted, fade wedges under a light curve,
8 px fade handles on hover or selection, an outline at 40/70/100% for
rest/hover/selected, and a warning outline with "Missing: file" when the media
is absent (checked once per project change). The playhead is a 1 px line with
a head in the ruler; the view follows it by pages (toggled in the ruler's menu).
Zoom: Ctrl+wheel around the pointer, a vertical drag in the ruler, Z to fit,
Shift+Z to the selection; the wheel scrolls time and Shift+wheel the tracks.
Drags show the snap guide and a tooltip (new position and delta in beats, the
trimmed length, the fade, the gain); files dragged over light the lane and show
a ghost clip with the file name, and dropping below the last lane makes a track
per file. `ui::TrackHeader` has the stripe (click: colour menu), number (drag:
reorder with an insertion line, through `core::MoveTrack`), name (double-click
renames inline), M/S/R and monitor toggles, then the input combo, a horizontal
meter fed from the mixer's last readings and a compact volume slider, and at
the large row height pan, output and a sends summary; right-click opens the
track menu. The corner holds the add-track menu and the row-height stepper.
Not in this pass: buses and the master as arrange rows, take badges, the live
recording clip, a split tool mode (S splits at the playhead, Shift+S at the time
selection or loop), track duplication.

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

Live view (04-arrange.md "Recording clip"): the writer keeps min/max peaks of the
samples it has put in each take's file (`LiveTake`: a 256-sample level and a
16384-sample one, under its own mutex, appended once per chunk), and the record
session drains its MIDI events and take marks into controller-side lists on
demand (`capturedMidi()`, `takeSpans()`), so both can be read mid-take. Once per
UI frame `ProjectSession::updateLiveRecording` brings the arrange view's
`engine::LiveRecording` up to date in place (a known take gets only its new bins)
and the view draws each audio take as a growing clip in `record`, placed with
`placeRecording` where its clip will land, and each MIDI take as the clip it will
become (the current take even before a note). The same call collects a finished
recording, so the clips replace the live takes in the frame the files close.

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

`ui::MixerView` is the console of 05-mixer.md, a panel of the shell (the dock
by default, F2). One 84 px strip per track and bus, 2 px apart, buses after a
gap, the 112 px master pinned at the right behind a separator. A strip, top to
bottom: the name cap (track colour at the header alpha with the full-colour
stripe, the fader-group bar and number; click selects the channel in the
arrange view and back; double-click renames; right-click: fader group, colour,
remove bus), the input combo (audio or MIDI inputs, tracks only), the insert
slots (20 px: name, VST3/CLAP badge, a strike when bypassed, the power icon and
drag handle on hover; drag reorders with an insertion line; click opens the
editor; right-click: open, bypass, move, wet/dry, sidechain, outputs, remove;
a ghost "+" opens the add menu; the box scrolls past six), the send rows (name,
a 40 px level slider, the pre/post dot; "+" adds one; right-click removes),
the pan knob, M/S/R/phase toggles, the `Fader` with a `Meter` beside it, the
dB `NumericField` and the output combo (cyclic targets greyed). The fader
travel takes the height left between 120 and 200 px; below 120 the inserts and
sends collapse to one-line summaries. The master has the fader, a scaled meter,
the value field and the M/S/I loudness readout (I lights in the target colour
within 1 LU of the target). Strips feeding a soloed bus get a dim solo outline;
the selected strip an accent outline; Tab focuses a strip, arrows move its
fader, M/S/R toggle it. The default dock height is 480 so the fader has its
full travel. Not in this pass: the 112 px wide strip mode, master inserts (the
model's master has none), solo-safe.

The command pipeline: every edit goes through `ProjectSession::run`, whether
it comes alone (`execute`), as a named group (`executeGroup`), from a finished
recording, a recovery or an automation pass. `run` asks the command, not its
type, what it needs: `setDefaultStrip` (a new track or bus gets the channel
strip), `automationWrite` (a fader, pan or parameter move recorded while
writing), `capturesInsertState` (removing or replacing an insert pulls the live
plugin state first), then applies it through the undo stack (a stale command,
one whose track is gone, is dropped). The engine work follows its
`Command::ChangeKind`: cosmetic (a row height: none), parameters (pushed to
their processors), values (every channel's live values pushed) or structure (a
new snapshot). A `ProjectSession::Batch` makes several runs one undo step and
does the engine work once, for the widest kind among them; undo and redo use
the undone command's kind. Undo groups nest (an inner group joins the open one).
The paths that make a MIDI track with a new instrument (the "+" menu, the
Instruments panel, the browser, "Plays... New instrument") all go through the
session (`addMidiTrack`, `playNewInstrument`), which names and sets up the pair
in one place. A test applies, undoes and redoes every command and compares the
whole project each time.

Live values and smoothing: a fader, pan, master or insert-parameter command is
live (`ChangeKind::values` or `parameters`). The session applies it to the model (undo) and pushes
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
(the channel strip's compressor, `strip::createKeyedCompressor`: peak or RMS
detection, soft knee, attack and release on the gain reduction, makeup plus auto
makeup, a parallel mix, and its detector keyed from a sidechain when one is
chosen; one compressor serves both, and a project saved with the older six-control
insert compressor loads its settings by key with the new controls at their
defaults) and Limiter (a lookahead limiter: the
signal is delayed by the lookahead while the gain the coming samples need is
known, taken as a running minimum over the window with release, smoothed by a
moving average of the same length, so nothing crosses the ceiling and the gain
ramps instead of stepping; it reports the lookahead as latency for delay
compensation). `ui::InsertEditor` is the generic parameter editor of
07-plugins.md 7.3: 28 px rows of name, knob and an editable value field, a
footer with the latency and the key input, in its own window for a built-in and
embedded under the bar of a `PluginWindow` for a hosted plugin without a GUI.

`engine::LoudnessMeter` implements BS.1770 K-weighting (the RLB stage with the
standard's unnormalised numerator, so a full-scale 997 Hz sine in one channel reads
-3.01 LUFS) with 400 ms momentary and 3 s short-term windows on the master mix, and
integrated loudness per BS.1770-4: the 400 ms window at every 100 ms hop is kept
(two hours' worth, preallocated) and the controller computes the gated mean on
demand (absolute gate -70 LUFS, relative gate 10 LU under the mean of the rest).
Play from stop restarts it.

### FX tracks

An FX track is a `core::Bus` with `fx` set: the mixing, sends (pre or post
fader), delay compensation, solo and automation are a bus's, so nothing in the
engine changed. The difference is presentation and creation:
`plugins::addFxTrackCommands` makes one with its effect as the first insert
(fully wet, since the dry signal stays on the senders), its id taken from the
project's next id so sends in the same group can name it, plus a post-fader
send at -6 dB from each given channel, run as one undo step. The arrange view
shows FX tracks as rows after the tracks (`rowCount`/`channelAtRow`), each with
a `TrackHeader` fed a copy of the bus as an aux-kind track (no input or arm), an
empty lane counting its senders, its bus meter and the automation overlay
(which now works on any channel row). The built-in Reverb (an eight-line
feedback delay network after pre-delay, low cut and four diffusing allpasses,
with damping and width) and Delay (gliding time, tone, ping-pong) were added so
an FX track is useful without a plugin.

### Channel strip

Every track and bus has a channel strip (design: [design/12-channel-strip.md](design/12-channel-strip.md)):
six built-in modules switched on in place rather than added. In the model they
are `core::Insert`s in `Channel::strip`, beside `Channel::inserts`, with
`bypass` meaning off and `stripPreInserts` placing the strip before or after the
inserts. `Channel::findInsert` searches both lists, so the insert commands
(parameter, bypass, mix) apply to modules unchanged and undo needs nothing new;
`MoveStripModule` and `SetStripPosition` reorder. `Channel::processingChain()`
gives the processing order, and the snapshot compiler and the live-value push
walk it instead of `inserts`. The insert slots, inserts menu, sidechain and
everything else that lists inserts still read `inserts` only.

New channels get the strip from the session: `ProjectSession::execute` hands
every `AddTrack` and `AddBus` the default strip (`plugins::strip::defaultStrip`,
every module off with its defaults) before it applies, and the command
allocates the module ids and keeps them, so a redo brings back the same ids the
later commands in the history refer to (the same now holds for a new track's
inserts, which previously kept id 0). Opening a project runs
`ensureChannelStrip`, which adds any missing module with a fresh id, so older
projects (and modules added later) need no migration and no undo step.

The modules (`src/plugins/ChannelStrip.cpp`) share `ProcessorBase` (moved out of
`BuiltIns.cpp`: atomic parameters, the bypass crossfade, chunking) and the RBJ
biquads in `Biquad.h`, which also compute magnitude responses so the editor
draws exactly the filter that runs. Gate: key high-pass and low-pass, a peak
detector, hysteresis and hold, the gain gliding between unity and the range.
EQ: twelve biquad slots in a fixed layout (four per cut, Butterworth sections
for 6 to 48 dB/oct; one per band), identity slots skipped, coefficient ramps
across the block, a section's memory cleared when it switches on. Compressor:
peak or RMS detector, the soft-knee curve applied to the level and the
reduction smoothed with attack and release (smoothing the curve's output, not
the level, keeps a waveform's troughs from dragging the envelope down), make-up
with an auto amount, internal wet/dry. De-esser: an LR4 crossover so the bands
sum flat, the band's envelope over the threshold setting a 5:1 reduction capped
at the range, on the band or the whole signal. Saturation: drive into tanh,
biased tanh or a cubic clip with first-order antiderivative anti-aliasing (no
oversampling, so no latency), a DC blocker, a tone shelf, a trim of half the
drive and wet/dry. Limiter: zero-latency peak limiting (gain drops at once,
recovers at the release rate, the output clamped), so a strip that is off never
adds latency.

Presets: `plugins/StripPresets` holds the factory presets (each built from the
default strip, every module off, with the modules and values the preset uses)
and `SetStripState` applies one: each given module's bypass and parameters go
to the channel's module of the same type, ids and order untouched, one undo
step. `ui/strip/UserStripPresets` reads and writes the user's presets as JSON.

Real-time feedback is `engine::ProcessorFeedback` (`InsertProcessor::feedback()`):
the audio thread publishes a frame every 5 ms (input and output peaks, gain
reduction, one module-specific level) into a 1024-frame ring of atomics and, for
the EQ, the mono sum before and after it into two 16384-sample taps that only
run while an analyser asks. A module that is off measures its input only while
an editor is watching it (`setWatched`), so a hidden or collapsed card costs
nothing. Readers copy what is newer than their cursor; a frame being overwritten
while it is read only ever shows on a display.

The UI is `src/ui/strip`: `ModuleCard` (the card: header, bound knobs, choices
and pills that send the insert commands, the level history and meter ballistics
read on `tick`), the displays in `Graphs` (`HistoryGraph`, `TransferCurve`,
`SaturationCurve`, `LevelBars`) and `EqGraph` (draggable band nodes over the
response and a JUCE FFT analyser), the six cards in `ModuleCards.cpp`, and
`ChannelStripView`, which the Editor panel places before or after the insert
cards in signal order and which ticks its cards at 60 Hz while showing. Cards
of modules that are off collapse to tabs. The mixer strips get a row of module
cells that switch modules in place and show gain reduction.

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

Robustness. A hosted plugin's processing call runs through
`hosting::callGuarded` (structured exception handling on Windows): a crash
inside it (an access violation, a thrown exception) is contained, the plugin
is never called again and its slot passes audio through, and the session tells
the user once to save and restart (`ProjectSession::checkCrashedPlugins`); the
plugin's memory may be damaged, so it stays off. A CLAP plugin's audio-side
calls are claimed by whichever thread makes them (`HostedClapPlugin::Claim`, a
compare-and-swap, no waiting): a block that finds a main-thread flush in the
plugin passes its input through, a flush that finds a block running leaves the
queue to it, so the two never overlap. The scanner waits for each child process
in 100 ms slices, so cancelling (quitting mid-scan) kills a slow plugin's
inspection at once instead of the thread being force-stopped, and results it
queued for the message thread are dropped once it is gone.

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
plugin browser (`ui::PluginBrowser`, the Browser panel, `--plugins` for it
floating) is 7.4: a search field, format (All, VST3, CLAP, Built-in) and
category segmented controls, then sections (Favourites, Recent, Effects,
Instruments, Blocked) of 28 px rows with the star, name, manufacturer and
format badge; Enter or a double-click inserts the plugin on the selected track,
a row drags onto the Editor chain (at the insertion line) or an arrange track
(the row lights); a blocked row shows the reason and Retry (the registry forgets
the file and scans again); the scan button's chevron menu rescans or clears the
blocklist, and a progress bar with "Scanning n of m · file" and Cancel replaces
it while a scan runs. Favourites and recent types are user settings. The
Browser panel is `ui::BrowserPanel`: a Plugins / Files segmented control over
the plugin browser and `ui::MediaBrowser` (7.4g; the tab is a user setting).
The Files tab browses a place (the saved project's folder, Music, or a folder
added to its list, kept in the settings with the last folder) and its
subfolders; a background job (one `juce::ThreadPool` thread, a generation count
cancelling a stale one) lists the folder, or with a search walks the whole place
for names holding every word (up to 2000 results), and reads each file's header
for its length and rate, posting the result back to the message thread. A click
auditions a file through `ProjectSession::previewFile` (the engine's
`PreviewPlayer`, see Media and streaming; the panel pumps it on a 30 Hz timer
while one plays, and shows the progress under the row), a second click, another
file, Escape or Stop ends it; double-click or Enter imports the file onto a new
track at the playhead; a row drags as "file:<path>", which the arrange view
takes like a file from Explorer (a new track where it lands, with the same drop
preview) and the Sampler's waveform loads. Launch flag: `--browser-files
[folder]`. The `PluginWindow` bar is 7.2: power, "Plugin · Track", the format badge, the 80 px
mix slider with its value, the presets menu and pin; position and editor size
are remembered per plugin type. The Editor panel's cards (7.1) show an accent
dot while the editor window is open, reorder by dragging their header, take
plugin drops, read "Not installed" with a warning when the plugin is missing,
and the "+" card's menu lists recent, built-in, favourite and then every
scanned plugin by manufacturer. Not in this pass: the generic-editor toggle for
plugins that have both editors, a failed-load toast with a Blocklist action.

Sidechains: an insert slot can key from another channel (`Insert::sidechain`,
`SetInsertSidechain`, the inserts menu's Sidechain submenu, offered when the live
processor `acceptsSidechain()`, and the compressor card's Sidechain button; both
build their items with `ui/SidechainMenu.h`). The key taps the source before its fader (its
chain output; the fader does not matter) or, with `Insert::sidechainPostFader`
(the submenu's Pre-fader / Post-fader), after it, so it follows the fader and its
automation: the renderer writes a post-fader copy of any channel a post-fader key
reads once its chain has run (`ChannelNode::keyPostFader`, `postLeft/Right`).
Either way mute wins, since a muted channel's key is silence. Keys are delay
compensated: a key that reaches its slot earlier than the slot's own audio (the
source gathered less latency than the keyed channel's input plus the inserts
before the slot) runs through a delay line for the difference
(`ChannelNode::insertKeyDelay`); a key that arrives late is used as it is, since
lining it up would add latency to the keyed channel. A track may key from a track; a bus from a track or from a bus. To make
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
note-ports (CLAP and MIDI 1.0 dialects), gui and timer-support. The library and
its entry point are shared by every instance of one file and released with the
last (a per-instance init/deinit would break plugins that count them).

Ports are negotiated once, at creation: every audio port's channel count and
in-place pair are read; the main input must be mono (fed the summed pair) or
stereo, the main output mono (copied to both sides) or stereo, a stereo second
input port is the key, and every other port is fed or drained through silent
scratch so the plugin always sees exactly the buffer and channel counts it
declared (a four-channel main port is refused with a message, never indexed
past). The output is in place on the chain's pair only when the plugin pairs the
ports (`in_place_pair`); otherwise it gets its own output buffers. The input is
copied before every call: a process error passes it through, and bypass
crossfades to the dry signal delayed by the plugin's latency (a ring of history
fed every block) so a bypassed lookahead plugin stays where the graph's
compensation expects it; a failed activation reports zero latency.

Threads follow the spec: main-thread calls (init, activate, state, gui, rescans)
on the message thread; audio-thread calls (start/stop_processing, process) on the
real-time thread. Host parameter changes and gestures (the generic editor's
drags send `PARAM_GESTURE_BEGIN/END`) are queued on an SPSC queue and become
sample-0 events of the next block; while the engine is not running the session's
poll flushes them through `params.flush` (as an audio-thread call under the
pretend flag when the plugin is active, after leaving the processing state),
so a value set with the device stopped reaches the plugin and its state. The
poll also services every live instance's requests made from the audio thread
(`request_callback`, latency changes, GUI closures); a `request_callback` from
the message thread is answered through an async message that checks the
instance is still alive. `params.rescan` while active never reallocates the
tables the audio thread walks: values and texts are refreshed in place, and a
change of the parameter set (or `RESCAN_ALL`, legal only when inactive) becomes
a restart request. `request_restart` (and an audio-port rescan) is honoured by
the insert cache: the next compile creates a fresh instance with the old one's
state and the snapshot takes it, so the real-time thread never sees a
deactivation; a plugin window on the old instance closes. The plugin's own
parameter events update the host's view.

A hosted plugin owns its parameters: the model's parameter list is not pushed on
compiles or on live commands, only the parameters a command touched (set, undo,
redo) reach the instance, and the model's list is brought up to date from the
instance before such a command so undo restores the real previous value. A
preset loaded from the plugin's own menu therefore survives fader moves, edits
elsewhere and a save/reload. State is `clap.state` over a byte vector, base64 in
the project like VST3 state; presets are that blob in a `.clappreset` file under
`Documents/CLAP Presets/<vendor>/<plugin>`. The GUI (`clap.gui`, win32) is
embedded by `ui::ClapEditor`: a plain child window of the JUCE window that JUCE
keeps positioned over the component becomes the plugin's parent, so the GUI sits
under the window's bar; `can_resize` is asked only once the GUI exists, the
plugin's resize requests are honoured, `set_scale` carries the display scale,
and a GUI the plugin closes itself (`host.gui.closed`) is destroyed by the host
when the plugin did not, and its window goes. The scanner inspects `.clap` files
in the same child process (entry, factory, descriptors) from
`%COMMONPROGRAMFILES%\CLAP`, the per-user folder and every `%CLAP_PATH%` entry; a
file whose entry refuses to initialise is blocklisted with the reason rather than
recorded as empty; the registry records each plugin's format and the browser
filters effects from instruments. The test file (`AuralTestGain.clap`, built with
the tests) holds five plugins: the stereo in-place effect with a key and a note
port, a four-channel one (refused), a two-output one (drained), one without an
in-place pair that swaps its channels (separate buffers), and one that asks for
callbacks, rescans and restarts from the audio thread. No third-party CLAP plugin
is installed on this machine, so the GUI path is verified only by construction.

Sidechain keys are refused (and greyed in the menu) when they would form a loop
through outputs, sends or other keys (`Project::keyWouldCycle`). A channel heard
only through a send still meters what it sends. `DelayLine::continueFrom` reads
the line it replaces on the controller thread while the real-time thread may be
writing it: at worst one block of the new line's first delay samples is
mis-ordered, which is inaudible against the splice it avoids and accepted as is.

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

### Automation (Phase 9)

`core::AutomationLane`s live on the channel (`Channel::automation`, with
`automationMode`): a target (gain, pan, or an insert or strip module's
parameter), sorted breakpoints in the parameter's own units, and
`automationValueAt` (a binary search, linear between points, gain in dB, held
outside) that the engine and the UI share. Commands add, remove and replace a
lane's points and set the mode; lanes and the mode are in the project file.

Playback: `compileSnapshot` copies the lanes of a channel whose mode reads
(read, touch, latch) into `AutomationNode`s, skipping hosted plugins (they own
their parameters) and lanes for missing inserts. `Renderer::renderSpan`
evaluates every lane at the span's end before any thread renders: gain and pan
become the span's ramp targets (`ChannelRuntime::automatedGain/Pan`, overriding
the static targets while a lane plays), an insert parameter is set on its
processor (an atomic). A per-lane `AutomationRuntime::touched` flag (outliving
snapshots, set by the controller) skips a lane while it is being written. The
runtime also publishes what played (`shownGain`, `shownPan`, `automated`) so the
mixer's controls follow. Lanes play when stopped too, at the playhead.

Writing (`ProjectSession`): while the transport plays, a `SetTrackGain`,
`SetTrackPan` or built-in `SetInsertParameter` on a channel in touch, latch or
write mode is captured instead of executed: the value goes straight to the
engine (`setLiveGain`, `setLivePan`, the processor), the point is appended to a
pass with the engine position, and the lane is marked touched. Touch passes end
with the gesture, latch and write passes at the stop (or when the session's
timer sees the transport stopped); write mode also starts a pass for each of the
channel's lanes when playback starts. A pass replaces the lane's points over its
range (a 10 ms ramp in from the curve, a 50 ms glide back for touch, the last
value held to the stop for latch and write), or creates the lane, as one
"Write automation" undo step; a loop wrap closes the pass so far. The model's
static values and the undo history are untouched while writing.

Hosted plugins own their parameters (a CLAP plugin's setParameter queues for
its process call from one producer, the controller thread), so their lanes are
not compiled into the snapshot: `ProjectSession::playHostedAutomation` runs 50
times a second on the message thread, evaluates those lanes at the engine
position and sends each changed value to the instance; a lane being written is
skipped. Writing a hosted parameter (through the generic editor's commands)
records like any other. A move in a plugin's own window reaches the session as
`InsertProcessor::ParameterEdit`s (value, gesture begin and end): a VST3 through
JUCE's parameter listeners (on the message thread, or the audio thread, which
only tries the lock and drops on contention; the host's own setValue is not
mistaken for an edit), a CLAP through its output events (an SPSC queue whose
one producer is process() or, while nothing processes, a flush). The same 50 Hz
pass drains them: while writing they record without being sent back to the
plugin, and a gesture end closes a touch pass; otherwise the plugin keeps them.
`HostedPlugin` lists a VST3 plugin's automatable parameters as normalised
values through JUCE's parameter objects (message thread only), and mirroring a
plugin's parameters into the model before a command only reaches up to the
touched parameter, since a plugin can have thousands.

UI: `ui::AutomationOverlay` covers the arrange view and takes the pointer only
over rows whose header's A toggle is on; it draws the chosen lane over the row
and edits points (04-arrange.md, Automation). The mixer strip asks for the
automated values each meter frame; strip cards follow their processor's
parameters.

### MIDI and instruments (Phase 8)

A track is audio or MIDI (`Track::kind`); a MIDI track has `midiClips` of
`MidiNote`s (clip-relative start and length in samples, pitch, velocity) and a
`midiInput` device name (empty = every enabled input). Commands cover clips
(add, remove, move between MIDI tracks, trim with the notes keeping their
timeline place, split, duplicate) and notes (add, remove, set, quantise to a grid
with a strength); the JSON writes notes as compact arrays.

The engine side is `engine::MidiEvent` / `MidiEventList` (a fixed-capacity list
per track, sized at compile) and `InsertProcessor::acceptsMidi/processMidi`. An
instrument lives on an instrument channel (`TrackKind::instrument`), in its
instrument slot (`Channel::instrument`, an `Insert` with an empty type when there
is none), which `processingChain()` puts first, so the first processor that takes
MIDI is the instrument (the engine, automation, parameter commands and instance
cache treat it as an insert; `SetInstrument` sets, replaces or removes it, keeping
the slot's id on a replacement so output tracks stay connected, and taking its
parameter lanes with it). MIDI tracks (`TrackKind::midi`) make no audio: each
aims at an instrument channel (`midiTarget`, `midiChannel`; `SetMidiTarget`), and
`AddInstrumentTrack` adds a MIDI track and its channel as one step. In the
snapshot each instrument channel lists its feeding MIDI tracks (`midiFeeds`) and
renders in a later level than them; a MIDI track only schedules its span's
events, and the channel merges every feed's events, stamped with the feed's MIDI
channel, note-on velocities scaled by its volume (a muted feed passes only
note-offs), and plays them through its chain; its pan (a stereo balance applied
after the chain) and fader follow as on any channel. Projects from before
instrument channels (version 1) are refused.

**Parameter keys and file versions.** A processor's parameters are addressed by
index in memory, but saved with stable keys: each insert writes `paramKeys`
beside `params`, and each parameter lane writes `parameterKey`. A built-in's key
is its parameter's `id`, or the parameter name lower-cased with punctuation as
dashes (`InsertProcessor::parameterKey`); set an explicit `id` before renaming a
parameter. A CLAP plugin's key is `clap:<clap_id>` and a VST3's `vst3:<ParamID>`,
both stable across plugin versions. On load, `core::reconcileParameters` puts
values where the processor wants them now (by key; a new parameter takes its
default, a removed one is dropped) and moves each lane to its parameter's new
index (a lane whose parameter is gone is removed). Built-ins resolve at once;
hosted plugins resolve once the engine has made their instances, so the session
reconciles twice (`ProjectSession::adoptLoadedParameters`). A plugin that is
not installed keeps its keys, so a re-save loses nothing. Core knows no
processor: the app supplies keys through `core::ParameterKeys`. The file's
version upgrades step by step (`migrate` in ProjectJson.cpp) from
`Project::oldestReadableVersion`; only files older than that are refused.
`Renderer::scheduleMidi` collects the events for each span: note-ons and
note-offs from the clips at exact sample offsets while playing, the live input
(filtered by the track's device) while the track monitors (armed and not
playing, or recording, or monitor on), and the note-offs a discontinuity
demands. A `MidiRuntime` per track (held notes, where the previous span ended)
outlives snapshots like the channel runtimes, so a stop, a locate, a loop wrap
or play-from-elsewhere ends every held note at the top of the next span without
the engine having to know why the position moved. Live MIDI enters through
`AudioEngine::pushMidiInput` (device threads, a mutex on the producer side of an
SPSC queue, drained once per block) tagged with a per-device-name index the
snapshot resolves for each track. A device event carries its arrival time on
the steady clock; the block that drains it places it where it arrived within
the previous block's span of time, scaled to this block's length, so a
performance keeps its timing for a constant one-block delay instead of
snapping to block starts. Every piece of a block split at a loop end, a punch
edge or the count-in's end takes the events that fall in it (the last piece
takes any past the end). An instrument's extra outputs are compensated like
its main one: an output track's signal lags by the source chain's latency up
to the instrument, plus its own chain.

Instruments: Aurora (`aural.aurora`, `plugins/Aurora.*`, the default for new
MIDI tracks): twelve voices of two morphing polyBLEP oscillators with up to
seven-voice unison spread across the stereo field, a sub and noise, a drivable
TPT state-variable filter (12/24 dB low-pass, band-pass, high-pass) with key
tracking and its own envelope, coefficients and the LFO at a 16-sample control
rate, glide, and Shimmer, a two-line modulated chorus; it publishes its output
tap, the sounding voices and the LFO through the built-ins' feedback for its
editor, and has eight factory presets. Pulsar (`aural.pulsar`, `plugins/Pulsar.*`)
is a drum synth: a voice per pad (kick and toms a pitch-swept sine with a click,
snare two shell modes and band-passed wires, clap four noise bursts and a tail,
hats six square waves at the 808's ratios and noise through a high-pass, rim an
FM ping), triggered by note-ons only, the closed hat choking the open one; the
same voice code renders a pad offline (`renderPad`) for its editor's pictures,
and `pulsar::activity` gives the editor a hit counter and level per pad. Each pad
routes to the main pair or one of seven extra pairs, exposed as aux outputs (as
many as the highest in use) with buffers allocated in prepare; the master drive
and level apply per output. `InsertProcessor::auxOutputName` lets an instrument
name a pair ("Kick"), which output tracks take as their name. Nebula
(`aural.nebula`, `plugins/Nebula.*`) is a pad synth: per voice up to 24 partials
plus a halo an octave up, each two sines from a shared table (linear
interpolation) detuned by the spread and a slow random drift, their levels set at
a 32-sample control rate from a tilt, an odd/even balance, a stretch and a vowel
formant (with the motion LFO sweeping them) and ramped across the interval; a
sub, band-passed breath noise, a one-pole tone filter and an eight-line feedback
delay network for the space. `nebula::spectrum` gives the editor the same partial
levels for its picture. Starbit (`aural.starbit`, `plugins/Starbit.*`) is a
chiptune synth: naive pulse, 4-bit triangle and saw, a 15-bit LFSR (short mode
taps bit 6) clocked from the note and a 32-step drawn wave; 16-step volume;
arpeggio, vibrato, sweep and glide at a 32-sample control rate from constant
tables (nothing is built on the audio thread); mono with a held-note stack, or
four voices; then sample and hold, bit reduction and an echo. The first built-in
synth (`aural.synth`) is retired: projects that used it open with Aurora in its
place (`ProjectSession::replaceLegacySynth`, before `adoptInstruments`). Every
instrument parses its events at their sample offsets, so note timing is
sample-exact. Hosted VST3 plugins that accept MIDI (the events go into the JUCE
MIDI buffer as raw bytes) and CLAP plugins with a note input port (CLAP note
events, or MIDI 1.0 events when that is the only dialect the port takes, after
the block's parameter events). The test CLAP plugin has a note port that marks
the audio at each note so the host's timing is tested. A multi-output instrument
keeps its further stereo output buses (VST3) or ports (CLAP) and exposes them as
`InsertProcessor::auxOutput` pairs; an aux track (`TrackKind::aux`, with the
source track, insert and output number) carries one such pair through its own
chain, fader, pan and sends, and renders in a later level than its source like a
keyed track. The Instruments panel (`ui/panels/InstrumentRack`) lists every
instrument as a rack unit with its outputs (the main pair is the instrument
track; each further pair is an output track or a chip that makes one), and the
mixer's instrument slot menu offers the same "Outputs"; a source that goes away
leaves the aux track silent.

The Sampler (`aural.sampler`, `plugins/Sampler.*`) plays one sample across the
keyboard: a root note and a key range, tune and fine, velocity to level, an ADSR
on the amplitude (the shared `synth::Envelope`), start and end points, a forward
loop whose last few milliseconds crossfade into the same distance before the
loop's start (so a wrap mid-wave does not click; the fade is as long as the
material before the loop start allows), one-shot playback (to the end, note-offs
ignored), a TPT low-pass per voice with the envelope moving its cutoff (out of
the path at 20 kHz with no envelope), up to 32 voices (over the limit the oldest
fades out in 5 ms, as does a retriggered note) and glide from the last note's
pitch. Each voice reads the sample at its exact position with 4-point Hermite
interpolation (`engine::hermite`), the increment folding in the sample's own
rate, so nothing is resampled on load. Everything but the file is a parameter
(automatable); start, end and loop points are fractions of the sample. The file
is the insert's `state` (base64 of the path) and so lives in the model: loading
one is a `SetInsertState` command (undoable), and `InsertCache` now passes a
changed model state to a live built-in that does not own its parameters
(hosted plugins still take theirs only on creation). The processor is JUCE-free:
`setState` asks the `sampler::Loader` the factory was given (the app's) for an
immutable `sampler::Sample` on the controller thread and publishes it through
an `rt::SnapshotPublisher`, so the real-time thread swaps it in at a block start
and a retired sample is freed on the controller side once the audio thread has
moved past it. A missing file keeps its path (a re-save loses nothing) and the
sampler stays silent with the load error for the editor.
The app's loader is `SampleFiles` (in the session, given to the insert factory, so
bounces load the same way): the whole file at its own rate, up to ten minutes,
shared while any sampler holds it (a recompile, undo or bounce does not decode it
again) and read afresh when the file changed on disk. To make one from audio
already in the project, right-click an audio clip and choose **Open in Sampler**:
`ProjectSession::addSamplerTrack` adds an instrument channel named after the file
and a MIDI track playing it in one undo step, the start and end points set to
the part of the file the clip uses, and the editor opens. The editor
(`ui/strip/SamplerEditor`, 07-plugins.md 7.4f) draws the waveform from the live
instance's sample (min and max per pixel column, rebuilt when the sample or the
width changes), drags the points as parameters, shows the sounding voices from
`sampler::soundingVoices` (positions the audio thread publishes as atomics per
block) and loads a dropped or chosen file with `SetInsertState` plus the points
reset, grouped as one gesture. Launch flag: `--sampler <file> [demo]` (the same
with a four-note clip, editor open; `demo` sets a loop, a key range and the
filter).

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
one opens `ui::PianoRoll` in the Piano Roll panel (F6). The roll follows
09-piano-roll.md: a 32 px toolbar (Draw / Select / Erase, the snap grid menu,
the quantise menu with its strength, a link-zoom toggle that follows the
arrange view's time view, an audition toggle, the scale slot), a 64 px keyboard
(white keys `surface2`, black `surface0`, C labels; keys light in `accent`
while their notes sound at the playhead or are held, and pressing one previews
the note), with drawing, clicking or transposing a note auditioning it too
(while the toggle is on). Auditions go through `AudioEngine::pushMidiPreview`:
queued with live MIDI but tagged with the clip's track, they reach only that
track's instrument whether or not it is armed or monitoring, and are never
recorded, `laneAlt` rows on the black keys with octave
lines, the arrange grid colours, notes in the track colour at the waveform
alpha with a `canvas` inset and a velocity alpha from `midiNoteVelocityLow` to
full, selection as a `textPrimary` outline over the selection tint, ghost notes
from the track's other clips at 30%, and an 80 px velocity lane with 6 px caps
that ramp when dragged across. Draw adds a note of the last-used length and
drags its length (Alt skips snap); Select lassos, moves with a pitch tooltip,
resizes, Ctrl+drags to duplicate, Shift constrains the axis, double-click adds;
Erase clicks or drags across notes; Q quantises at the menu strength, Shift+Q
the ends too; arrows nudge (Shift fine, Ctrl an octave), + and - transpose.
Not in this pass: the scale highlight (no key model), notes drawn live while
recording, the instrument chip in the track header. Launch flags:
`--midi-track`, `--midi-track-with <type>`, `--piano-roll` (the first MIDI
clip in the panel).

### MIDI output

A MIDI track with `midiOut` set plays the MIDI output device (the one chosen in
Audio Setup) on its `midiOutChannel`. After the tracks of a span have rendered,
`Renderer::renderSpan` (on the callback thread, so the queue has one producer)
pushes each such track's span events, with the channel set, into the engine's
`midiOut_` SPSC queue, timed on the engine's sample clock (samples rendered
since the engine started; loops and locates do not disturb it). At the start of
each block the engine publishes the clock with the `steady_clock` time,
guarded by a sequence count so a reader never sees half a pair.
`audio::MidiDeviceLayer` runs a sender thread that drains the queue every
millisecond into a clock-ordered list and sends each event once its clock time,
mapped to wall time and delayed by the audio output latency, has come, so the
external instrument sounds with the audio. Note-offs on stop, locate and loop
come through the same events. A full queue drops events rather than block.

### Crash reports

`aural::crash` (src/app/CrashReports, no JUCE) installs an unhandled-exception
filter at startup. A crash writes a minidump (thread info, indirectly referenced
memory, unloaded modules) and a note with the version and exception code into
`<settings folder>/Crashes`, from buffers prepared at install (the heap may be
corrupt by then), and leaves the exception to Windows Error Reporting too. The
next launch says so with a toast (Show reveals the dump; either button moves the
reports to `Crashes/Seen`). Nothing is sent anywhere: the Diagnostics
preference to send reports waits for the release work.

### Metronome, count-in, loop and punch

`engine::ClickGenerator` adds a 30 ms decaying sine burst on every beat (1.5 kHz on
bar starts, 1 kHz otherwise) at multiples of the beat length from zero, so the same
code serves the count-in before the start position (negative timeline positions)
and playback. The snapshot carries tempo and time signature.

Every insert is handed an `engine::PlayHead` before each block
(`InsertProcessor::setPlayHead`): the tempo, the time signature, whether the
transport plays, and the song position at the block's first sample in samples
and quarter notes (one tempo, so position / samples-per-quarter). Built-ins read
it through `ProcessorBase::playHead()` (moved on by the chunk offset when a block
is split); the Delay and Starbit's arpeggio use it for note values
(`plugins/BeatSync.h`), and a hosted plugin gets it as JUCE's `AudioPlayHead`
(VST3) or a `clap_event_transport` (CLAP). The editors read the tempo back from
the feedback block (`ProcessorFeedback::tempoBpm`).

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

Preferences (08-setup.md 8.2, `ui::PreferencesView`, Ctrl+, or the app menu,
`--preferences [page]`): a 160 px page list and pages of label-and-control rows
with uppercase section headers. General (project folder, recent-file count,
theme, accent swatches and hex, UI scale), Audio (the Audio Setup view inline),
Recording (file format, count-in, pre-roll, metronome level, take name
pattern), Editing (snap and grid defaults, default fade, ripple, follow
playhead), Plugins (extra scan folders, scan on launch, blocklist, scan now,
windows on top), Shortcuts (a searchable table of the keyboard map) and
Diagnostics (crash-report opt-in, the settings folder, copy system report).
Values are `general.*`, `recording.*`, `editing.*`, `plugins.*` and
`diagnostics.*` keys; the main window applies the ones with an effect (UI
scale, metronome level, scan folders, follow playhead) when a value changes and
the project defaults (count-in, snap, grid, ripple, scan on launch) at launch.
The accent is stored and applied at the next launch; file format, pre-roll,
default fade, the take name pattern and crash reporting are stored and say so
in their hints until the features that use them land; shortcut rebinding is
not in this pass.

Audio Setup (8.1): the sections are cards (`surface1`, uppercase caption
titles), labels in body `textSecondary`, values in the mono value face, the
status line in mono with a success/warning/danger dot; the preferred device
shows greyed as "(unavailable)" when it is not present; calibration reports
"Playing click… n/m" inline; pass-through carries the hint "Hear the selected
inputs directly. Use headphones." and switching it on while an input is live
shows a one-time feedback toast. The window is 720×620 and scrolls the
content. Not in this pass: a meter per input channel (the callback reports one
input peak) and stereo-linked channel pairs (tracks take one input channel).

### UI

`ui::Theme` loads the design tokens (docs/design/tokens.json, section 2 of the
design spec): colours, the track palette, fonts, spacing, radii, sizes, motion,
meter zones, elevation and the tactile `material` layer. The build embeds the
file as the defaults and ships a copy under `assets/` beside the executable
(read at start, so a newer token file needs no rebuild); a `tokens.json` beside
the user's settings file overrides any keys it holds. Bundled typefaces (Inter,
JetBrains Mono under `assets/fonts`) are loaded as system typefaces when
present and fall back to Segoe UI and Consolas when not; `Theme::font()` and
the named helpers (`body()`, `label()`, `value()`, `timeLarge()`...) give views
the scale. The pre-token field names (`colours.background`, `sizes.controlHeight`)
are kept as aliases until every screen is restyled. `ui::AuralLookAndFeel` draws
every stock control to the component specs (buttons by variant through component
properties: secondary, primary, ghost, danger; toggles with the semantic M/S/R/
monitor colours and a "hard on" arm; combos, fields, checkboxes, sliders with
bipolar fills, scrollbars, tabs, menus with icon columns and shortcut hints,
tooltips, progress bars, table headers) and the tactile layer (control-face
gradients with a highlight and shadow, a reversed gradient and inset when pressed,
glows for active toggles rendered once per size and colour into cached images).
Focus rings are drawn only for keyboard navigation (the window reports Tab and
arrow presses; any click turns them off). Dedicated components under
`ui/components` cover what stock controls cannot: `Fader` (0 dB at 75% of the
travel, +6 dB at the top, a grooved track and a lit cap), `Knob` (ring with a
radial body), `Meter` (zone segments as LEDs in a well, peak hold, clip squares),
`SegmentedControl` (sliding thumb), `NumericField` (drag, type, wheel),
`PanelHeader`, `ToastStack` (top-left notifications under the header, persistent
for warnings and errors, up to two actions) and `Sheet` (a dialog without a
modal loop, with an optional content component). A CTest
(`scripts/check-ui-tokens.ps1`) fails when `src/ui` carries a literal colour;
`JUCE_MODAL_LOOPS_PERMITTED` stays 0.

**The shell** (03-shell.md, 06-transport.md). `MainWindow`'s content is five
zones: the 48 px `HeaderBar`, the arrange view, the dock below it, the side
panel to its right and the 24 px `StatusBar`; toasts stack under the header at
the left and sheets cover the content. Every secondary view is a panel
(`ui::PanelId`: mixer, editor, piano roll, diagnostics, browser, inspector) that
exactly one host owns at a time: the dock, the side panel (both `PanelHost`, a
tab row with detach and close buttons) or a `FloatingPanelWindow` (a native
window with a header that attaches back, pins always-on-top and closes). The
`PanelManager` owns the model: register, show/hide/toggle, move between hosts
(buttons, Shift+F-key, dragging a tab out of a host or a floating header over
one, with a drop highlight), animated dock height and side width (reduce-motion
honoured), `layout.json` beside the settings (hosts, order, sizes, floating
bounds, visibility) and the Mix and Edit presets. Closing a floating panel hides
it; a panel is never lost. The header carries the app and project menus, undo
and redo with their names in tooltips, the transport cluster (go to start,
play/stop, stop, record; loop, punch, click with a count-in menu), the bars and
time readouts (click to type, drag or wheel a segment to nudge, both following
the heard position), tempo and signature fields, the master clip indicator, snap
with its grid menu, ripple, zoom to fit and the panel toggles. The status bar
shows the device (click: Audio Setup), rate and buffer, the round trip (measured
or driver-reported), callback load with the warning thresholds, dropouts since
the last click, disk space while recording, a persistent warning and transient
messages (saved, undid...). The first launch (no settings file) shows the
first-run sheet: driver, device and buffer combos with a live input meter, then
"Scan for plugins now?"; `--first-run` shows it again. Unfinished recordings
are offered back as a toast (Recover / Set aside) and the window title carries
"● " while recording. The keyboard map of 10-interaction.md 10.4 lives in the
content's `keyPressed` ahead of the arrange view's edit keys; where the spec
gives Shift+M and Shift+S two jobs, the track state (mute, solo) keeps them and
the edits (marker region, split at selection) take Alt. Launch flags for
unattended captures: `--mixer`, `--plugins` (the browser floating),
`--layout <Mix|Edit>`, `--panel <key>`, `--first-run`, `--preferences [page]`,
besides `--open`, `--audio-setup` and the Phase 8 flags.

**Interaction** (10-interaction.md). Moves and trims in the arrange view snap
to the nearest target within 6 px (clip edges on the row, markers, the loop
and punch edges, the playhead) before the grid; the drag tooltip names the
target, Alt skips snap for that drag, and a clip that did not start on the
grid keeps its offset (relative snap). The middle button pans the arrange
view and the piano roll. Ctrl+C/X/V copy, cut and paste clips (at the
playhead on the selected track, keeping spacing and track offsets);
Alt+Left/Right nudge (Shift fine); 1 and 2 pick the select and range tools
(a range drag in a lane selects a time range across the selected tracks); F2
renames the selected track while the arrange view has the focus and is the
mixer's panel key otherwise; Ctrl+F shows the browser and focuses its search;
T taps the tempo while the tempo field is focused (`NumericField::onKey`).
The status bar's right side shows the tooltip of the control under the
pointer after a second. Errors follow 10.6: a disconnected device stops
playback and raises an error toast (what happened, what it means, what to
do) with Audio Setup as the action; ten dropouts raise a warning toast with
the fix at most once a minute; closing with unsaved changes and removing a
bus that channels route to ask in sheets whose destructive action is styled
danger. `tests/unit_juce/KeyboardMapTests.cpp` exercises the map through the
views' key handlers (the main window's own bindings are thin forwards). Not
in this pass: triplet and dotted grids, custom fade and split cursors, the
300 ms wheel guard on valued controls, Space+drag panning, rebinding.

**Accessibility and performance** (11-accessibility-performance.md).
`scripts/contrast.py` regenerates docs/design/contrast.md from the token file
(the WCAG ratios for every text/surface pair the design uses and the twelve
hues) and exits 1 with `--check` when a required pair fails; its output
matches the checked-in table. Preferences offers a High contrast variant
(lines at 24%, text one step brighter) and the accent override; both are
written to the user's `tokens.json` beside the settings and read at the next
launch. UI Automation: `Fader`, `Knob`, `NumericField` and `SegmentedControl`
expose themselves as sliders with their value text (dB, pan, the segment
label), `Meter` as a read-only level, icon buttons carry their tooltip as the
name, track headers are titled "Track n: name" with the kind as description,
and the status bar's transient messages and toasts post announcements (toasts
for warnings and errors at high priority). Reduce-motion comes from the OS
setting (Theme). Rendering: the playhead repaints only the columns it leaves
and takes, meters skip the repaint while silent, the status bar rebuilds only
when a shown value changed, and `ui::FrameStats` records the arrange and
mixer paint times; the Diagnostics panel shows the paint percentiles next to
the frame interval, and `--benchmark-ui <seconds>` plays the open project
with the mixer shown, scrolls the arrange view a screen a second and prints
the frame-interval, paint and audio-load percentiles to stdout before
quitting. The acceptance screenshots live under `docs/screenshots/phase7/`
(dated). Measured on 2026-10-20 on the development PC (Ryzen 9 3900X, RTX
4070 SUPER, the window at 1280×800, 100%, Volt ASIO at 64 samples) with the
RelWithDebInfo build, nine tracks (eight audio with a 3-minute mixdown each
and one MIDI), playing with the mixer open and the arrange view scrolling a
screen a second: paint p50 0.69 ms, p99 3.69 ms, max 4.92 ms over 655 paints;
the frame interval sits at the 30 Hz timer (p50 34.0 ms, p99 35.1 ms, no
missed tick); audio load p50 0.8%, max 2.0%. The Debug build of the same run
paints at p50 4.6 ms, p99 14.0 ms, max 16.9 ms. The 64-track 4K at 150% and
mixed-DPI scenarios of 11.6 were not run: no such monitor is attached here.
Not in this pass: handlers for clips and the ruler, a Narrator walkthrough
(no screen reader was run here), DPI-change verification beyond 100%, a CI
machine for the benchmark.

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
| 2026-10-19 | Theme tokens are a C++ struct for now, not a loaded JSON file (superseded: Phase 7 made the struct a loader for docs/design/tokens.json) | The Figma token export does not exist yet. The struct is the contract; Phase 7 adds the loader without touching the views. |
| 2026-10-19 | The token file is embedded at build time and also shipped beside the app | The app and the tests must never depend on a file being found; embedding gives that, and the shipped copy lets a designer try a token change without a rebuild. The user's override holds only the keys it changes, so a new default reaches them. |
| 2026-10-19 | Glows are rendered once per size and colour into images | A blurred glow per frame is the one expensive thing in the tactile layer; drawing a cached image keeps meters and toggles cheap, and reduce-motion drops the glows entirely. |
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
| 2026-10-19 | A CLAP restart is a fresh instance through the next snapshot, never a deactivation in place | The real-time thread may be inside process() at any moment while the instance is in a snapshot; recreating it with its state carried over reuses the path every other edit takes and needs no RT-side handshake. |
| 2026-10-19 | Hosted plugins own their parameters; the model's list reaches them only when a command touches it | Pushing the whole list on every compile or fader move reverted presets loaded from the plugin's own menu; mirroring the instance into the model before a touching command keeps undo exact without making the model the authority. |
| 2026-10-19 | CLAP parameter flushes while the device is stopped go through the session's poll, not a message | The flush is an audio-thread call while the plugin is active; the poll knows whether the engine is running and leaves the queue to process() when it is, which is what keeps one consumer on the queue. |
| 2026-10-19 | MIDI note-offs on stop, locate and loop wrap come from the track's own continuity check, not from transport commands | The renderer sees every span's start position; comparing it with where the previous span ended catches every discontinuity (a locate, a wrap, play from elsewhere, a stop) in one place, so no transport path can forget to end a note. |
| 2026-10-19 | MIDI events are a fixed list per track per span, sized at compile | An instrument reads the span's events with the audio in one call; building the list in the track's render job keeps it off the shared path and allocation-free, and the instrument sees the events in time order. |
| 2026-10-19 | Recorded MIDI is placed by the output-side latency only | A played note arrives when it is played, but the player plays to what they hear, which lags by the output path; subtracting the input side too (as for audio) would place MIDI early. |
| 2026-10-19 | MIDI devices are identified by name | The name is what the user recognises and what a project can carry between machines; JUCE's identifier is stable but opaque. Two devices of one name are both opened and both heard. |
| 2026-10-19 | A built-in synth ships with the engine | A MIDI track must make a sound without a third-party instrument, both for the user and for offline tests of the whole path (schedule, instrument, bounce). |
| 2026-10-19 | Trimming a MIDI clip keeps its notes where they are on the timeline | Matches audio clips (the source stays put under the clip edges) and lets a trim be undone by trimming back without losing notes. |
| 2026-10-20 | The channel strip is stored as inserts in a second list, not a new model type | Modules need exactly what an insert has (type, parameters, bypass, a cached processor instance); a second list keeps them out of the insert rack while every insert command, the undo history, the instance cache and the live-value path work on them unchanged. |
| 2026-10-20 | Strip modules that are off are compiled as bypassed inserts, not left out | Switching a module is then a live value (no recompile, no click: the bypass crossfade covers it), and a settled bypass costs one branch per block. |
| 2026-10-20 | Feedback for editors is a ring of atomics per processor, polled by the UI | It follows the RT-to-controller rule (preallocated, never blocks) without a queue per display; a display tolerates the rare torn frame, and the UI can read as much or as little history as it draws. |
| 2026-10-20 | The strip limiter has no lookahead | A strip is on every channel; a module that is off must not add latency, and switching it on must not re-solve delay compensation. The insert limiter keeps its lookahead for mastering. |
| 2026-10-20 | Paint times are recorded by the two heavy views, not by hooking every component | A scope at the top of `ArrangeView::paint` and `MixerView::paint` costs nothing elsewhere and covers what the budget is about; the frame interval (the timer's cadence) is tracked separately in the main window. |
| 2026-10-20 | The mixer's section boxes are real slots and rows, not menus | Signal flow must be visible: an insert's bypass and order, a send's level and tap are on the strip, and the menus keep only what has no control of its own. |
| 2026-10-20 | The arrange headers read meter copies from the mixer rather than taking from the meter block | `MeterBlock::take` resets the peak, so one consumer must own each slot; the mixer already takes every slot each frame and keeps the last readings for anyone else. |
| 2026-10-20 | One compressor for inserts and the channel strip: the strip's, keyed | The two had drifted (the insert lacked RMS, auto makeup and mix; the strip lacked a key); keeping both meant fixing everything twice. The strip's behaviour won, so insert compressors in older projects sound somewhat different. |
| 2026-10-20 | Selection has one owner that notifies one listener | Four views each cached the selection and nine call sites kept them in step by hand; missing one left the editor on another channel, twice. |
| 2026-10-20 | Parameters are saved with stable keys and reconciled on load | Saving by position silently moved every later value (and automation) whenever a processor gained a parameter, as Pulsar did; keys let processors change, and an upgrade chain replaces refusing older files. |
| 2026-10-20 | Missing media is detected once per project change, not per paint | A file-exists check per clip per frame would stall painting on a slow volume; the set is rebuilt when the project changes. |
| 2026-10-20 | Sidechain keys can tap after the source's fader, and are delay compensated | Ducking that should follow a source's level rides (a kick pulled down in the chorus) needs the fader in the key; pre-fader stays the default so existing projects sound the same. An early key is delayed for free, a late one would cost the keyed channel latency, so it is left as is. Bus sources for track inserts are still not offered. |
| 2026-10-19 | Panels are owned by one of three hosts and never destroyed on close | A panel's component keeps its state (the mixer's open editors, the browser's filter) across moves and hides; closing a floating panel hides it and returns it to its default host, so a user cannot lose one. |
| 2026-10-19 | Shift+M / Shift+S stay the track state; the marker region and split-at-selection take Alt | The spec assigns both pairs the same keys; mute and solo are the more frequent and match every other DAW, so the edits move. |
| 2026-10-19 | The first-run sheet is detected by the settings file's absence, before settings are created | No "first run" flag can be stale, and the sheet never shows again once any setting is saved. |
| 2026-10-19 | The limiter has no lookahead (superseded the same day: lookahead added once delay compensation landed) | Lookahead adds latency that needs delay compensation across the graph, which Phase 5 brings with plugin hosting; until then a fast-release limiter with a hard ceiling is honest about what it does. |
