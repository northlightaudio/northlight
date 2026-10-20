# 13. Vocal pitch editing (Phase 11)

Status: **built**, 2026-10-20, on the `pitch-editing` branch. "As built" at the end
records where the build differs from this plan.

Melodyne-style editing of a monophonic vocal clip, inside a new audio clip editor:
analyse the clip for pitch, edit each note, mute or reduce breaths, and correct the
notes towards the key. The result must sound musical, not like pitches snapped to
the nearest frequency.

## Summary

There is no wave editor yet (the Editor panel is the plugin chain), so the work
starts with an audio clip editor: a dock panel that edits one audio clip, as the
piano roll edits one MIDI clip. Pitch editing then has three parts:

- **Analysis**, once per media file, in the background: a pitch curve, pitch
  marks, notes, and breaths.
- **Edits stored as parameters on each note**, never as baked audio, so every edit
  stays undoable and can be changed later.
- **A background renderer** that rebuilds only the phrase that changed and swaps it
  into playback without touching the audio thread.

What makes the result musical is keeping a note's centre, its slow drift and its
vibrato apart. A correction moves the centre and trims some drift, but keeps the
vibrato and the singer's own slides between notes.

## Where it fits

| Part | Where | Why |
|---|---|---|
| Audio clip editor | `src/ui/AudioEditor.*`; opened by double-clicking an audio clip; registered with `PanelManager` like `PianoRoll` | Same shape as the piano roll: one clip, edits out through `onEdit` as commands |
| Analysis cache | `src/audio/PitchStore.*`: a background thread and a sidecar `<media>.aural-pitch` | Same pattern as `PeakStore`: derived data that can be rebuilt, kept out of the project file |
| Analysis algorithms | `src/engine/pitch/` (no JUCE) | Tested in `aural_unit_tests` |
| Edits | `core::VocalEdit` on `AudioClip`: notes (source sample ranges, original centre) with each note's parameters; breath segments with their gain | Splitting and merging notes, and every parameter, go through the undo stack, so they belong in the model. Bumps the project file version |
| Playback | `PitchEditedSource : AudioSource`: reads rendered phrases where they exist and the original audio elsewhere | The renderer and the snapshot compiler do not change; `MediaCache` keys the source as `path#vocal:<editHash>` |
| Re-rendering | A controller-side render thread (not the real-time `WorkerPool`) that publishes an immutable phrase table atomically, like `SnapshotPublisher` | Dragging a note re-renders about one phrase in milliseconds; nothing allocates on the audio thread |
| Bounce | `BounceJob` renders the clip's vocal edits synchronously before it starts | Bounce builds its own engine, so it cannot rely on the live render cache |

A **phrase** (a voiced run between pauses) is the unit of rendering. The
resynthesis only looks a few milliseconds either side of any point, so an edit never
reaches past a pause.

## Analysis

1. **Pitch curve.** pYIN (probabilistic YIN with Viterbi smoothing) at a 5 ms hop:
   f0 in cents and a voicing probability per frame. Written from the paper, not from
   the GPL reference code. Stereo vocals are analysed on the mid signal (L+R).
2. **Pitch marks.** One mark per glottal pulse in voiced regions; the resynthesis
   needs them.
3. **Notes.** Split at unvoiced gaps, at loudness onsets, and at pitch jumps of more
   than about 70 cents that hold for longer than 60 ms. A note's **centre** is the
   loudness-weighted mean of its stable middle, leaving out the attack scoop and the
   release fall. That matches the pitch the ear hears better than a median does.
4. **Curve split.** Within a note, `original = centre + drift + vibrato`. Drift is a
   zero-phase low-pass (below about 3 Hz) of the deviation from the centre; vibrato
   is what remains. **Transitions** are the stretches where the curve travels between
   two neighbouring centres (at least 30 ms).
5. **Unvoiced segments** are sorted into three kinds:
   - **Breath:** 120 to 1000 ms, broadband noise with a centroid around 1 to 4 kHz,
     quieter than the phrase, usually just before a phrase starts.
   - **Sibilant:** short, energy mostly above 4 kHz, right next to voiced audio.
   - **Silence or room tone.**

   Only breaths get a tool. Sibilants are shown but never touched.

## Resynthesis

**Decision:** pitch-synchronous overlap-add (PSOLA), written in-house.

- Each grain is two periods long under a Hann window; grains are re-spaced at the
  target period and overlap-added.
- It keeps formants naturally, so a vocal moved down does not sound like a giant.
- It is very transparent for the shifts vocal tuning uses (under ±3 semitones) and
  fine up to about ±5.
- It is cheap enough for live re-rendering while a note is dragged.

Unvoiced audio passes through untouched. At voiced and unvoiced edges the pitch
ratio eases to 1 over about 10 ms, and breath gain uses 10 to 15 ms fades, so
nothing clicks.

**Null rule:** a note with no edits renders bit-identical to the original, so
opening the editor never changes the sound. A test enforces it.

This adds no dependency and does not depend on the open licensing decision (PLAN.md,
section 10). Signalsmith Stretch (MIT) is the fallback if large formant shifts or
timing edits need it later.

## Editing model

Every note has parameters, not baked audio. For note *n* with original centre *c*
and target *T*:

```
new(t) = T + driftAmount·drift(t) + vibratoAmount·vibrato(t)
```

