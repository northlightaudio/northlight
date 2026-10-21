# 14. Take lanes and comping

Loop recording and punching leave several takes of the same part. Comping is
choosing, phrase by phrase, which take plays: the "punch and comp fast and
obvious" promise of the brief (01-brief.md). This note covers the model, the
recording rules and the arrange view's take lanes, Logic / Cubase / Studio One
style. It supersedes 04-arrange.md's v1 decision that takes only stack.

## Model

- A **take group** on a track holds the **takes** recorded over a range: each
  take is the pass as recorded (an audio clip that is not on the timeline itself)
  and a number ("T3"), never reused within the group.
- The **comp** is a list of segments, each a time range and the take that plays
  there. Segments are sorted, contiguous and cover the union of the takes; a
  segment plays only where its take has audio.
- What plays on the track's main lane is the comp's result, **materialised as
  ordinary clips** (one per segment, on the take's media with the matching source
  offset). The group remembers which clips are its comp.
  - Why: the engine, bounce, peaks, the inspector and every clip command already
    work on clips; a comp that is clips needs no change in any of them, and the
    risk of the feature stays in the model and the arrange view.
  - The cost: the comp's clips can be edited like any clip (moved, trimmed,
    gain), but the next comp edit rebuilds them from the takes. Flatten first to
    keep hand edits as the new starting point.
- **Crossfades**: at a boundary between two takes the clips overlap by the
  group's crossfade, 10 ms by default, centred on the boundary, with matching
  linear fades (constant sum, the existing crossfade mechanism). A crossfade is
  at most half of either segment; where a take does not reach across the
  boundary the splice is butt-joined.
- **Flatten** drops the group and leaves its clips as ordinary clips.
- **Move take to new track** makes an audio track under this one with the take
  as its clip and deletes the take from the group.
- Undo covers every comp edit; each is one step.

## Recording

- A loop recording's passes become takes in one group, newest last; the newest
  pass plays over its own range (so a last pass stopped half way plays where it
  has audio and the take before it plays after that).
- A punch recording's pass is a take covering the punch range.
- A pass that lands on an existing take group's range joins it as its newest take
  and plays over its range.
- A single pass anywhere else is a plain clip, as before. Clips already on the
  track are not folded into a group: a pass over an ordinary clip overlaps it.
- MIDI loop recording keeps its clip per pass (MIDI take lanes are not built).

## Arrange view

```
+-----------+----------------------------------------------------------+
| 01 Vocal  | [Take 3  T3][Take 1  T1][Take 2  T2][Take 3  T3]   main  |
|  ...      | [v 3 takes]                                               |
+-----------+----------------------------------------------------------+
| T3 Take 3 | ######...............................#######   lanes,    |
| T2 Take 2 | .....................#######.................  newest    |
| T1 Take 1 | .........#######.............................  on top    |
+-----------+----------------------------------------------------------+
```

- **Main lane**: the comp's clips draw like any clip, with a take badge ("T2",
  02-design-system.md Badge) at the right of each header. A comp boundary shows
  as a 1 px `textPrimary` line at 35% through the clip bodies; hovered or dragged
  it is a 2 px `accent` line and the cursor is the left-right resize. Below the
  clip header strip a boundary drags (4 px either side), clamped to its
  neighbours; dragged across a segment it removes it. The header strip keeps the
  clip's own fade handles and move.
- **Disclosure**: a chip at the group's lower left on the main lane (`surface2`
  at 90%, radius `sm`, chevron icon and "3 takes" in `caption`) shows or hides
  the lanes. Also: the track menu's Show/Hide take lanes, Alt+T, and the
  comp's right-click menu. The chip hides below a 24 px row.
- **Take lanes**: `takeLane` (40 px) each, under the track's row, newest take on
  top, on `surface0` with a `lineSubtle` rule. A take draws as a clip-shaped
  block in the track colour: dim (half the body alpha, waveform at
  `waveformMutedAlpha`) where the comp does not use it, lit (the selected body
  alpha, a 1 px outline in the track colour, waveform at `waveformAlpha`) where
  it does. Its badge and name sit at its left, kept in view.
- **Lane headers**, in the header column under the track header: a 4 px stripe
  of the track colour at 40%, the badge, the name in `label`, and "Comp" in
  `accent` at the right when the take is the whole comp. A take not in the comp
  has a dim badge and `textSecondary` name.
- **Swipe**: drag across a take's lane to make that take play over the dragged
  range (snapped to the grid when snap is on). While dragging, the range shows in
  `selectionFill` with a `selectionOutline` rim. A click on a lane (no drag)
  puts that take in the comp segment under the pointer. A click on a lane's
  header makes that take the whole comp.
- **Menus**: right-click a lane or its header: Use whole take, Rename take...
  (an inline field over the header; Enter commits, Escape or focus loss cancels),
  Move take to new track, Delete take, Flatten comp, Hide take lanes. Right-click
  the comp on the main lane: Show/Hide take lanes, Use take (each take, the whole
  comp's ticked), Flatten comp.
- **Keyboard** (selected track, at the playhead): Alt+Up plays the next newer
  take in the comp segment there, Alt+Down the next older (wrapping, skipping
  takes with no audio there); Alt+T shows or hides the track's take lanes.
- Rows: the track's row height (and its resize edge, under the main lane) is
  unchanged; the lanes add to the row's extent, so the tracks below move down.

## Not in this pass

- MIDI take lanes (MIDI loop recording keeps a clip per pass).
- Moving a whole take group by dragging its comp (each comp clip moves alone; a
  ripple shift moves the whole group).
- Turning existing overlapping clips into a take group.
- A per-group crossfade length control (the model has it; there is no UI).
