# 4. Arrange view

## Purpose and anatomy

Where the recording and the arrangement live. It must stay legible with 4 tracks or
120, and every edit must be visible as it happens.

```
+-----------+------------------------------------------------------------+
| corner    | R  Ruler: markers row · bars/beats · time                   | 32
+-----------+------------------------------------------------------------+
| H  Track  | L  Lanes                                                   |
|   headers |      clips, waveforms, playhead, loop/punch, grid           |
|   220 px  |                                                            |
+-----------+------------------------------------------------------------+
|           | overview strip (optional, 24 px)                           |
+-----------+------------------------------------------------------------+
```

- **Corner**: 220×32, `surface0`; holds the "+" add-track button (ghost, left), the
  tools after it (select, range, pencil, erase, split, glue, mute; the current one
  lit; keys 1 to 7) and the row-height stepper (two tiny icon buttons). When the
  headers are too narrow for all seven, one button shows the current tool with a
  chevron and opens a menu of them (icon, name, key; the current one ticked). The
  piano roll's tools are the same buttons, icons and cursors (select, pencil,
  erase).
- **Pencil**: on a MIDI track's empty space a drag draws a new empty clip in whole
  grid cells from the grid line at the press (either direction), previewed in the
  track's colour; a click draws a bar. The new clip is selected (double-click opens
  it in the piano roll). On a clip the pencil acts as the select tool; on an audio
  track it draws nothing. With any tool, a double-click on a MIDI track's empty
  space makes a bar-long clip on the bar there.
- **Erase** (4, as in the piano roll): a click on a clip deletes it; a drag sweeps,
  and each clip it passes over dims to 40% until the release removes them all as one
  undo step. A comp's clips go together, and ripple (Shift+N) applies as with Delete. The
  clip under the eraser shows a 1 px `danger` outline.
- **Split** (5): a 1 px `accent` line runs through the clip under the pointer at the
  grid line nearest it (at the pointer when no grid line falls inside the clip),
  with the position in a tag ("Split at 5.1.000"). A click cuts there. Shift runs
  the line through every lane and the click cuts every track ("Split all at");
  Alt takes the line off the grid. On empty space a click locates and a drag
  lassos, as with the select tool.
- **Glue** (6): over a clip that can join the next one, a 1 px `accent` outline
  round both shows the clip they will make, with each seam marked 2 px; a click
  joins them. Audio joins when the next clip continues the same recording (a split
  undone); a MIDI clip joins the next one across any gap, taking its notes. Over a
  clip that cannot, a tag beside it says why: "Joins only a clip that continues
  this one" or "No clip after this one to join". Muted and playing clips do not
  join each other.
- **Mute** (7): a click mutes a playing clip or unmutes a muted one; a drag sweeps,
  giving each clip it passes what the first one got, shown as it goes; one undo
  step. **Ctrl+M** mutes the selected clips with any tool (unmutes them when all are
  muted).
- With erase, split, glue and mute, a press anywhere on a clip uses the tool: edges
  and corners do not trim or fade, and the cursor is the tool's icon over the whole
  lane. A press on a clip of a larger selection acts on the selection.
- **R Ruler**: `size.ruler` 32 px, `surface0`. Two rows: 14 px markers row on top,
  18 px bars/beats below with the seconds readout in `caption` `textTertiary` under
  the bar numbers when zoomed out past 1 bar per 80 px.
- **H Track headers**: `size.trackHeaderWidth` 260 default, resizable 160 to 360,
  `surface0`.
- **L Lanes**: `canvas` background, one lane per track at the track's row height.
  Each track can have its own height: drag the bottom edge of its header or lane
  (an up-down cursor shows there), 28 to 240 px; double-click the edge to return to
  the default the corner stepper sets. The height is saved with the project and
  the resize is undoable.
- **Overview strip** (**Decision**: deferred; keep the slot).

## Ruler

- Bar lines: every bar gets a 1 px `gridBar` tick full height and its number in
  `caption` 500 `rulerText`, left-aligned 4 px after the tick. Beat ticks 6 px,
  `gridBeat`. Sub-beats appear only when a beat is wider than 48 px.
- Loop region: `loopFill` band across the bars row with 2 px `loopEdge` ends; the ends
  are drag handles (8 px hit); dragging inside the band moves it; a double-click on
  the ruler's bars row sets the loop to the clip or region under the pointer.
- Punch region: same pattern with `punchFill`/`punchEdge` in the markers row's lower
  half, so loop and punch can overlap without ambiguity.