- **Transitions.** Across a join between notes *n* and *n+1*:
  `new(t) = original(t) + lerp(offsetₙ, offsetₙ₊₁, smoothstep(s))`, where an offset
  is a note's `T - c`. The singer's own slide keeps its shape and moves with the
  notes instead of stepping.
- **Attack protection.** Drift correction ramps in over a note's first 60 ms or so,
  so natural scoops into a note survive.
- **Parameters per note:** pitch (cents), drift %, vibrato %, transition in and out
  time, gain, mute; a formant control later. The inspector shows them for the
  selection.

## Correct to key

A suggestion pass, not a snap. It sets each note's target and drift amount as one
undoable command, and any note can be adjusted afterwards.

1. **Key.** **Decision:** add a project key and scale to the model, shared with the
   piano roll's scale highlight. When none is set, estimate one from a pitch-class
   histogram weighted by duration and loudness (Krumhansl profiles) and ask the user
   to confirm it.
2. **Target note.** A small dynamic-programming pass over the notes, scoring the two
   nearest scale degrees of each by:
   - distance in cents;
   - keeping the melody's direction (a rising interval never becomes falling or flat);
   - repeated sung pitches landing on the same target;
   - a bonus for chord tones from MIDI notes sounding at that moment (optional).

   This is what stops a note sung 50 cents flat from landing on the wrong neighbour.
3. **How far to move.**
   - Notes within the tolerance (default ±10 cents) are left alone.
   - Others move by the strength (default 85%). Leaving a little of the singer's
     offset is much of what keeps it from sounding robotic.
   - Drift is trimmed by 40% by default; vibrato is kept at 100%.
   - Notes shorter than about 90 ms (ornaments, melismas) get much weaker correction.
   - No note moves more than about a semitone automatically. Larger moves are flagged
     for the user to check instead.
4. **The sheet:** key, strength, tolerance, drift %, keep vibrato, chord-aware.
   Changes preview live before Apply.

## Breaths

- Breaths are drawn as grey hatched blobs.
- One bulk action: **Breaths: Mute / Reduce by [12] dB**. **Decision:** reduce by
  12 dB is the default; it usually sounds more natural than a full mute.
- Clicking a breath toggles it. Any unvoiced segment can be turned into a breath, or
  back, to fix a wrong classification.
- It is all gain envelopes in the render path, so it is cheap.

## The audio clip editor

- **Wave mode:** a zoomable detailed waveform (from `PeakStore`), clip gain and
  fades.
- **Pitch mode:** entering it the first time starts the analysis, with a progress
  bar. Then:
  - the piano roll's keyboard and scale-highlighted grid;
  - notes drawn as blobs shaped by their loudness;
  - the original pitch curve faint, the edited curve bright.
- **Tools:**
  - **Pitch:** drag vertically to snap to semitones; Alt moves in free cents.
  - **Split and merge:** double-click splits.
  - **Drift and vibrato:** drag vertically to scale them.
  - **Transition handles** at the joins between notes.
  - **Breath.**
- Clicking a note auditions it. The ruler and playhead link to the arrange view, as
  the piano roll's "Link zoom to arrange" does.

Layout, tokens and states for this panel are to be specified in this document (in
the shape of the other screen specs) before the Phase 11 UI work starts.

## Phases

1. **Audio clip editor panel:** waveform, gain, fades. No pitch yet, but useful on
   its own.
2. **Analysis engine and `PitchStore`.** Tests on synthetic voices (a pulse train
   through a formant filter, with known glides and vibrato): pitch error under
   5 cents and the expected note boundaries.
3. **Model, commands and JSON,** with undo and redo tests for every command.
4. **PSOLA renderer, `PitchEditedSource`, background re-render and bounce.** Tests:
   - the null rule;
   - output pitch within 3 cents of the target;
   - formant envelope kept (LPC distance);
   - no step larger than a click threshold at edges;
   - phrase swaps under the real-time allocation hook.
5. **Pitch mode UI and tools.**
6. **Correct to key,** with the project key and scale and key detection.
7. **Breaths.**

Later: timing edits (they need time-stretch, the Phase 9 open decision), larger
formant shifts, exporting the melody as MIDI, harmony generation.

## Decisions to confirm

- **Project key and scale in the model:** recommended yes, shared with the piano
  roll.
- **Timing edits:** left out of the first version.
- **Breath default:** reduce by 12 dB rather than a full mute.

## As built (2026-10-20)

- The analysis cache and render service are one engine class,
  `engine::VocalService` (src/engine/pitch/VocalService.h), instead of a JUCE
  `PitchStore` in src/audio: the bounce needs the same analysis and renders
  without JUCE. It caches analyses beside the media as `<file>.<rate>.aural-pitch`.
- Correct to key also considers each note's nearest semitone, with a penalty when
  it is outside the key. A chromatic note sung in tune stays chromatic, and a wrong
  key (detection from a short phrase can pick a relative key) cannot pull a note
  onto a different scale step. The key breaks ties; it does not force.
- The correction sheet previews live inside one gesture. Apply, or clicking away,
  keeps the result; Cancel undoes it.
- Opening pitch mode on a clip for the first time adds an "Analyse pitch" undo
  step that stores the notes found. It changes nothing audible.
- Launch flags for unattended checks: `--audio-editor [pitch]`, `--correct-pitch`,
  `--mute-breaths`.
- Not built yet: the formant control, and the layout/token spec for the panel.
