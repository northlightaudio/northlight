# 9. Piano roll and MIDI

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

- **Toolbar** (left column top, 32 px): the tools as in the arrange view's corner,
  the same icon buttons, keys and cursors: select (1), pencil (3, draws notes) and
  erase (4), the current one lit; then the snap chevron, quantise button with strength menu, "Link zoom to arrange"
  toggle, an audition toggle (speaker icon, on by default: drawing, clicking or
  re-pitching a note sounds it through the track's instrument, armed or not), and a
  scale/key display ("C minor") for the highlight.
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
- The grid (the snap menu: 1/4 to 1/32) divides the beat of the bar where a note
  falls, so a 6/8 bar after 4/4 snaps to its eighths, as the arrange view's grid
  follows the tempo track.
- Select tool: click, Shift/Ctrl as in the arrange; lasso; drag moves (vertical =
  transpose with the pitch shown in a tooltip); edges resize; Ctrl+drag duplicates.
- Erase: click or drag across notes.
- Quantise: Q quantises selected (or all) note starts to the grid at the strength set
  in the menu (100% default); Shift+Q also ends.
- Keyboard (while the piano roll has the focus; all but the arrows rebindable,
  interaction.md 10.4): arrows nudge by grid (Shift = fine, Ctrl = octave for
  vertical), Delete, Ctrl+A, Esc clears the selection, = (or +) and - transpose a
  semitone, Ctrl+D duplicate, 1 / 3 / 4 the select, pencil and erase tools, G snap,
  Q quantise (Shift+Q the ends too), [ and ] a coarser or finer grid.
- Scrolling and zoom: the wheel scrolls the notes up and down, Shift+wheel scrolls
  time, and Ctrl+wheel zooms around the pointer, smoothly, as in the arrange view.
- Recording: incoming notes draw in `record` as they arrive and settle to track colour
  on stop; takes as in audio.
- MIDI clip in the arrange: body as audio clips with a miniature of the notes instead
  of a waveform (pitch range auto-fit, 1 px per note minimum).

## MIDI tracks and their instruments

- Track header: a MIDI input select (device + channel, "All") in place of the
  audio input.
- MIDI tracks, instrument channels and output tracks: see
  [plugins.md 7.4a](plugins.md#74a-instruments-channels-midi-tracks-and-the-rack).

## Acceptance

- A 2-bar clip of 16 notes shows 64 px keys, note colours by velocity, an 80 px
  velocity lane and selected-note styling.
- Draw, move, resize and lasso look right mid-drag.
- 1,000 visible notes at 150% scroll at 60 fps.
