# 13. Vocal pitch editing

Melodyne-style editing of a monophonic vocal clip, inside the audio clip editor:
analyse the clip for pitch, edit each note, mute or reduce breaths, and correct the
notes towards the key. The result must sound musical, not like pitches snapped to
the nearest frequency.

## Summary

The audio clip editor is a dock panel that edits one audio clip, as the piano roll
edits one MIDI clip. Pitch editing has three parts:

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
| Audio clip editor | `src/ui/AudioEditor.*`; opened by double-clicking an audio clip | Same shape as the piano roll: one clip, edits out as commands |
| Analysis and rendering | `src/engine/pitch/` (no JUCE), with `engine::VocalService` as the cache and render service; analyses cached beside the media as `<file>.<rate>.northlight-pitch` | Derived data that can be rebuilt, kept out of the project file; the bounce uses the same code without JUCE |
| Edits | `core::VocalEdit` on `AudioClip`: notes with their parameters, breath segments with their gain | Splitting, merging and every parameter go through the undo stack, so they belong in the model |
| Playback | `PitchEditedSource`: rendered phrases where they exist, the original audio elsewhere | Re-rendering runs off the audio thread and publishes immutable results; nothing allocates on the audio thread |
| Bounce | Renders the clip's vocal edits before it starts | Bounce builds its own engine, so it cannot rely on the live render cache |

A **phrase** (a voiced run between pauses) is the unit of rendering. The
resynthesis only looks a few milliseconds either side of any point, so an edit never
reaches past a pause.

## Analysis

1. **Pitch curve.** pYIN (probabilistic YIN with Viterbi smoothing) at a 5 ms hop:
   f0 in cents and a voicing probability per frame. Written from the paper, not from
   the GPL reference code. Stereo vocals are analysed on the mid signal (L+R).
2. **Pitch marks.** One mark per glottal pulse in voiced regions, for the
   resynthesis.
3. **Notes.** Split at unvoiced gaps, at loudness onsets, and at pitch jumps of more
   than about 70 cents that hold for longer than 60 ms. A note's **centre** is the
   loudness-weighted mean of its stable middle, leaving out the attack scoop and the
   release fall: closer to the pitch the ear hears than a median.
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
- It keeps formants, so a vocal moved down does not sound like a giant.
- It is transparent for the shifts vocal tuning uses (under ±3 semitones) and
  fine up to about ±5.
- It is cheap enough to re-render live while a note is dragged.

Unvoiced audio passes through untouched. At voiced and unvoiced edges the pitch
ratio eases to 1 over about 10 ms, and breath gain uses 10 to 15 ms fades, so
nothing clicks.

**Null rule:** a note with no edits renders bit-identical to the original, so
opening the editor never changes the sound. A test enforces it.

No dependency is added. Signalsmith Stretch (MIT) is the fallback if large formant
shifts or timing edits need it.

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
  time, gain, mute. The inspector shows them for the selection.

## Correct to key

A suggestion pass, not a snap. It sets each note's target and drift amount as one
undoable command, and any note can be adjusted afterwards.

1. **Key.** **Decision:** the project has a key and scale in the model, shared with
   the piano roll's scale highlight. When none is set, one is estimated from a
   pitch-class histogram weighted by duration and loudness (Krumhansl profiles) and
   the user confirms it.
2. **Target note.** A small dynamic-programming pass over the notes, scoring the two
   nearest scale degrees of each, and the nearest semitone (penalised when outside
   the key), by:
   - distance in cents;
   - keeping the melody's direction (a rising interval never becomes falling or flat);
   - repeated sung pitches landing on the same target;
   - a bonus for chord tones from MIDI notes sounding at that moment (optional).

   This stops a note sung 50 cents flat from landing on the wrong neighbour. A
   chromatic note sung in tune stays chromatic, and a wrongly detected key (a short
   phrase can suggest the relative key) cannot pull a note onto a different scale
   step: the key breaks ties, it does not force.
3. **How far to move.**
   - Notes within the tolerance (default ±10 cents) are left alone.
   - Others move by the strength (default 85%). Leaving a little of the singer's
     offset is much of what keeps it from sounding robotic.
   - Drift is trimmed by 40% by default; vibrato is kept at 100%.
   - Notes shorter than about 90 ms (ornaments, melismas) get much weaker correction.
   - No note moves more than about a semitone automatically. Larger moves are flagged
     for the user to check.
4. **The sheet:** key, strength, tolerance, drift %, keep vibrato, chord-aware.
   Changes preview live inside one gesture; Apply, or clicking away, keeps the
   result; Cancel undoes it.

## Breaths

- Breaths are drawn as grey hatched blobs.
- One bulk action: **Breaths: Mute / Reduce by [12] dB**. **Decision:** reduce by
  12 dB is the default; it usually sounds more natural than a full mute.
- Clicking a breath toggles it. Any unvoiced segment can be turned into a breath, or
  back, to fix a wrong classification.
- Breaths are gain envelopes in the render path, so they are cheap.

## The audio clip editor

- **Wave mode:** a zoomable detailed waveform, clip gain and fades.
- **Pitch mode:** entering it the first time starts the analysis, with a progress
  bar, and adds an "Analyse pitch" undo step that stores the notes found (nothing
  audible changes). Then:
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

Layout, tokens and states for this panel are not yet specified.

## Acceptance

- Analysis of synthetic voices (a pulse train through a formant filter, with known
  glides and vibrato): pitch error under 5 cents and the expected note boundaries.
- Undo and redo for every command.
- Rendering: the null rule; output pitch within 3 cents of the target; formant
  envelope kept (LPC distance); no step larger than a click threshold at edges;
  phrase swaps pass the real-time allocation hook.

Deferred: a formant control, timing edits (on the clip time-stretch), larger
formant shifts, exporting the melody as MIDI, harmony generation.
