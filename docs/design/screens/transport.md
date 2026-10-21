# 6. Transport, time display, status

## Purpose and anatomy

The transport cluster is the centre of the header bar. It is the one place that is
always visible and always truthful about what the engine is doing.

```
 ╭─────────────╮   ╭──────────────────────────────────────────╮   ╭───────────────────────────╮
 │ |◀  ▶  ■  ● │   │ 5.3.240   0:12.480   120.00 BPM   4/4 ⌄  │ ▪ │ loop  punch  click  count │
 ╰─────────────╯   ╰──────────────────────────────────────────╯   ╰───────────────────────────╯
                                  ↑ centred in the window
```

Every control in the bar is `controlLarge` (32 px) tall. The bar is `surface1`;
each cluster sits in a tray recessed into it: `surface0`, radius `md`, the groove's
inset along the top and a 1 px `lineSubtle` rim, `space.xs` padding, `space.xxs`
between the controls (`space.xs` between the transport keys), `space.md` between
trays. The bar has a `lineSubtle` rule along its bottom. The same trays
hold the left cluster (project, undo, redo) and the right ones (snap, ripple,
zoom; the panel toggles). In the centre, the readout is centred in the window, with
the transport buttons on its left and the mode toggles on its right, each
`space.sm` + `meterWidth` + `space.sm` away (the gap on the right holds the master
clip indicator). When the window is too narrow for both flanks to clear the side
clusters, the readout moves off centre just enough; when even that does not fit,
the bar gives way in steps, so nothing overlaps down to the window's 1280 px
minimum: first the clock time leaves the readout (bars, tempo and signature stay),
then the project name shortens to 96 px. The trays and the readout well are
recessed with a soft shade fading down 5 px from their top edge (`canvas` at 55%
to clear), not the fader groove's hard inset. The parts:

1. **Transport buttons** (32 px, `controlLarge`): Go to start, Play, Stop, Record.
   Keys in the control material (section 2.4): a `surface2` face at rest, 16 px
   icons (half the key) in `textSecondary`; the active one glows. While playing, Play is solid
   `accent` with a `textOnColour` icon. Play toggles to Pause while playing (**Decision**:
   Space is play/stop, not play/pause; Pause exists for Shift+Space). Record is a
   `record`-coloured icon at rest; while recording the button is solid `record` with
   `textOnColour` and the time display turns `record`.
2. **Mode toggles** (28×28 icon toggles, 8 px apart, each with a tooltip naming
   the mode, its state and shortcut): Loop (two arrows cycling), Punch (in/out
   brackets around a dot; on state uses `record` instead of `accent`), Click
   (metronome), Count-in (metronome with the pendulum the other way and the bar
   count as a 9 px figure in the corner; right-click or long-press for 0/1/2).
3. **Readout**: one readout well (the material's `readoutWell`, radius `md`, the
   groove's inset, a `lineSubtle` rim) holding bars, time, tempo and signature
   with no dividers, `space.sm` apart and told apart by size and colour: the
   primary in `timeLarge` 22 px mono and the material's `readoutText` (Overcast),
   the secondary time in `timeSmall` `textSecondary`, tempo and signature in
   `timeSmall` `textPrimary`. In order: the secondary time
   (h:mm:ss.mmm, or samples, or timecode via right-click), the tempo with a
   `caption` "BPM", the signature with a small chevron. Click a field to
   type a position; drag vertically on a segment to nudge. The primary shows the
   heard position (delay-compensated).
4. **Tempo and signature** (in the readout): tempo drags, types, and taps (T while
   focused); the signature opens its list on click.
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

- Cluster screenshot matches: every control 32 px in 40 px trays, one readout
  well, 22 px mono primary time, exact token colours per state.
- Recording state screenshot: solid record button, red time text, "●" title prefix,
  disk readout in the status bar.
- Count-in screenshot shows negative bars in `warning`.
- Keyboard map verified end to end from the arrange view with nothing focused.
