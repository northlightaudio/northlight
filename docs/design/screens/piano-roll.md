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
| ---- | C  more lanes (a controller each), stacked, each resizable     |
+------+---------------------------------------------------------------+
```

- **Toolbar** (32 px), left to right: the tools as in the arrange view's corner,
  the same icon buttons, keys and cursors: select (1), pencil (3, draws notes),
  erase (4), split (5, the scissors), glue (6) and mute (7), the current one lit;
  then the grid menu (ghost button with chevron: "1/16", "Off 1/16" with snap off),
  Quantise (menu: quantise starts or ends, strength, swing, humanise), the default
  velocity ("Vel 100", menu), then icon toggles: link zoom to arrange, follow the
  playhead, audition (speaker, on by default: drawing, clicking or re-pitching a
  note sounds it through the track's instrument, armed or not), other tracks'
  ghosts, and, when the instrument names its notes, named rows only (the filter
  icon); the Scale toggle; and at the right the project key as a ghost menu button
  ("C minor", "No key"), which sets the key (the same menu as the audio editor's).
  A narrow panel drops the velocity menu and Scale before anything overlaps the key.
- **Keys**: 64 px keyboard, white keys `surface2`, black keys `surface0`, C labels in
  `caption`; keys light `accent` when a note on that pitch plays; clicking a key
  previews the note through the track's instrument. An instrument that names its
  notes (Pulsar's pads: Kick on 36, Rim, Snare, ...) labels those keys with the
  names in `caption` `textPrimary`; with named rows only, the rows not named (and
  without notes) hide and each row is a plain key (`surface2` and `surface1`
  alternating) with its name. A name several notes share shows once, on the note
  the clip plays.
- **Note grid**: `canvas`; rows alternate `laneAlt` for black keys (every other row
  with named rows only); octave lines `gridBar`; beat grid as the arrange;
  out-of-scale pitches dimmed when a scale is set (`laneAlt` darker).
- **Notes**: track colour at `waveformAlpha`, radius `sm`, 1 px inset `canvas`
  outline so adjacent notes separate; velocity shown as alpha from
  `midiNoteVelocityLow` (v=1) to full (v=127); selected notes `textPrimary` outline
  and `selectionFill` tint; ghost notes from the track's other clips at 30%; the
  note name (or the instrument's name for it) appears inside notes taller than 14 px.
  A muted note is hollow: the track colour at 12% inside, a 1 px outline at 70% and
  a hairline through its middle at 50%; it plays nothing.
- **Other tracks' ghosts** (the ghost toggle): the other MIDI tracks' notes in their
  own track colours at 20% with a 1 px outline at 45%, under this clip's notes.
- **Lanes**: under the grid, top to bottom, at most four. Each is the notes'
  velocities or one controller; the name at its left (`captionCaps`, with a chevron)
  opens its menu: the lane's kind (velocity, pitch bend, mod wheel, expression,
  sustain, pressure, other CC), "Add a lane" and "Remove this lane". A lane's top
  edge drags its height (40 to 320 px; the cursor is up-down, the rule lights 2 px
  `accent` while hovered or dragged).
- **Velocity lane**: a bar per note in track colour with a 6 px square cap (a muted
  note's in `textTertiary`); drag a cap: the selection moves with it, every note by
  the same amount, so the phrase keeps its dynamics (Alt: every selected note to
  the value), with a tooltip ("Velocity 96  (+12 each)"); drag across to ramp;
  selected notes' bars brighten.

## Behaviour

- Draw tool: click adds a note of the last-used length at the snapped position with
  the default velocity (the last velocity edited, or the one picked in the toolbar's
  menu); drag sets length; Alt disables snap. Double-click in Select mode also adds.
- The grid menu: Snap, Adaptive, Bar, 1/4 to 1/32, and submenus of triplets (1/4T
  to 1/32T) and dotted values (1/4. to 1/16.). A division divides the beat of the bar
  where a note falls, so a 6/8 bar after 4/4 snaps to its eighths, as the arrange
  view's grid follows the tempo track. [ and ] step finer or coarser within the
  grid's family; G turns snap on and off.
- Select tool: click, Shift/Ctrl as in the arrange; lasso; drag moves (vertical =
  transpose with the pitch shown in a tooltip); edges resize; Ctrl+drag duplicates.
- Erase: click or drag across notes. Split: click a note to cut it at the snapped
  pointer (a selected note cuts every selected note there). Glue: click a note to
  join it with the next note of its pitch (a selection: each pitch's run joins).
  Mute: click or drag across notes; the first note decides whether the sweep mutes
  or unmutes.
- Quantise: Q quantises selected (or all) note starts to the grid at the strength set
  in the menu (100% default); Shift+Q also ends. Swing (the menu, 50% to 75% as drum
  machines count it, 66% a triplet feel) makes every second grid line late.
  Humanise moves the notes' timing (±5, 10 or 20 ms) and velocity (±5 or 10) at
  random, one step.
- Right-click on the grid: the note under the pointer joins the selection, then a
  menu: Cut, Copy, Paste here, Duplicate, Delete; Quantise, Quantise starts and
  ends, Humanise timing; Velocity (set to 127/100/80/64/32, louder, softer, scale,
  compress or expand about the average, randomise, humanise); Length (legato to the
  next note, the grid step, halve, double); Transpose (a semitone or an octave);
  Mute or Unmute; Split here; Join; Select all of the pitch under the pointer;
  Select all.
- Clipboard: Ctrl+C, Ctrl+X, Ctrl+V pastes at the playhead when it is inside the clip,
  else where the pointer last was in the grid (snapped); the pasted notes are
  selected. The clipboard is shared by every piano roll, so notes go between clips.
  Ctrl+D duplicates right after the selection, rounded to whole grid steps.
- Keyboard (while the piano roll has the focus; all but the arrow families
  rebindable, interaction.md 10.4): arrows nudge by grid (Shift = fine, Ctrl = octave for vertical), Delete,
  Ctrl+A, Esc, +/- transpose a semitone, Ctrl+Left/Right select the previous or next
  note (Shift extends), Alt+Left/Right shorten or lengthen by a grid step (Shift: a
  quarter of one), Ctrl+M mutes, S splits the selection at the playhead, J joins,
  Z zooms to the notes, Ctrl+= and Ctrl+- zoom time (the window's zoom keys), Ctrl+D
  duplicates, 1 / 3 / 4 / 5 / 6 / 7 the select, pencil, erase, split, glue and mute
  tools, G snap, Q quantise (Shift+Q the ends too), [ and ] a coarser or finer grid.
- Scrolling and zoom: the wheel scrolls the notes up and down, Shift+wheel scrolls
  time, and Ctrl+wheel zooms around the pointer, smoothly, as in the arrange view.
  Ctrl+Shift+wheel (or Alt+wheel) zooms the rows (6 to 32 px) around the pointer.
  Z fits the notes: time with a grid step either side, rows as tall as fit.
- Follow (the toolbar toggle): a playhead that passes either edge of the view turns
  the page, so it lands an eighth of the view in. The audio editor has the same.
- The selected notes show in the Inspector (shell.md) as a Note card: pitch (typed as
  C3, F#2 or a number), start (bar.beat.tick), length (beats), velocity and Mute.
  With several notes the fields show the first; a change moves them all by the same
  amount.
- Recording: incoming notes draw in `record` as they arrive and settle to track colour
  on stop. What the passes become is the MIDI track's record mode (the Inspector's
  Recording card): Takes (as audio: loop passes, a punch, or a pass over a MIDI take
  group make takes in a take group, comped in the take lanes as audio is,
  comping.md), Merge (overdub: the notes join the clip under the pass, which grows
  to hold it; loop passes pile up) or Replace (the notes the clip had over the pass,
  and its events of the controllers the pass sends, are replaced).
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
- The demo song's drums show the pad names; with named rows only, eight rows.
