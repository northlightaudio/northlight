# Decisions

Why Northlight is built the way it is. Each entry records a choice, the date it was
made and the reason, so a later change can tell what it is trading away. The log is
grouped by area; an entry that replaced an earlier choice says so.

For how the pieces fit together see [architecture.md](architecture.md).

## Foundations

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Windows first; a platform seam kept for a later port | ASIO and VST3 hosting are the hard requirements and both are Windows-first. The device layer, plugin window embedding and file dialogs sit behind interfaces so macOS and Linux remain possible. |
| 2026-10-19 | VST3 and CLAP; no VST2, AAX or AU | Steinberg no longer licenses VST2 to new hosts. CLAP is open (MIT) and cheap to add beside VST3. |
| 2026-10-19 | MIDI and instruments in the first version | With the bundled instruments the application is useful on its own. |
| 2026-10-21 | Licensed under the GNU AGPLv3 | It matches JUCE's open licence and takes the ASIO SDK under its GPLv3 option (GPLv3 section 13 allows the combination); every other dependency is permissive. See LICENSE and THIRD_PARTY_NOTICES.md. |
| 2026-10-21 | Renamed from Aural to Northlight | A name of its own for the public release. Settings and folders from the old name migrate on first start. |
| 2026-10-21 | Signalsmith Stretch for time-stretch | MIT licensed and very good; Rubber Band would have cost a licence. |

## Log

### Platform and build

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | C++20 and JUCE 8, not Rust | VST3 hosting and plugin-window embedding are mature in JUCE and immature in Rust. |
| 2026-10-19 | Own engine rather than Tracktion Engine | Licensing freedom and control of the threading and data model. |
| 2026-10-19 | Dependencies via `FetchContent`, no vcpkg yet | Nothing yet needs a package with a real build step. nlohmann::json is header-only and comes the same way. |
| 2026-10-19 | JUCE modules compiled once into `northlight_juce` and re-exported | JUCE modules add their sources to every consumer, so several consumers would give duplicate symbols. One target also gives every consumer the same module settings. |
| 2026-10-19 | The real-time allocation check overrides `operator new` in an OBJECT library | A static library's override can be discarded by the linker; OBJECT library objects are always linked. |

### Engine and real time

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Timeline positions are samples at the project rate, not beats | Sample-accurate placement and recording with no rounding. Bars, beats and tempo are a view of the timeline through the tempo map. |
| 2026-10-19 | Snapshots are retired by epoch, not through a real-time garbage queue | The real-time thread advertises the epoch it uses and the controller frees older snapshots, so the real-time thread never touches a queue or free list. |
| 2026-10-19 | Per-track buffers live in the snapshot, not a global pool | The snapshot already knows every track and is built off the real-time thread, so no pool manager or real-time bookkeeping is needed. Render calls are capped at 8192 samples and larger device blocks are split. |
| 2026-10-19 | Workers woken by a counting semaphore; the callback spins for completion | A semaphore release is the cheapest non-blocking wake on Windows, and the spin is bounded by one track's render. |
| 2026-10-19 | Worker pool batch state packed into one atomic | Separate index and count fields let a late worker run a job from one batch against the next batch's context. One 64-bit `fetch_add` keeps them inseparable. |
| 2026-10-19 | Tracks render in parallel; buses sum on the callback thread | Buses are few and serial by dependency anyway, so parallel tracks capture the benefit without a dependency-aware scheduler. |
| 2026-10-19 | Callback load is a histogram of deadline fractions | Percentiles need no sorting or allocation, and a fraction of the deadline means the same at any buffer size. |
| 2026-10-19 | Live values travel as targets the renderer ramps to; structural edits recompile | A fader move must glide, not step, and must not rebuild the graph on every mouse move. Per-channel state that outlives snapshots carries this, and automation too. |
| 2026-10-19 | Insert parameters are atomics on a persistent instance, not part of the snapshot | Recreating a processor on every edit would reset its state and click. The snapshot stays immutable. |
| 2026-10-19 | Meters are peak-hold plus last-block RMS in atomics | The UI cannot miss a peak between frames and applies its own ballistics, with no queue. |
| 2026-10-20 | Editor feedback is a ring of atomics per processor, polled by the UI | Preallocated and non-blocking, with no queue per display; a display tolerates the rare torn frame. |
| 2026-10-19 | Own Hermite resampler instead of `juce::LagrangeInterpolator` | JUCE's interpolator carries a lookback delay and sequential state, which misaligned media and complicated seeks. A stateless resampler at exact positions is sample-aligned. |
| 2026-10-22 | Render workers join MMCSS "Pro Audio"; the disk thread runs at `THREAD_PRIORITY_HIGHEST` | Windows boosts busy normal threads as high as time-critical, so a compile could tie with a worker and make the whole block late, whatever the buffer size. MMCSS puts the workers above that range. The disk thread only needs to beat normal work, and staying below the workers keeps file reads from taking a core from rendering. The driver's callback thread is left as the driver set it. |
| 2026-10-22 | Disk dropouts are starvations, not every underrun | Every locate plays a little silence while the window rebuilds, so counting all underruns would report a dropout on each seek. An underrun only counts when the window had caught up, and only once per gap. |
| 2026-10-19 | Loops shorter than 64 samples are refused | Each loop pass in a block costs a render span and a worker wake; very short loops have no musical use. |