- Markers: a 10 px flag (triangle plus stem) in `markerFlag` with the name in
  `caption` to its right, clipped at the next marker; regions as a `regionFill` span
  with the name at its start. Selected marker: `accent` flag and name. Hover: name
  turns `textPrimary`. Drag moves on the grid; double-click renames inline (a text
  field over the name, Enter/Esc).
- Playhead: 1 px `playhead` line across ruler and lanes with a 7 px downward triangle
  head in the ruler. `playheadRecording` while recording. Click in the ruler locates;
  drag scrubs the position (no audio scrub in v1).
- Zoom: Ctrl+wheel over ruler or lanes zooms around the pointer; drag the ruler
  vertically to zoom (Logic style); "Z" zooms to fit, Shift+Z to the selection.

## Folder tracks and frozen tracks

- A folder track's header shows its icon, a disclosure (▾ open, ▸ closed) before
  its name, and M and S, which act on every track it holds. Its lane reads "5
  tracks" in `caption` `textTertiary` over its tracks' clips drawn as thin bars,
  one row per track (at most eight), in each track's colour (30% when muted).
- Tracks inside a folder indent their header 12 px per level. A closed folder's
  tracks take no height.
- Add a folder from the "+" menu (it takes the selected track) or a track's menu
  (Folder: a new folder holding it, or move it into a folder or out of one).
- A frozen track's name carries a snowflake and its lane is veiled (canvas at 45%)
  with "❄ FROZEN" in `captionCaps` `info`. Freeze and unfreeze from the track's
  menu; the status bar shows "Freezing 40%" while the render runs and a toast says
  when it is done.

## Tempo track

A 52 px lane under the ruler, shown from the ruler's menu ("Show tempo track"; kept
per user). The header column reads "TEMPO" in `captionCaps` `textTertiary` and the
tempo and signature at the playhead in `caption` `textSecondary`.

- The tempo as a curve in `automation` (1.5 px) over a 12% fill, flat between
  changes and gliding over ramps; the lane's vertical range follows the tempos in
  use with 30% headroom.
- A handle (4 px radius, `automation`, `selectionOutline` when selected) at the
  song's start and at each change, with its tempo in `caption` `rulerText` beside
  it where there is room ("90 ramp" for a change ramped into).
- Time signatures as `markerFlag` tags along the lane's top edge at their bars.
- Drag a handle up or down to set its tempo (Shift: a tenth as fast); drag a
  change sideways to move it, snapped to the arrange grid as clips are (G turns
  snap off, Alt for the drag), counted on the tempo before it. Double-click the lane to add a change; double-click a handle to type a
  tempo. Right-click: a handle (set tempo, ramp from the previous tempo, delete),
  the lane (add a tempo change, a signature from the bar under the pointer, hide
  the lane) or a signature tag (change or remove it). Delete removes the
  selected change.
- **Decision:** tempo and signature changes sit at musical positions (quarter notes
  and bars), and a tempo edit keeps MIDI clips and notes, markers, automation and
  audio clips that follow the tempo on their bars and beats. Other audio clips stay
  at their time, as recorded audio should.

## Track header

Rows at `trackRow` 56 default. Contents, left to right, inside `space.sm` padding:

```
|▌ ∿  Kick                                 [M][S][R][●] |
|   In 1 ▾    ▁▃▅▇ meter                   ─────o──── |
```

- 4 px track-colour stripe on the left edge (`track.headerStripeWidth`), full row
  height. Clicking it opens the colour swatch popup: each palette colour by name
  with its swatch, the current one ticked, then Default. The track menu's and the
  mixer strip's Colour submenus are the same menu.
- Row 1 (28 px): the track-kind icon (14 px, `textTertiary`, `textSecondary` when
  selected: audio, MIDI, instrument channel, instrument output, folder; its tooltip
  gives the track number and kind), name `label` `textPrimary`
  (double-click renames inline), then the control trio right aligned: M, S, R as
  24 px toggle buttons with their semantic colours, plus a monitor toggle (● icon)
  that cycles off/auto/on with the state in its tooltip: dim when off, `monitor`
  green while the input is heard (on, or auto with the track armed and the
  transport stopped or recording), `warning` yellow while auto is waiting.
- Row 2 (24 px, hidden below 44 px row height): input select (compact combo: "No
  input", then the stereo inputs from Audio Setup by name under a "Stereo"
  heading, then each enabled channel as "In 3" (its device channel number) under
  "Mono"; the tooltip of a stereo choice names its channels, "Vocals (In 1 + In 2)";
  a stereo input the device does not have shows as "Vocals (missing)" with a
  tooltip pointing to Audio Setup), an 8 px tall horizontal meter filling the
  middle, and a compact volume slider on the right (hidden below 200 px header
  width).
