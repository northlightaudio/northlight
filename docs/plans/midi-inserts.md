# Plan: MIDI inserts

Processors that take MIDI and give back MIDI: an arpeggiator, chord tools, a scale
filter, note echo and the rest. They sit on a MIDI track, ahead of the instrument it
plays, and change the notes before any sound is made.

**Status (2026-10-29), on the `midi-fx` branch (not committed):** Phases 1 to 3 are
built except for the automation lanes' UI (a lane on a MIDI effect's parameter plays
in the engine and is tested, but the arrange view offers no lanes on a MIDI track yet). The engine path, the model (schema 19) and all twelve
core effects are in `src/plugins/midi` (MidiEffectBase, MidiTools, Transpose,
Utilities, TimeEffects, Chords), with tests for each and a stress test that plays
every effect hard with its settings moving (no allocation, nothing left sounding).
The Editor panel has the MIDI FX section (screens/plugins.md 7.4h), with a
settings-rows face for every effect, the "+ MIDI effect" menu and the slot menu.
`--midi-fx <type>` adds one for captures. Changes from the plan: **MIDI Filter**
replaces Channel & Filter (the track stamps its channel after the chain), Note
Length has no scale mode, the arrange chip names the MIDI FX in its tooltip only,
and the Arpeggiator has no accent row, step pattern or retrigger option yet.
**Not built yet:** the arrange view's automation lanes on a MIDI track (no Volume
or Pan there, only its MIDI FX parameters; needs an arrange.md spec), built faces for the Arpeggiator and Chord Pads, presets, the plugin browser
category, dragging MIDI FX cards, the overflow count in the track's trouble line,
Render MIDI FX to clip, the later list, and hosted MIDI effects. The open questions
are settled (see Decisions at the end): MIDI FX live on the MIDI track, their
automation on the MIDI track's lanes, recording keeps what was played, the first
release has all twelve core effects, and every effect has a plain name.

## What exists today

- `engine::MidiEvent` / `MidiEventList` (`engine/Midi.h`): a fixed-capacity list of
  timestamped events per block, allocation-free.
- A MIDI track schedules its span's events (clips, live input, auditions, chase
  after a jump) in `Renderer::scheduleMidi`. It makes no audio.
- An instrument channel merges the events of every MIDI track that plays it (each
  on its MIDI channel, velocities scaled by the track's volume, muted tracks start
  no notes) and hands them to the first insert that `acceptsMidi()`
  (`Renderer::processChain`). A MIDI track can also send its events to an external
  device through `AudioEngine`'s MIDI output queue.
- `engine::PlayHead` already carries tempo, signature and position to every insert,
  and its comment names "an arpeggio" as a use.
- The project has a key (`core::MusicalKey` on the project: root and `ScaleKind`),
  which the piano roll shades and the pitch editor uses.
- CLAP hosting already reads note ports and has an output event list
  (`HostedClapPlugin::OutEvents`).

What is missing: nothing in the chain can *produce* MIDI, there is no place in the
model for a MIDI processor, and nothing keeps notes from hanging when a processor
changes a note between its on and its off.

## Where they go

**On the MIDI track, as its own chain: MIDI FX.** Decided 2026-10-29, over putting
them on the instrument channel ahead of the instrument slot, because in Northlight
the notes belong to the MIDI track:

- Several MIDI tracks can play one multitimbral instrument on different MIDI
  channels. An arpeggiator on the instrument channel would arpeggiate all of them
  together.
- A MIDI track can drive an external synth. MIDI FX on the track work for it too;
  on an instrument channel they would not exist for that track.
- Live input, arm and monitoring are the MIDI track's, so what the player hears
  while they play goes through the same chain.

Order of the path for one MIDI track, per block:

```
clips + live input + chase  →  MIDI FX 1 → MIDI FX 2 → …  →  MIDI out (device)
                                                         →  instrument channel merge
```

Mute, the volume's velocity scale and the channel assignment stay where they are
(the merge), so they apply to what the chain made. This is a decision worth
recording in decisions.md.

## Engine

### The interface

One interface keeps the insert cache, parameters, presets, the generic editor,
automation and hosting working without a second copy. `InsertProcessor` gains:

```cpp
// A MIDI effect: takes the block's events, writes the events it makes. No audio.
virtual bool isMidiEffect() const noexcept { return false; }
// Real-time. `out` is empty on entry; offsets are within the block, in time order.
virtual void processMidiEffect(const MidiEventList& in, MidiEventList& out, int numSamples) noexcept {}
```