### Audio devices and media

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Device changes go through `AudioDeviceManager::initialise` with a `DEVICESETUP` element | The other route opens the type's default device first and then the chosen one. The XML path applies everything in one open. |
| 2026-10-19 | Device rate vs project rate: empty projects follow the device, others warn | Silently resampling a whole project on a device change would hide a real problem. |
| 2026-10-19 | Files under 30 s load into memory; longer ones stream | Short clips are the common case and avoid disk latency; streaming keeps project size unbounded by RAM. |
| 2026-10-19 | One streaming window per clip, not per file | Two clips of one file would make a shared window seek back and forth and play silence. Windows are cached by clip id so edits do not reset them. |
| 2026-10-19 | The streaming window is guarded by position, with a grace period on seeks | One reader and one writer do not need a full lock-free ring; a guard distance and a short quiet period after a seek give the same safety. |
| 2026-10-19 | Peaks are mipmapped, built at the project rate and cached beside the media | Every zoom level reads a few bins per pixel, peaks share the clips' units, and a project opened from another folder still finds them. |

### Latency calibration and recording

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Calibration uses a short windowed chirp found by cross-correlation | Impulses are smeared by converter filters and lost in noise; a chirp's correlation peak is one sample wide. Matching on magnitude tolerates polarity-inverting cables. |
| 2026-10-19 | Measured latency is stored with the device, rate and buffer size | Round trip changes with all three, and a stale figure silently misplaces recordings. |
| 2026-10-19 | Recordings are placed by calibration when it matches, else by reported latency | The measurement is the truth when it exists; the driver's figures keep placement roughly right otherwise. |
| 2026-10-19 | Recordings are 32-bit float mono WAV, one file per input | Float matches the engine and never clips; one file per input keeps placement and recovery simple. |
| 2026-10-22 | Stereo inputs are named buses of two device channels, as in Cubase, defined per device on this machine; tracks store the name | The hardware belongs to the machine, so a bus set up once serves every project, and a project moved to another machine shows its missing bus by name instead of silently recording the wrong channels. Single channels stay selectable, so mono recording needs no setup. A stereo input records one two-channel file, keeping one file per track input. |
| 2026-10-19 | The record session lives outside the snapshot | Editing during a take swaps snapshots, which must not disturb the rings or restart capture. |
| 2026-10-19 | Count-in and punch are handled by segmenting the block on the real-time thread | Acting on block boundaries would be up to one buffer late; segmenting is sample-accurate at any buffer size. |
| 2026-10-19 | One region serves both loop and punch | Two overlapping regions are hard to read and rarely differ; the toggles decide what the region means. |
| 2026-10-19 | Takes are split by the writer at marks from the audio thread, one file per pass | The audio thread must not open files, so it only pushes a sample count. One file per take keeps every take a plain clip. |
| 2026-10-19 | Dropped record blocks become silence, not a shorter file | A shorter file would shift every later sample and take boundary. |
| 2026-10-19 | Crash safety by periodic header rewrite plus repair from file size | The standard approach: a valid file at every second boundary and the tail recovered from the file size. |
| 2026-10-19 | A JSON sidecar per input, written before capture | After a crash only the sidecar knows the track, start, loop and latency, so recovery can place the take. |
| 2026-10-19 | Crash recovery asks before importing | Silently adding tracks to whatever project opens next would surprise. Declined takes are set aside, not deleted. |