- At `trackRowLarge` 112 the header gains a third row with pan knob, output select and
  a sends summary ("2 sends").
- Selected track: header `laneSelected` fill and name `textPrimary`; the lane gets
  the same fill. Hover: `laneHover`.
- Armed: the R button is solid `arm`; the stripe pulses once (120 ms) when arming.
  Recording: the whole header's stripe becomes `record` and the input meter shows
  recording.
- Drag the icon to reorder tracks; a 2 px `accent` insertion line shows the drop
  position. Right-click opens the track menu (rename, colour, duplicate, delete,
  freeze, add bus, show automation lanes).
- Buses appear in the same list after tracks, with no input row, in their colour
  (FX tracks Lichen and group buses Overcast unless coloured); the master is always
  last, pinned, in Slate. The mixer and the automation lanes use the same colours.

## Lanes and grid

- Lane background `canvas`; alternate lanes `laneAlt`; selected `laneSelected`;
  1 px `lineSubtle` under each lane.
- Grid: bar lines `gridBar`, beats `gridBeat`, sub-beats `gridSub` appear with the
  same thresholds as the ruler (a beat from 12 px, halves from 48 px, quarters from
  192 px). Grid draws under clips. The piano roll and the audio editor draw the
  same grid and ruler.
- Snapping follows the tempo track: the grid's beat and its divisions are counted
  in the signature of the bar where the snapped position falls, in every view
  (the piano roll, the automation lanes and the tempo track too).
- Loop and punch regions extend through the lanes as `loopFill`/`punchFill` bands at
  half the ruler alpha.
- Drop target: dragging files over a lane highlights the lane `dropTarget` and shows a
  ghost clip outline at the snapped drop position with the file name; dropping below
  the last lane creates a track per file.

## Clips

```
+-- header 18 px (track colour 55%) ------------------------+
| 01 Kick                                              ◢    |
+-----------------------------------------------------------+
|  waveform (track colour 90%) on body (track colour 22%)   |
|  fade handles ▲ at top corners when hovered               |
+-----------------------------------------------------------+
```

- Body: track colour at `clipBodyAlpha` over the lane; radius `sm`; 1 px inset
  outline of the track colour at 40%.
- Header: `clipHeader` 18 px, track colour at `clipHeaderAlpha`, name in `caption`
  500 `textPrimary` with 6 px padding, truncated with an ellipsis; a take badge
  ("T2") on the right for takes; the header hides below 24 px row height.
- Waveform: the peak-cache mip nearest to 1 px per bucket, drawn as a filled min/max
  envelope in track colour at `waveformAlpha`, centred vertically in the body, with a
  1 px darker centre line (track colour at 30%). Stereo shows two half-height
  channels. Gain changes scale the envelope (a boost up to full scale, as in the
  audio editor and the take lanes); muted clips use `waveformMutedAlpha`
  and a hatched body (45°, 6 px spacing, body colour at 10%).
