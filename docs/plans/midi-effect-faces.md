# Plan: faces for the MIDI effects

A design review of the twelve MIDI effects as built (captured at 100% and 200% on
`main` at 6776814), and what each needs to meet the design bar
([principles.md](../design/principles.md#the-design-bar)).

**Status (2026-10-29), on the `midi-faces` branch (not committed):** steps 1 to 4
are done for the first five. The defects are fixed (a settings row's segments
become a menu when they would not fit; Velocity's Fixed and MIDI Filter's Move
controller are each a switch and a value; no unit repeats its label). The MIDI
face, the keyboard strip, the step lane and the pad map are in design-system.md;
the effects publish their notes (`engine::NoteFeedback`); and the Arpeggiator,
Chord, Chord Pads, Velocity and Scale have faces in Heather (screens/plugins.md
7.4h), tested, captured at 100% and 200% at rest and playing, and inside the paint
budgets. Not done: faces for the other seven (they keep the settings rows),
presets (step 5), and Key Range's 88-key strip. Where the build differs from the
table below, the spec is the record: the Arpeggiator's velocity is a toggle and
a bud; Velocity is wide, with its range as buds as well as handles; the key
chips sit in the display's corner and show the key in use.

## Verdict

None of the twelve meets the bar. They work, but every card is a form: a column
of labels, combos and number boxes, the same for an arpeggiator as for a filter.
That is the "spreadsheet" the brief warns against, and it breaks the rule every
other built-in follows (screens/plugins.md 7.2a): **a live, draggable display in a
well, then controls in the module's hue.** Nothing on these cards shows what the
effect does to the notes, nothing moves when you play, and nothing carries a hue.
They do not belong beside Glue, Bloom or Aurora.

The settings-rows face (design-system.md "Settings rows") was a fix for a layout
bug, not a design. It should stay only as the fallback for a hosted MIDI effect
with no GUI, the way the generic grid serves hosted audio plugins.

## What is wrong everywhere

1. **No display.** The effect's behaviour is invisible: the chord a key plays, the
   arpeggio's order, the velocity curve, the range on the keyboard. Every one of
   these has an obvious picture.
2. **No live feedback.** Playing a note changes nothing on the card. The plan's
   in/out activity lights were never built. The built-in audio effects all show
   their signal; a MIDI effect should show its notes.
3. **No hue.** The cards are greyscale with the accent on toggles. Every built-in
   has a hue; MIDI has one already: Heather (`midiNote`).
4. **The wrong controls.** Small whole-number ranges are typed boxes (Octaves 1 to
   4, Inversion 0 to 3, Octave −2 to 2, Repeats 1 to 16) where pills or a stepper
   read at a glance. Continuous amounts (gate, swing, feedback, chance) are number
   boxes instead of knobs. Notes are typed text ("C-2", "G8") instead of keys.
5. **Settings that do nothing still show.** With Key set to Project, Root and Scale
   are live controls that change nothing. Fixed velocity shows while Velocity is As
   played; Strum time while Strum is Off; Note value and Time both show whatever the
   Length mode is.
6. **Dead space.** Each card fills the dock's height with its controls in the top
   third. Card widths run from 200 to 360 px at random, set by the longest
   control, so the chain reads ragged.
7. **Weak hierarchy.** Labels are `caption` beside 14 px controls, so the names are
   smaller than the values. No grouping: the Chord card's ten settings are one
   undivided column.
8. **No presets.** Every built-in effect has a preset bar; an arpeggiator without
   patterns to start from is unusual.

## Defects to fix whatever the redesign

These fail the bar outright and should be fixed first:

- **Note Length's four Length pills collide.** "As played" runs into "Note value"
  with no gap, and the segments are uneven (Time's is twice As played's): the
  segmented control sizes its segments to the space, not to their labels.
- **"Fixed: Off" on Velocity and "Move controller: Off" on MIDI Filter** read as
  errors, not choices: an on/off and a value should be two controls.
- **Units that say nothing.** "Semitones 0 st" and "Octaves 0 oct" repeat the label;
  "Octave 0 oct" on Chord Pads is a register, which reads better as a pill row.

## The shared pattern: a MIDI face

Proposed for design-system.md, before any face is built:

- **Hue**: Heather for the whole family, so MIDI effects read as one group in the
  chain, apart from the audio effects. Notes on displays are Heather; the
  effect's output is Heather, the input `textTertiary`.
- **Layout**: the strip's card language (ModuleCard, `takeDisplay`): a display in a
  well across the top, then a row of knobs, pills and choices in groups
  (`KnobGroups`), like Bloom and Vesper. A fixed width per effect, chosen from
  three sizes (narrow 224, medium 320, wide 416), so the chain lines up.
- **Keyboard strip**: a shared display part. Two octaves of keys at the bottom of
  a well, notes held going in lit `textTertiary`, notes coming out lit in Heather,
  scale notes marked by a dot. Most effects use it (Scale, Transpose, Chord, Chord
  Pads, Key Range).
- **Time lane**: a shared display part. The last two bars (or the next, for an
  arpeggio) on the song's grid, notes as Heather bars at their pitch and length,
  velocity as their opacity, a playhead line. Used by the Arpeggiator, Note Repeat,
  Note Echo, Note Length and Humanise.
- **Activity**: a dot in the header (in Heather) that flashes for a note out, so a
  bypassed or blocking effect is obvious.
- **Live data**: each effect publishes the notes held, the notes it played and its
  step position through `ProcessorFeedback`, as the audio effects publish meters.
  The card reads it at the panel's frame rate; nothing new on the audio thread
  beyond writing a few atomics.
- **Hidden when moot**: a control that does nothing in the current mode is not
  shown (the card does not reflow: its group keeps its place and shows the mode's
  own controls).

## Each effect

| Effect | What is wrong now | Proposed face |
|---|---|---|
| **Arpeggiator** | Eight rows; nothing shows the pattern; Fixed velocity always shown | Wide. Time lane showing the next bar of steps from the held chord (in order, with octaves stacked, gate as bar length, swing as the offset of every second step), lit as they play. Below: Mode as a pill row with small direction glyphs (up, down, up/down, down/up, as played, random, chord); Rate (corner choice); Octaves pills 1 to 4; Gate and Swing knobs; Latch toggle (lit in Heather while latched); Velocity pills with the Fixed knob only when Fixed. |
| **Chord** | Ten undivided rows; the chord is invisible | Medium. Keyboard strip with the last key played and the chord it made (the voicing exactly, so inversion and drop 2 are visible), named in the corner ("Cmaj7 / E"). Groups: CHORD (Shape or In key pills, then Shape or Size), VOICING (Inversion pills Root, 1st, 2nd, 3rd; Voicing pills), STRUM (pills; Time knob only when strumming). Key choices only with In key, and only Own key's root and scale. |
| **Chord Pads** | Shows none of the chords it plays | Wide. The pad map: seven pads for the white keys and five for the black, each named with its numeral and chord in the key ("ii Dm", "bVII Bb"), the pad playing lit in Heather. A keyboard strip under it with the voicing played. Then Size pills, Voice leading and Bass toggles, Octave pills (−2 to +2). The key shows as a chip ("Project key: C major"), its own Root and Scale only when set to Own. |
| **Note Repeat** | Three rows; nothing shows the repeats | Narrow. Time lane of one beat of repeats for the held note, ramp drawn as the bars' heights. Rate corner choice, Gate and Ramp knobs. |
| **Note Echo** | Four rows; nothing shows the echoes | Medium. Time lane of one note and its repeats, fading in opacity and stepping in pitch. Time corner choice, Repeats stepper, Feedback and Pitch knobs. |
| **Scale** | Mode combo, key controls always shown | Medium. Keyboard strip with the scale's notes marked and each key's destination drawn as a short Heather arc from the key played to where it goes (Nearest, Up, Down), blocked keys dimmed. Mode pills; key chip, own Root and Scale when Own. |
| **Transpose** | Three number boxes | Narrow. Keyboard strip with the last note in and out joined by an arc. Steps, Semitones and Octaves as knobs with centre detents (bipolar arcs from 0). |
| **Velocity** | Eight number boxes; the curve is invisible | Medium. The transfer curve in a well (velocity in along the bottom, out up the side), with the last notes as dots on it, Minimum and Maximum as draggable horizontal limits, Centre as a handle. Knobs: Dynamics, Gain, Offset, Random. Fixed as a toggle with its knob. |
| **Note Length** | Pills collide; both length controls always shown | Medium. Time lane with notes as played (`textTertiary`) and as they leave (Heather). Mode pills; Note value or Time by mode; Pedal toggle. |
| **Humanise** | Four number boxes | Narrow. Time lane of the last notes with their shift drawn as a short tail from the grid, velocity spread as opacity. Knobs: Timing, Velocity, Chance; Seed as a small stepper with a dice button that picks a new one. |
| **Key Range** | Notes typed as text | Narrow. A full 88-key strip with the range as a Heather band whose ends drag; notes outside dimmed or folded (an arrow into the range). Outside pills. |
| **MIDI Filter** | Five identical Block/Pass rows, then two number boxes that only mean something together | Narrow. A grid of five event chips (Notes, CC, Bend, Pressure, Poly) lit in Heather while passing, each flashing when its kind goes through. The controller move as one row: a toggle, then "CC 1 → CC 11" with both numbers as fields. |

## Order of work

1. Fix the defects (Note Length's pills, the two "Off" values, the repeated
   units), so what ships now is not broken.
2. Add the MIDI face pattern, the keyboard strip and the time lane to
   design-system.md, with Heather as the family hue; write each face into
   screens/plugins.md 7.4h. Captures of a mock first, for review.
3. The live data: each effect's `ProcessorFeedback` (held, played, step), tested.
4. The faces, in the order people meet them: Arpeggiator, Chord Pads, Chord,
   Velocity, Scale; then the rest. Each joins `uiperf::builtInTypes()` and passes
   the paint budgets; each is captured at 100% and 200%, at its narrowest dock, and
   with long values.
5. Presets: patterns for the Arpeggiator, progressions for Chord Pads, curves for
   Velocity, grooves for Humanise.

## Questions

1. **Hue**: Heather for the whole family (recommended), or a hue per group (Play,
   Shape, Route)?
2. **Scope**: all twelve to the full design above, or the five people use most
   first (Arpeggiator, Chord Pads, Chord, Velocity, Scale) with the rest moved onto
   the shared pattern without a bespoke display?
