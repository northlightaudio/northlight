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

- **Corner**: 220×32, `surface0`; holds the "+" add-track button (ghost, left) and
  the row-height stepper (two tiny icon buttons).
- **R Ruler**: `size.ruler` 32 px, `surface0`. Two rows: 14 px markers row on top,
  18 px bars/beats below with the seconds readout in `caption` `textTertiary` under
  the bar numbers when zoomed out past 1 bar per 80 px.
- **H Track headers**: `size.trackHeaderWidth` 220 default, resizable 160 to 360,
  `surface0`.
- **L Lanes**: `canvas` background, one lane per track at the track's row height.
- **Overview strip** (**Decision**: deferred past Phase 7; keep the slot).

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

## Track header

Rows at `trackRow` 56 default. Contents, left to right, inside `space.sm` padding:

```
|▌ 01 Kick                                 [M][S][R][●] |
|   In 1 ▾    ▁▃▅▇ meter                   ─────o──── |
```

- 4 px track-colour stripe on the left edge (`track.headerStripeWidth`), full row
  height. Clicking it opens the colour swatch popup.
- Row 1 (28 px): track number `caption` `textTertiary`, name `label` `textPrimary`
  (double-click renames inline), then the control trio right aligned: M, S, R as
  24 px toggle buttons with their semantic colours, plus a monitor toggle (● icon)
  that cycles off/auto/on with the state in its tooltip: dim when off, `monitor`
  green while the input is heard (on, or auto with the track armed and the
  transport stopped or recording), `warning` yellow while auto is waiting.
- Row 2 (24 px, hidden below 44 px row height): input select (compact combo, "In 1",
  "In 1/2" for linked stereo, "No input"), an 8 px tall horizontal meter filling the
  middle, and a compact volume slider on the right (hidden below 200 px header width,
  the fader lives in the mixer anyway).
- At `trackRowLarge` 112 the header gains a third row with pan knob, output select and
  a sends summary ("2 sends").
- Selected track: header `laneSelected` fill and name `textPrimary`; the lane gets
  the same fill. Hover: `laneHover`.
- Armed: the R button is solid `arm`; the stripe pulses once (120 ms) when arming.
  Recording: the whole header's stripe becomes `record` and the input meter shows
  recording.
- Drag the number to reorder tracks; a 2 px `accent` insertion line shows the drop
  position. Right-click opens the track menu (rename, colour, duplicate, delete,
  freeze (later), add bus, show automation (Phase 9)).
- Buses appear in the same list after tracks, with a slate stripe and no input row;
  the master is always last, pinned, with a `textPrimary` stripe.

## Lanes and grid

- Lane background `canvas`; alternate lanes `laneAlt`; selected `laneSelected`;
  1 px `lineSubtle` under each lane.
- Grid: bar lines `gridBar`, beats `gridBeat`, sub-beats `gridSub` appear with the
  same thresholds as the ruler. Grid draws under clips.
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
  channels. Gain changes scale the envelope; muted clips use `waveformMutedAlpha`
  and a hatched body (45°, 6 px spacing, body colour at 10%).
- Fades: a `textPrimary` curve over the faded region with the body dimmed under it;
  handles are 8 px squares at the top corners, visible on hover, dragged horizontally.
  Crossfades show both curves.
- Selected: body at `clipBodySelectedAlpha`, outline track colour at 100%, header
  text `textPrimary`. Multi-selection identical per clip.
- Hover: outline brightens to 70%; the cursor changes at the edges (trim, 6 px
  zone), header (move), lower corners (fade).
- During a move drag: the original stays at 40% alpha, the ghost moves with the
  pointer, snapped; a 1 px `snapGuide` vertical line marks the snap target through
  all lanes; a tooltip shows the new start in bars.beats and the delta.
- Trim drag: the clip edge follows; the waveform does not move (source stays put);
  tooltip shows the new length. Alt trims the content inside the clip instead.
- Split: a thin `accent` line follows the pointer in split mode (S key or tool) and
  clicking splits at the snapped position.
- Recording clip: a growing clip in `record` at 22% body, with the live waveform
  appended from the recording peaks, header "● Recording".
- Takes: stacked clips on the lane, each with a badge; the top one is audible. Lanes
  do not expand in v1 (**Decision**: take lanes as rows are a post-v1 feature).

## FX tracks

- FX tracks (buses fed by sends) follow the tracks as rows of the same height,
  with the same header minus the input and arm (number, name, M, S, A, meter,
  volume), Teal by default. The lane has no clips; a `caption` in `textTertiary`
  says how many channels send there ("FX track: 3 channels send here").
- Their header menu: Rename, Show/Hide automation, Remove FX track.
- A track's menu has **Send to**: every bus and FX track, ticked where the track
  already sends (untick removes the send), and **New FX track** (Reverb, Delay,
  Empty) that adds one with a send from this track in one step.
- **+ ▸ FX track** in the corner adds one without sends.

## Automation

- The track header's **A** toggle (fifth of the row-one toggles, after monitor)
  shows the track's automation over its lane; the lane's clips dim under a
  `canvas` wash at 55%.
- Two chips at the lane's top left (18 px, `surface2`, radius `sm`, `caption`):
  the parameter ("Volume", "Pan", or "EQ: Band 2 gain"; a menu of Volume, Pan and
  each insert and strip module's parameters, those with a lane marked with a dot,
  plus Hide automation) and the channel's mode (Off `textTertiary`, Read
  `success`, Touch and Latch `warning`, Write `record`; the menu explains each).
- The lane: the curve in `automation` (1.5 px, filled at 12% beneath), points as
  8 px dots ringed in `canvas` (10 px and a `textPrimary` ring when hovered or
  dragged); `textTertiary` when the mode is Off. Without a lane the static value
  shows as a dashed line with "Click to add a point".
- Vertical scale: Volume from -60 dB at the bottom to +6 dB at the top (silence
  at the bottom edge), Pan from L100 to R100, a parameter over its range (log for
  frequencies and times).
- Click adds a point where clicked (drag to place it before release), drag a
  point to move it (snapped in time to the grid; Shift keeps its time; points keep
  their order), double-click removes a point, right-click offers Delete point,
  Clear lane and Hide automation. The value shows in a `surface3` tag while
  dragging. Each edit is one undo step.
- While automation plays, the mixer's fader, value field and pan knob, and the
  channel strip's controls, follow it.

## Selection

- Click selects a clip or track; Shift+click extends; Ctrl+click toggles; lasso from
  empty lane space draws a `selectionOutline` 1 px rectangle with `selectionFill`.
- Time selection: drag in the ruler's bars row with Shift, or in a lane with the
  range tool (R key), to select a time range across the selected tracks, shown as
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

- 4-track screenshot at 100% matches: ruler 32 px, headers 220 px, rows 56 px, clip
  header 18 px, colours by alpha from the token file.
- 64-track screenshot at 3840×2160 150% renders in under 16 ms per frame while
  scrolling and with the playhead moving (section 11 method).
- Every drag state (move, trim, fade, lasso, time selection, file drop) has been
  screenshotted mid-drag and matches the descriptions, including the snap guide and
  tooltip.
- Clip, header and lane hover and selected states are each visibly distinct at a
  glance.