- Fades: a `textPrimary` curve over the faded region with the body above it dimmed;
  handles are 8 px squares at the top corners, visible on hover. Dragging a handle
  sideways sets the fade's length and up or down its curve: up bows it up (fast,
  full level sooner), down bows it down (slow), 80 px to either end, with a 4 px
  detent at linear (the default). The curve is drawn as it plays, and the waveform
  under a fade is scaled by it, so a faded edge looks as quiet as it sounds (the
  audio editor's waveform already was); the tooltip gives
  both ("Fade 0.50 s  fast 40%", or "linear"). One undo step. Crossfades show both
  curves.
- Selected: body at `clipBodySelectedAlpha`, outline track colour at 100%, header
  text `textPrimary`. Multi-selection identical per clip.
- Hover: outline brightens to 70%; the cursor changes at the edges (trim, 6 px
  zone), header (move), lower corners (fade).
- During a move drag: the original stays at 40% alpha, the ghost moves with the
  pointer, snapped; a 1 px `snapGuide` vertical line marks the snap target through
  all lanes; a tooltip shows the new start in bars.beats and the delta.
- Trim drag: the clip edge follows; the waveform does not move (source stays put);
  tooltip shows the new length.
- Stretch drag: Alt+drag of the right edge stretches the clip to the new end
  (snapped to the grid); the waveform stretches with it and the tooltip shows the
  playing speed. The header shows a stretched clip's speed ("66.7%"), its
  transposition ("+3.0 st"), a note for a clip that follows the tempo, and
  "stretching 40%" while its render is not finished. Right-click a clip for
  "Stretch to project tempo...", "Follow tempo changes", Transpose and "Reset
  stretch and pitch".
- Split: the split tool (above), or **S** at the playhead and **Alt+S** at the time
  selection, for the selected clips (every clip with none selected).
- Muted clips: a clip muted on its own draws as the clips of a muted track do: the
  hatched body, the waveform at `waveformMutedAlpha`, MIDI notes in `textDisabled`.
  It stays on the timeline and editable, saved with the project, and plays nothing.
- Clip menu (right-click a clip; it is selected first if it was not): the clip's
  name ("+2" when more are selected), then Split here (at the snapped position under
  the click), Join with next (Join for a selection), Crossfade (audio), Mute clip or
  Unmute clip, Duplicate, Normalise (audio); Cut, Copy, Delete; then for audio the
  stretch and pitch items and Open in Sampler, for MIDI Open in piano roll; and
  Track.... Each item shows its key; an item that would do nothing is disabled. The
  edits act on the selection.
- Recording clip: a growing clip in `record` at 22% body, with the live waveform
  appended from the recording peaks, header "● Recording".
- Takes: a take group's comp plays on the lane as clips with a take badge each; the
  takes expand into lanes under the row for swipe comping (screens/comping.md).
  Stacked clips from older projects stay stacked, the top one audible.

## Buses and the master

- Every bus follows the tracks as a row of the same height, in project order:
  group buses (tracks route their output there) and FX tracks (fed by sends). The
  header is a track's minus the input and arm (number, name, M, S, A, meter,
  volume); FX tracks are Lichen and group buses Overcast unless coloured. The lane
  has no clips; a `caption` in `textTertiary` says who feeds it ("FX track: 3
  channels send here", "Bus: 2 channels route here").
- The master is the last row: name, A, meter and volume (the master fader), in
  Slate, with no M, S, pan or rename; its lane reads "Master: the whole mix, after
  every track and bus". Its lanes are Volume and its inserts' parameters (it has
  no pan). Its header menu: Show/Hide automation lanes.
- A bus's header menu: Rename, Show/Hide automation lanes, Remove FX track (or
  Remove bus).
- So every channel that makes sound can be automated from the arrange view.
- A track's menu has **Send to**: every bus and FX track, ticked where the track
  already sends (untick removes the send), and **New FX track** (Reverb, Delay,
  Empty) that adds one with a send from this track in one step.
- **+ ▸ FX track** in the corner adds one without sends.

## Automation

A channel's automation shows as lanes of their own under its row (and under its
take lanes), one lane per automated parameter. Tracks that make sound, instrument
channels, buses and the master have them; MIDI tracks and folders do not. A closed folder
hides its tracks' lanes too.

```
|▌ ∿  Vocals                          [M][S][R][●][A]³|  clips
|▏  ▾ Volume ▾               −3.8 dB  👁 |  ●───●     ●──  (48 px)
|▏    −60 … +6 dB                        |
|▏  ▸ Pan                         L12 👁 |  ─────╲__╱──   (20 px, collapsed)
|▏    + Add lane              1 hidden ▾ |                 (20 px)
```

- **The A toggle** (fifth of the row-one toggles, after monitor) opens and closes
  all of the channel's lanes. Its letter takes the mode's colour, open or closed:
  Off `textTertiary`, Read `success`, Touch and Latch `warning`, Write `record`;
  open, it is a toggle on in that colour. A badge at its top right (14 px pill,
  `surface3`, `caption` `textPrimary`, ringed in `canvas`) counts the lanes, hidden
  ones included, so closed lanes are never forgotten. Right-click: the mode (each
  with a line on what it does), Show or Hide lanes, Show the hidden lanes, Expand
  all, Collapse all, and Add lane (the parameter menu). The **A** key opens or
  closes the selected track's lanes; the track menu has Show/Hide automation lanes.
  Opening and closing is saved with the project and is one undo step.
- **A lane's header** sits in the header column, `surface0` (a step below a track
  header), with the track's stripe carried down at 38% and a `lineSubtle` line under
  it. Its top line (24 px): a chevron (collapse or expand), the parameter's name in
  `small` `textSecondary` with a small chevron (click: the parameter menu), the
  value at the playhead in `value` `textSecondary` (right aligned, repainted only
  when it changes), and an eye (hide). The chevron and eye are 14 px icons in
  `textTertiary` that turn `textPrimary` on a `surface2` rounded square when
  hovered; the name turns `textPrimary`. At 44 px and taller a second line gives
  the lane's range in `caption` `textTertiary` ("−60 … +6 dB", "L100 … R100", or the
  parameter's minimum and maximum).