Built-ins derive from a new `plugins::MidiEffectBase` (beside `ProcessorBase`):
atomic parameters as now, the play head, blocks longer than `prepare` announced
taken in pieces, and the services below. It does not touch audio, so the audio
bypass crossfade does not apply: bypass passes events through, and switching it
ends the notes the effect was holding (see hanging notes).

### Services every MIDI effect needs (in `MidiEffectBase`)

- **Note tracking.** A fixed table keyed by input (channel, note) recording which
  output notes it started. A note-off releases exactly those, so changing the
  transpose, the chord or the scale while a key is held never leaves a note
  hanging or ends the wrong one. Most hanging-note bugs in other DAWs come from
  skipping this.
- **A pending queue.** Arps, repeats, echoes, strums and fixed lengths make events
  later than the block they were caused in. A fixed-capacity queue of events in
  absolute samples (`PlayHead::samples` plus offset), drained each block.
- **Discontinuities.** The track's runtime already notices a stop, locate or loop
  wrap and ends held notes. It sends `allNotesOff` into the chain; each effect ends
  its outputs, clears its queue and, for tempo-synced effects, re-syncs its clock
  to the new position. A loop wrap must restart an arp on the loop's start beat,
  not continue mid-pattern.
- **A clock while stopped.** A player arpeggiating with the transport stopped
  hears the arp at the project tempo from the first key, on a free-running clock.
  When the transport starts, the clock locks to the song grid.
- **Scale and key.** The project key goes into the snapshot and reaches the effect
  with the play head (a new `PlayHead::keyRoot` / `keyMask`), so "follow project
  key" works and follows key changes without a recompile.
- **Activity.** In and out note counts in the effect's `ProcessorFeedback`, for the
  card's activity lights.

### The renderer

- Each MIDI track node with MIDI FX gets two scratch `MidiEventList`s, sized at
  compile, and ping-pongs through the chain in `scheduleMidi`. The track's `events`
  holds the chain's result, so the merge and MIDI out read it unchanged.
- Capacity: chord and echo effects multiply events. Size the scratch lists at
  compile from the chain (each effect declares a worst-case expansion, clamped to
  a ceiling such as 8192). Overflow drops the event, as the list already does;
  a count of dropped events for the track's trouble line is still to add.
- The chain runs on the render worker that schedules the track. MIDI tracks are
  rendered before the instrument channels they feed, so no new dependency level
  is needed.
- Real-time rules apply in full: no allocation, all tables fixed at `prepare`,
  every RT function `noexcept`, and the debug `operator new` hook covers the new
  path in the offline tests.

### Latency

No lookahead in the first version. An effect that wants to move notes earlier
(humanise "early", a strum that centres on the beat) cannot, because live input has
no future. Timing randomness is late-only and strums start on the beat. Later,
clip playback could read ahead by a declared amount so a lookahead effect is
compensated for clips (not live input); that is a separate decision.

## Model and file

- `core::Channel` gains `std::vector<Insert> midiInserts`, used only on MIDI tracks
  (`TrackKind::midi`). `Insert` is reused as it is (type, bypass, params, state,
  param keys, plugin name). `mix` and `sidechain` do not apply and are ignored.
- `Project::schemaVersion` goes to 19. The field is optional and defaults to empty,
  so older files open. The round-trip test covers a track with two MIDI FX.
- The insert commands (add, remove, move, replace, bypass, parameters, paste) take
  a chain selector (`InsertChain::audio` / `InsertChain::midi`) instead of new
  command types. Each gets apply, undo and redo tests for the MIDI chain. Moving
  between the two chains is refused: a MIDI effect cannot go in an audio chain and
  the reverse.
- The insert cache keys by insert id, which is already unique, so it needs no
  change beyond creating MIDI effects.

## Recording and printing

- Recording keeps what the player played (before the chain), as Logic, Cubase and
  Ableton do, so an arp setting can change after the take. There is no option to
  record the chain's output; Render MIDI FX to clip covers that.
