# 6. Transport, time display, status

## Purpose and anatomy

The transport cluster is the centre of the header bar. It is the one place that is
always visible and always truthful about what the engine is doing.

```
 [|◀] [▶] [■] [●]  ·  ⟲ loop  punch  click  count-in  ·  ┌ 5.3.240 ┐ ┌ 0:12.480 ┐  ·  120.00  4/4
                                                          └ bars    ┘ └ time    ┘
```

Left to right, `space.md` gaps inside groups and `space.xl` between groups:

1. **Transport buttons** (32 px, `controlLarge`): Go to start, Play, Stop, Record.
   Icon buttons with 20 px icons. Play toggles to Pause while playing (**Decision**:
   Space is play/stop, not play/pause; Pause exists for Shift+Space). Record is a
   `record`-coloured icon at rest; while recording the button is solid `record` with
   `textOnColour` and the time display turns `record`.
2. **Mode toggles** (28 px toggle buttons with icon and short label): Loop, Punch,
   Click, Count-in (label shows the bar count, "Count-in 1"; chevron menu for 0/1/2).
3. **Time displays**: two `surface2` fields with radius `sm`, 28 px, `timeLarge`
   22 px mono for the primary (bars.beats.ticks) and `timeSmall` 13 px mono for the
   secondary (h:mm:ss.mmm, or samples, or timecode via right-click). Click a field to
   type a position; drag vertically on a segment to nudge. The primary shows the
   heard position (delay-compensated).
4. **Tempo and signature**: numeric field "120.00" (drag, type; tap tempo via T key
   while focused) and a "4/4" combo. Both `value` mono.
5. **Master clip indicator**: a 6×6 square at the far right of the cluster, hidden
   until the master clips, then `meterClip` until clicked.

## States

- Stopped: Play `textSecondary`, Stop `textPrimary`.
- Playing: Play `accent` (active), Stop `textSecondary`.
- Recording: Record solid `record`; time display text `record`; the status bar shows
  the disk readout; the window title gets a "●" prefix.
- Count-in: the time display shows "-1.4.000" style negative bars in `warning` and
  the Click toggle pulses `accent` on each beat (120 ms).
- Loop on: the Loop toggle in its on state and the ruler region visible; when the
  loop region is empty the toggle is disabled with a tooltip "Set a loop region in
  the ruler".
- No device: Play and Record disabled; a `warning` dot on the Record button with the
  tooltip "No audio device"; clicking opens Audio Setup.

## Keyboard

Space play/stop, Shift+Space pause, R record (with confirmation if no track is
armed: a toast "Arm a track to record" with an "Arm selected" action), Enter go to
start, L loop, P punch, C click, Home/End, comma/full stop previous/next marker,
numpad as in Pro Tools (0 play, 3 record, 1 go to start). Full map in section 10.

## Diagnostics panel (dock tab)

A table of engine health for the user who wants to know: callback time p50/p99/max
as sparklines over the last 60 s, dropouts with timestamps, disk read-ahead fill,
plugin latencies per track, worker thread count, device state. All in `small` and
`value` mono; `warning`/`danger` colouring at the same thresholds as the status
bar. A "Copy report" ghost button copies a text summary for bug reports.

## Acceptance

- Cluster screenshot matches: 32 px transport buttons, 28 px toggles and fields,
  22 px mono primary time, exact token colours per state.
- Recording state screenshot: solid record button, red time text, "●" title prefix,
  disk readout in the status bar.
- Count-in screenshot shows negative bars in `warning`.
- Keyboard map verified end to end from the arrange view with nothing focused.