- **The lane** to the right: `surface0` under the grid. The curve in the track's
  colour (1.5 px, filled at 12% beneath), so each lane reads as its track's;
  points as 8 px dots ringed in `canvas` (10 px and a `textPrimary` ring when
  hovered or dragged); `textTertiary` when the mode is Off. A point at the song's start is drawn whole, inside the lane's edge, and
  is as easy to grab as any other. Volume draws a dashed `lineStrong` line at 0 dB
  and Pan one at the centre. A lane with no points shows the static value as a
  dashed line with "Click to add a point"; it plays nothing.
- **Heights:** a new lane is `automationLane` (48 px); drag a lane's bottom edge
  (the bottom 4 px, an up-down cursor) to size it between `trackRowMin` and
  `trackRowMax`. The corner stepper sizes track rows only.
- **Collapse** (the chevron, or the lane's menu) turns a lane into a
  `automationLaneCollapsed` (20 px) strip: the header in `caption` `textTertiary`,
  the curve at 75% without fill or points. It is read-only; a click on the strip
  expands it. **Hide** (the eye) takes the lane out of the view. Neither changes
  what plays, both are saved with the project and each is one undo step.
- **The footer row** (20 px) under a channel's open lanes: "+ Add lane" (the
  parameter menu) at the left and, when lanes are hidden, "2 hidden ▾" at the
  right (a menu: Show each, Show all). Both are `caption` text buttons with a
  `surface2` hover.
- **The parameter menu** lists Volume, Pan and each insert and strip module's
  parameters, a dot marking those with a lane. From a lane's name it is headed
  "This lane shows", ticks the lane's parameter and continues with Delete point
  (when right-clicked on a point), Collapse or Expand lane, Hide lane, Clear points
  and Delete lane. Choosing another parameter changes an empty lane's parameter;
  for a lane with points it shows that parameter's lane, or adds one.
- **Editing:** click adds a point where clicked (drag to place it before release),
  drag a point to move it (snapped in time to the grid; Shift keeps its time;
  points keep their order), double-click removes a point. The value shows in a
  `surface3` tag beside the point while dragging. Each edit is one undo step.
  Removing a lane's last point keeps the lane; Delete lane removes it and its
  points (undoable).
- **Writing:** in Touch, Latch and Write, a control moved with no lane for it gets
  one when the pass ends, and the channel's lanes open; writing into a hidden or
  collapsed lane shows it again.
- The track's row is not covered or dimmed: its clips stay editable while lanes are
  open.
- While automation plays, the mixer's fader, value field and pan knob, and the
  channel strip's controls, follow it.
- **Decision:** lanes are rows under the track, not drawn over it (see
  decisions.md, 2026-10-25).

## Selection

- Click selects a clip or track; Shift+click extends; Ctrl+click toggles; lasso from
  empty lane space draws a `selectionOutline` 1 px rectangle with `selectionFill`.
- Time selection: drag in the ruler's bars row with Shift, or in a lane with the
  range tool (key 2), to select a time range across the selected tracks, shown as
  `selectionFill` through those lanes. Used by split-at-range, bounce range, loop
  from selection.
- Esc clears selection.

## Scrolling and zoom

- Horizontal: wheel; vertical: Shift+wheel; both via overlay scrollbars. The view
  follows the playhead during playback ("page" scrolling: jumps by a screen when the
  playhead reaches 90% of the width), toggleable in the ruler's right-click menu.
- Zoom range: from 1 px per 256 samples to 1 px per 8 bars; zoom steps are smooth
  (no discrete levels) and the mip selection follows.

## Empty and error states

- No tracks: the shell empty state (section 3).
- Missing media: the clip body is `surface2` with a `warning` outline, waveform
  replaced by "Missing: file.wav" in `caption` and a "Locate…" link; the track header
  shows a `warning` dot.
- Recovery: a toast after launch when partial recordings exist, with "Recover" and
  "Set aside" actions, not a dialog.

## Acceptance

- At 100%: ruler 32 px, headers 220 px, rows 56 px, clip header 18 px, colours by
  alpha from the token file.
- 64 tracks at 4K and 150% draw in under 16 ms per frame while scrolling with the
  playhead moving (section 11 method).
- Every drag state (move, trim, fade, lasso, time selection, file drop) matches its
  description mid-drag, including the snap guide and tooltip.
- Each tool's hover and sweep state (the split line and its tag, with Shift too; the
  glue outline or its reason; the eraser's outline and swept clips; the mute sweep)
  matches its description, and every tool fits the corner at the default header
  width.
- Clip, header and lane hover and selected states are each distinct at a glance.