### Mixing and routing

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Tracks and buses share one Channel type and one set of fader commands | Their fader sections, routing and sends are identical, so undo stays uniform and the mixer draws both with one strip. |
| 2026-10-19 | Solo-in-place propagates both up and down the routing | Soloing a bus plays what feeds it, and soloing a track keeps its bus and the master alive. Resolving this at compile keeps the real-time path a flag check. |
| 2026-10-19 | Routing cycles are refused at the command layer | Silently rewiring a cycle would show one graph and play another. |
| 2026-10-19 | Fader groups are commands over channel values, not engine state | The snapshot is unchanged and undo is exact. Relative moves keep the members' balance. |
| 2026-10-19 | Delay compensation per graph edge, solved at compile, lines cached across compiles | Edges handle a source feeding destinations with different needs. Solving at compile keeps the real-time path a fixed delay, and caching lines makes recompiles inaudible. |
| 2026-10-19 | Insert chains are stereo pairs | Channels are stereo, so every plugin gets a stereo main bus, which keeps hosting simple and correct for effects. |
| 2026-10-19 | Sidechain keys are pre-fader by default, can tap post-fader, and are delay compensated; keyed tracks render in dependency levels (post-fader option 2026-10-20) | A keyed track must see its source finished in the same block; levels keep everything unkeyed parallel. Post-fader keys follow level rides. A late key is left as is rather than adding latency, and bus sources for track inserts are not offered. |
| 2026-10-20 | The channel strip is stored as inserts in a second list, not a new model type | Modules need exactly what an insert has, so every insert command, undo, the instance cache and live values work on them unchanged. |
| 2026-10-20 | Strip modules that are off are compiled as bypassed inserts | Switching one is a live value with no recompile and no click. |
| 2026-10-20 | One compressor for inserts and the strip: the strip's, keyed | Two had drifted apart and needed every fix twice. Insert compressors in older projects sound somewhat different. |
| 2026-10-20 | The strip limiter has no lookahead; the insert limiter does | A strip is on every channel and must not add latency or re-solve delay compensation when switched. The insert limiter keeps lookahead for mastering (added once delay compensation landed). |
| 2026-10-19 | Fades are linear | Linear fades sum to constant level for correlated material, the common case of a take split and rejoined. |

### Plugins

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Built-in processors live in `src/plugins` behind the engine's insert interface | The engine must not depend on any processor; hosted plugins implement the same interface and the app injects the factory. |
| 2026-10-19 | CLAP hosted against the C ABI, VST3 through JUCE, both behind one insert interface | JUCE does not host CLAP. Thread-check answers must be truthful because plugins assert on them. |
| 2026-10-19 | Plugin scanning runs in a separate executable, one file per process | A plugin that crashes or hangs during a scan must not take the DAW with it; the file is blocklisted with the reason. |
| 2026-10-19 | Hosted plugins own their state and parameters | State is an opaque blob saved and restored, as in every host. The model's parameter list reaches the plugin only when a command touches it; pushing it on every compile reverted presets loaded from the plugin's own menu. |
| 2026-10-22 | Pulsar models the TR-808's circuits rather than generic drum recipes | A big pitch sweep, a DC click and filters set as fractions of the sample rate made a laser-like kick and pads that changed with the rate. The 808's own structure (a resonator with a gentle drop, two snare modes, retriggered clap bursts, six squares through band and high passes) is what people expect a drum synth to sound like. Filters in hertz, noise scaled to the rate and an 18 kHz top keep a pad the same at any sample rate. |
| 2026-10-19 | A CLAP restart creates a fresh instance through the next snapshot, never a deactivation in place | The real-time thread may be inside `process()` at any moment; recreating with state carried over needs no real-time handshake. |

