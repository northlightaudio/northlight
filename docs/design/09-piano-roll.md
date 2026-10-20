# 9. Piano roll and MIDI (Phase 8)

Specified now so Phase 8 builds to the design and Phase 7 can cover it.

## Purpose and anatomy

A dock tab ("Piano Roll") that edits the selected MIDI clip, or all MIDI clips on the
selected track in a ghosted overlay.

```
+------+---------------------------------------------------------------+
| tool | R  ruler (bars/beats, shares zoom with arrange when linked)    | 32
+------+---------------------------------------------------------------+
| K    | N  note grid                                                   |
| keys |      notes (track colour 90%), ghost notes 30%, playhead       |
| 64px |                                                               |
+------+---------------------------------------------------------------+
| ---- | V  velocity lane (resizable, default 80 px)                   |
+------+---------------------------------------------------------------+
```

- **Toolbar** (left column top, 32 px): segmented control for tool (Draw, Select,
  Erase), snap chevron, quantise button with strength menu, "Link zoom to arrange"
  toggle, an audition toggle (speaker icon, on by default: drawing a note, clicking
  one and dragging it to another pitch sound it through the track's instrument,
  armed or not), a scale/key display ("C minor") for the highlight.
- **Keys**: 64 px keyboard, white keys `surface2`, black keys `surface0`, C labels in
  `caption`; keys light `accent` when a note on that pitch plays; clicking a key
  previews the note through the track's instrument.
- **Note grid**: `canvas`; rows alternate `laneAlt` for black keys; octave lines
  `gridBar`; beat grid as the arrange; out-of-scale pitches dimmed when a scale is
  set (`laneAlt` darker).
- **Notes**: track colour at `waveformAlpha`, radius `sm`, 1 px inset `canvas`
  outline so adjacent notes separate; velocity shown as alpha from
  `midiNoteVelocityLow` (v=1) to full (v=127); selected notes `textPrimary` outline
  and `selectionFill` tint; ghost notes from other clips at 30%; the note name
  appears inside notes taller than 14 px.
- **Velocity lane**: a bar per note in track colour with a 6 px square cap; drag
  caps to change velocity; drag across to ramp; selected notes' bars brighten.

## Behaviour

- Draw tool: click adds a note of the last-used length at the snapped position;
  drag sets length; Alt disables snap. Double-click in Select mode also adds.
- Select tool: click, Shift/Ctrl as in the arrange; lasso; drag moves (vertical =
  transpose with the pitch shown in a tooltip); edges resize; Ctrl+drag duplicates.
- Erase: click or drag across notes.
- Quantise: Q quantises selected (or all) note starts to the grid at the strength set
  in the menu (100% default); Shift+Q also ends.
- Keyboard: arrows nudge by grid (Shift = fine, Ctrl = octave for vertical), Delete,
  Ctrl+A, +/- transpose semitone, Ctrl+D duplicate.
- Recording: incoming notes draw in `record` as they arrive and settle to track colour
  on stop; takes as in audio.
- MIDI clip in the arrange: body as audio clips with a miniature of the notes instead
  of a waveform (pitch range auto-fit, 1 px per note minimum).

## MIDI tracks and their instruments

- Track header gains a MIDI input select (device + channel, "All") in place of the
  audio input, and the instrument's name as a chip under the name that opens its
  window.
- Mixer strip: the instrument appears as the first insert card with an "INST"
  badge; multi-output instruments add child strips grouped under the instrument.

## Acceptance

- Screenshot with a 2-bar clip of 16 notes matches: 64 px keys, note colours by
  velocity, velocity lane 80 px, selected note styling.
- Draw, move, resize and lasso mid-drag screenshots match.
- Rendering 1,000 visible notes at 150% scrolls at 60 fps.