- **Render MIDI FX to clip** (clip menu, and the track's): runs the track's chain
  offline over the selected clips with the same code and play head, writes the
  result as new notes and controller events, and bypasses the chain. One undoable
  command. It needs a deterministic seed for random effects (stored in the effect's
  state), so the print matches what was heard.
- Freeze and bounce of an instrument channel already render from the merged events,
  so they include MIDI FX with no change.

## UI

Specs go in `docs/design/screens/plugins.md` (a new 7.4h, MIDI effects) before the
UI is built; the design system gains the patterns below if it has none.

- **Editor panel.** Selecting a MIDI track shows a MIDI FX section first: a strip of
  cards like the insert chain (7.1), then the instrument channel it plays as now.
  Cards have no wet/dry footer. Each has in/out activity lights in its header.
- **Arrange.** The MIDI track's chip gains a small badge when it has active MIDI FX
  ("→ Keys: Aurora  ch 1  · Arp"). The track menu gets "Add MIDI FX".
- **Insert menus and browser.** A third category, MIDI effects. They are offered
  only where they fit: the MIDI FX "+" slot, and from the browser onto a selected
  MIDI track.
- **Mixer.** MIDI tracks have no strip, so no change.
- **Piano roll.** No change in the first version. Later: a ghost layer showing what
  the chain makes from the clip (render offline, display only).
- **Automation.** MIDI tracks have no automation lanes today. MIDI FX parameters
  need them (an arp rate change at the chorus is common), so MIDI tracks gain
  automation lanes for their MIDI FX parameters only (no volume or pan lanes: the
  track's volume is a velocity scale, not a fader). The arrange spec's "a MIDI
  track's header has no ... automation" changes with it. Automation reaches the
  effect's atomics as it does for any insert, so the engine side is unchanged.
- **Faces.** The arpeggiator and the chord tool get built faces (the step pattern
  and the chord shown on a keyboard); the utilities use a compact designed face
  shared between them; nothing uses the generic editor.
- Launch flags: `--midi-fx <type>` adds an effect to the demo song's first MIDI
  track, so `--capture` shows each card at 100% and 200%.

## Hosted MIDI effects

Later than the built-ins (Phase 5), because the formats differ:

- **CLAP:** a plugin with a note input port, a note output port and no audio ports
  is a note effect. The out-event list is already wired; it needs translating CLAP
  note events back into `MidiEvent`s and the registry marking such plugins as MIDI
  effects.
- **VST3:** there is no MIDI effect category. Plugins that output events (Scaler,
  Cthulhu, many arps) appear as instruments or effects with an event output. JUCE's
  `producesMidi()` tells us; the scanner records it, and such a plugin can be placed
  in a MIDI FX slot (its audio output ignored).
- The out-of-process scanner records "MIDI effect" as a kind so the browser can
  filter on it.

## The MIDI inserts to ship

What users expect from a DAW in 2026, from what Logic, Ableton, Bitwig, Cubase,
FL Studio and Reaper ship. **Core** is Phases 2 and 3, and all twelve ship in the
first release; **later** is Phase 4.

| Name (working) | Tier | What it does |
|---|---|---|
| **Arpeggiator** | Core | Plays held notes one at a time. Modes: up, down, up/down, down/up, as played, random, chord (all at once on each step), converge, diverge. Rate synced (1/1 to 1/64, dotted, triplet) or free. Octave range 1 to 4 with octave mode. Gate length, swing, latch (keeps playing after release), retrigger on new note or on the bar. Velocity as played, fixed, or from an accent row. A 1 to 32 step pattern with rests, ties and per-step velocity. |
| **Chord** | Core | One key plays a chord. Chord by shape (major, minor, 7, maj7, sus2, sus4, add9 …) or by intervals, inversion, voicing (close, open, drop 2, spread), and a scale mode where the chord is built from the project key so each key plays its diatonic chord. Optional strum (up, down, alternating, time in ms or note value). |
| **Chord Pads** (the chord tool) | Core | A progression player: the white keys of one octave trigger a set of chords in the project key (I, ii, iii, IV, V, vi, vii° and borrowed chords), with voice leading between them so changes move smoothly. A bass note on a separate range and preset progressions. This is the feature people buy Scaler and Cthulhu for. |
| **Scale** | Core | Constrains notes to a key and scale: snap to the nearest note, snap up or down, or block out-of-scale notes. Follows the project key or its own. A "white keys play the scale" mode. |
| **Transpose** | Core | Semitones and octaves, or by scale degree (diatonic, so a third stays in the key). |
| **Velocity** | Core | Scale, offset, compress or expand around a centre, a curve, a fixed value, a range clamp and random amount. |
| **Note Length** | Core | Sets lengths: fixed (synced or ms), scaled, legato (each note runs to the next), or staccato. Includes sustain-pedal to note length, for printing piano parts. |
| **Humanise** | Core | Random timing (late only), velocity and length within set amounts, and a probability that each note plays. Deterministic seed for printing. |
| **Note Repeat** | Core | Repeats held notes at a synced rate (drum rolls and ratchets), with a velocity ramp up or down and a gate. Pressure or mod wheel can set the rate. |
| **Note Echo** | Core | MIDI delay: repeats each note with feedback, velocity falling per repeat, an optional pitch step per repeat (up an octave, a fifth, in scale) and a repeat limit. |
| **Key Range** | Core | Passes notes inside a range and blocks the rest, or shifts notes outside it into it by octaves. With two MIDI tracks on one input, this makes a keyboard split. |
| **MIDI Filter** (was Channel & Filter) | Core | Blocks message types (CCs, pitch bend, pressure, notes) and moves one CC to another. It does not set the channel: the track stamps its own after the chain. |
| **Step Sequencer** | Later | A key-triggered pattern: up to 64 steps of pitch offset, velocity, length, probability and ratchets per step, transposed by the held note. Different from the Arpeggiator in that it does not need a chord. |
| **Harmoniser** | Later | Adds diatonic harmony voices to each note (a third and fifth above in the key, a fixed interval below), each with its own velocity. |
| **Mono** | Later | Monophonic: one note at a time with priority (last, highest, lowest) and retrigger of a released held note. Useful before a polyphonic synth playing bass. |
| **Note Map** | Later | Remaps notes by table, chiefly General MIDI drum notes to Pulsar's pads or another kit, with presets per instrument (uses `builtInNoteName`). |
| **Modulator** | Later | Writes CC, pitch bend or pressure from an LFO (synced shapes, rate, depth, phase reset on note), or from an envelope triggered by each note. |
| **Curve** | Later | Reshapes a CC, velocity, pressure or pitch bend through a drawn curve: velocity-to-CC, pressure to mod wheel, smoothing of a stepped controller. |
| **MPE Splitter** | Later | Spreads a polyphonic part over channels for MPE instruments. Needed only once MPE input is supported. |

Names: every MIDI effect has a plain name, the Arpeggiator and Chord Pads included.
Branded names stay for instruments and the big audio effects. Type ids follow the
built-ins: `northlight.arpeggiator`, `northlight.chordpads`, and so on.

## Phases

1. **Plumbing.** The interface, `MidiEffectBase` with note tracking, the pending
   queue, discontinuity handling and the free clock; the renderer chain with
   ping-pong lists; the model field, schema 19 and the chain selector on the insert
   commands; the key on the play head. Tested offline with **Transpose** and a test
   effect that delays every event by more than a block. No UI.
   Tests: no hanging notes when a parameter changes mid-note, when bypass toggles,
   on stop, locate and loop wrap; event order and offsets across block boundaries;
   overflow is counted, not crashing; no allocation on the RT path; command apply,
   undo and redo for the MIDI chain; JSON round trip.
2. **The easy utilities.** Scale, Velocity, Key Range, Channel & Filter, Note
   Length, Humanise. They test the note tracker and the scale plumbing. UI: the
   MIDI FX section in the Editor panel, the shared compact face, the menus, the
   browser category, the arrange badge. Specs first.
3. **Time-based effects.** Arpeggiator, Note Repeat, Note Echo, Chord (with strum),
   Chord Pads. Tests against exact sample positions at several tempos, through a
   tempo ramp, across loop wraps, and stopped (free clock). The Arpeggiator and Chord
   Pads faces. Presets for each. Automation lanes on MIDI tracks. The first release
   ships after this phase, with all twelve core effects.
4. **Render MIDI FX to clip**, then the later list, ordered by demand (Step
   Sequencer and Harmoniser first).
5. **Hosted MIDI effects**: CLAP note effects, then VST3 plugins that produce MIDI,
   and the scanner's MIDI effect kind.
6. **Docs** in the same commits as each phase: architecture.md (MIDI and
   instruments: the chain on the MIDI track), decisions.md (where the chain lives,
   recording before the chain, no lookahead), the plugins spec, the user guide,
   CHANGELOG.md and the roadmap.

Each phase passes the usual bar: no warnings under `ci`, ctest, clang-format, a
capture at 100% and 200% for anything visible, and the UI performance budgets for the
new cards (each built-in joins `uiperf::builtInTypes()`).

## Decisions

Settled 2026-10-29; each goes into decisions.md with the phase that builds it.

| Question | Decision | Why |
|---|---|---|
| Where the chain lives | On the MIDI track, as a MIDI FX chain | The notes belong to the MIDI track: multitimbral parts stay apart, external MIDI out gets the effects, live input goes through them. |
| Automation | Lanes on the MIDI track, for its MIDI FX parameters only | The automation sits beside the effect it moves, and a track driving an external synth can still automate its effects. |
| Recording | What was played, before the chain; no option to record the output | Effect settings stay editable after the take. Render MIDI FX to clip prints the output when it is wanted. |
| First release | All twelve core effects | A release with the arpeggiator but no Note Repeat or Humanise would feel incomplete; the later list follows. |
| Names | Plain names for every MIDI effect | They are tools, not instruments with a character; branded names stay for instruments and the big effects. |