### MIDI

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Note-offs come from each track's own continuity check, not transport commands | Comparing each span's start with where the last ended catches every locate, wrap and stop in one place. |
| 2026-10-19 | MIDI events are a fixed list per track per span, sized at compile | Built in the track's render job, allocation-free and in time order. |
| 2026-10-19 | Recorded MIDI is placed by the output-side latency only | The player plays to what they hear; subtracting input latency as for audio would place notes early. |
| 2026-10-19 | MIDI devices are identified by name | The name is what the user recognises and what a project carries between machines. Devices sharing a name are all opened. |
| 2026-10-19 | A built-in synth ships with the engine | A MIDI track must make a sound without third-party instruments, for users and for offline tests of the whole path. |
| 2026-10-19 | Trimming a MIDI clip keeps its notes in place on the timeline | Matches audio clips and makes a trim undoable by trimming back. |

### Files and settings

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | Audio settings are a JUCE-free value type stored as flat key/value pairs | Testable without JUCE, with a simple on-disk format. |
| 2026-10-20 | Parameters are saved with stable keys and reconciled on load | Saving by position shifts later values whenever a processor gains a parameter. An upgrade chain replaces refusing older files. |
| 2026-10-20 | Missing media is detected once per project change, not per paint | A file check per clip per frame would stall painting on a slow volume. |
| 2026-10-19 | First run is detected by the settings file's absence | No flag can go stale, and the first-run sheet never shows again once a setting is saved. |
| 2026-10-22 | A project opens in steps: reading and decoding on a thread, plugins one per message-loop step | Plugin creation must stay on the message thread (VST3 and CLAP require it), and a plugin can take most of a second, so the window paints the next plugin's name before making it. Swapping the project in only when the plugins start keeps the current one untouched, and cancellable, through the slow file work. |

### Interface

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | `JUCE_MODAL_LOOPS_PERMITTED=0` | Async dialogs only, so the message thread never re-enters during a modal loop, a classic source of host and plugin deadlocks. |
| 2026-10-19 | Theme tokens load from `docs/design/tokens.json`, embedded at build time and also shipped beside the app (replaced a hard-coded C++ struct) | The app and tests never depend on finding a file, and a designer can try a change without a rebuild. A user override holds only changed keys, so new defaults still reach them. |
| 2026-10-19 | Glows are rendered once per size and colour into images | A per-frame blur is the one expensive thing in the tactile layer. Reduce-motion drops glows entirely. |
| 2026-10-19 | Edits are previewed in the view and committed as command groups | A drag issues no commands until release, which becomes one undo step. The model never sees a half-finished edit. |
| 2026-10-20 | Selection has one owner that notifies one listener | Views caching their own selection drifted out of step. |
| 2026-10-20 | The mixer's sections are real slots and rows, not menus | Signal flow must be visible on the strip; menus keep only what has no control of its own. |
| 2026-10-19 | Panels are owned by one of three hosts and never destroyed on close | Panels keep their state across moves; closing a floating panel returns it to its default host, so none can be lost. |
| 2026-10-22 | Late callbacks are timed against a due time that snaps to early arrivals and follows late ones slowly | Callbacks can be delayed but never early, so the earliest arrivals mark the schedule; following late ones slowly absorbs clock drift without hiding a stall. Splits and catch-ups (a block right after the previous one) are not judged. |
| 2026-10-22 | The output tap finds clicks with a linear-prediction error test, not a slope or level threshold | Slope thresholds fire on every drum hit; a sample the last 16 do not predict is a discontinuity in any material. |
| 2026-10-22 | Driver dropouts show "not reported" when the driver cannot count them | Most ASIO drivers cannot; a 0 there told users nothing was wrong when nothing was being measured. |
| 2026-10-22 | All UI meets a design bar: every element and state designed, nothing at a toolkit default | Automated checks catch literal colours and contrast, not a screen that looks unconsidered. Reviewing captures against a written list holds visual quality to the same standard as correctness. |
| 2026-10-22 | The transport readout spaces its text, not fixed boxes, and draws every field on one baseline | Fixed boxes with centred text left the gaps to chance (51, 40 and 24 px) and four alignments; boxes that hug the text keep the gaps equal at any value. The well grows by a figure at bar 10 and 100, which happens rarely and moves the flanks a few pixels. |
