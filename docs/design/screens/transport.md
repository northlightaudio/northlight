# 6. Transport, time display, status

## Purpose and anatomy

The transport cluster is the centre of the header bar: always visible, and always
true to what the engine is doing.

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
trays. The readout well spaces its text instead (part 3). The bar has a `lineSubtle` rule along its bottom. The same trays hold the
left cluster (project, undo, redo) and the right ones (snap, ripple, zoom; the panel
toggles). The readout is centred in the window, with the transport buttons on its
left and the mode toggles on its right, each `space.sm` + `meterWidth` + `space.sm`
away (the right gap holds the master clip indicator). When the flanks would hit the
side clusters, the readout moves off centre just enough; if that is not enough, the
bar gives way in steps so nothing overlaps down to the 1280 px minimum: first the
clock time leaves the readout, then the project title shortens to 104 px. Trays and
the readout well are recessed with a soft shade fading down 5 px from their top edge
(`canvas` at 55% to clear), not the fader groove's hard inset. The parts:

1. **Transport buttons** (32 px, `controlLarge`): Go to start, Play, Stop, Record.
   Keys in the control material (section 2.4): a `surface2` face at rest, 16 px
   icons in `textSecondary`; the active one glows. While playing, Play is solid
   `accent` with a `textOnColour` icon. Play toggles to Pause while playing: its icon
   is the pause bars and its tooltip "Pause  Shift+Space"; a click pauses, keeping
   the position, and the next Play resumes from it. Stop ("Stop  Space") stops and,
   with **Return on stop** on (the default, Preferences > Recording > Transport),
   takes the playhead back to where playback started (after a pause, where the
   first Play started; after a locate while playing, where it jumped to); Stop
   after a pause does the same. **Decision**: Space is play/stop, not play/pause;
   Pause is Shift+Space and the Play button while playing. Return on stop has no
   header toggle: the header has no room in the mode tray that would not crowd the
   count-in, and it is a set-once preference. Record is a
   `record`-coloured icon at rest; while recording the button is solid `record` with
   `textOnColour` and the time display turns `record`.
2. **Mode toggles** (28×28 icon toggles, 8 px apart, each with a tooltip naming
   the mode, its state and shortcut): Loop (two arrows cycling), Punch (in/out
   brackets around a dot; on state uses `record` instead of `accent`), Click
   (metronome), Count-in (metronome with the pendulum the other way and the bar
   count as a 9 px figure in the corner; a click steps Off, 1, 2, 4 bars, the
   choices Preferences offers, and its tooltip names the count).
3. **Readout**: one readout well (the material's `readoutWell`, radius `md`, the
   groove's inset, a `lineSubtle` rim) holding bars, time, tempo and signature
   with no dividers, told apart by size and colour: the
   primary in `timeLarge` 22 px mono and the material's `readoutText`, the
   secondary time in `timeSmall` `textSecondary`, tempo and signature in
   `timeSmall` `textPrimary`. After the primary come the secondary time
   (h:mm:ss.mmm, or samples, or timecode via right-click), the tempo with a
   `caption` "BPM" `space.xs` after it, and the signature with a 7 px chevron
   `space.xs` after it, centred on the figures. Click a field to
   type a position; drag vertically on a segment to nudge. The primary shows the
   heard position (delay-compensated).

   Spacing is measured between text, not boxes: the text sits `space.md` in from
   each end of the well and `space.lg` apart, whatever the values. Each field is
   as wide as its text plus `space.xs` on each side (its hover and focus area),
   so the well grows by a figure when the bars reach 10 or 100, when a count-in
   adds its minus, or when the tempo passes 100, and the bar lays out again.
   Every field draws on one shared baseline: the one that centres the 22 px
   primary in the 32 px control height. The smaller text sits on that line
   rather than being centred on its own.

   ```
   ╭────────────────────────────────────────────────────╮
   │ 12 1.1.000 16 0:00.000 16 120.00 BPM 16 4/4 ⌄ 12   │   gaps in px, text to text
   ╰────────────────────────────────────────────────────╯
   ```
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
numpad as in Pro Tools (0 play, 3 record, 1 go to start). All rebindable; full map
in interaction.md 10.4.

## Diagnostics panel (dock tab)

A table of engine health: callback time p50/p99/max
as sparklines over the last 60 s, overruns, late callbacks (callbacks that started
half a block, and at least 2 ms, after they were due, with the latest as a percentage of a
block: "2, latest 140% of a block", in `warning` when non-zero), driver dropouts (the
driver's own count, or "not reported" in `textTertiary` when the driver cannot count
them), disk gaps (streamed clips that ran dry, in `warning` when non-zero), and while
`--output-tap` runs, an Output tap row with the jumps and gaps found in what was sent
to the driver ("0 jumps, 0 gaps", in `warning` when non-zero). Rows without a
sparkline let their value use the rest of the row. Also disk read-ahead fill, plugin
latencies per track, worker thread count with how many joined MMCSS ("15 (15 MMCSS)"; on Linux
how many run real-time, "15 (15 real-time)"; in `warning` when any did not), device state. All in `small` and
`value` mono; `warning`/`danger` colouring at the same thresholds as the status
bar. A "Copy report" ghost button copies a text summary for bug reports.

## Acceptance

- Every control is 32 px in 40 px trays, with one readout well, a 22 px mono
  primary time and the token colours for each state.
- The readout's text is 12 px from the well's ends and 16 px apart, on one
  baseline, at short and long values alike.
- Recording: solid record button, `record` time text, "●" title prefix, disk
  readout in the status bar.
- Count-in shows negative bars in `warning`.
- The keyboard map works from the arrange view with nothing focused.
