# 11. Accessibility and performance

## 11.1 Contrast

- Text on its surface: 4.5:1 minimum for `body` and smaller, 3:1 for `title` and
  larger. `textTertiary` is only 3.6:1 on `surface0`, so it is used only at 13 px or
  larger or for non-essential hints. The full table is in [contrast.md](contrast.md),
  generated from the token file.
- Non-text (icons, control outlines, meter zones, focus ring): 3:1 against adjacent
  colour. The focus ring at 70% accent over `surface0` is 4.2:1.
- Track colours are never the only carrier of meaning: clips also carry names,
  strips carry numbers, and the twelve hues are distinguishable in the three common
  colour-vision deficiencies (check in a simulation; swap any pair that collapses).
- A "high contrast" variant of the token file (lines at 24%, text one step
  brighter) is selectable in Preferences.

## 11.2 Keyboard and focus

- All functionality reachable without a pointer, per the map in section 10.
- Focus ring visible on every focusable element; focus order follows the visual
  order; focus is never lost (closing a window returns it to the opener).
- Modal sheets trap focus; Esc closes.

## 11.3 Screen readers (UI Automation)

- Every control exposes a name (its label), role, value and state. Custom
  components: faders and knobs are sliders with dB/pan values, meters read-only
  values, clips list items with name and position, track headers groups.
- Live regions: the status bar's transient message and toasts announce.
- Verified with Narrator on the arrange, mixer and audio setup screens.

## 11.4 Motion and flashing

- Honour the OS reduce-motion setting (section 2.6).
- Nothing flashes faster than 3 Hz; the metronome pulse and the clip indicator are the
  only periodic visuals and both are below that.

## 11.5 Scale and DPI

- Per-monitor DPI aware (v2). The window re-lays out on a DPI change without
  reopening; plugin windows pass the scale to the plugin.
- Verified at 100, 125, 150 and 200% on the reference monitor and at mixed DPI across
  two monitors.
- Minimum window 1280×720 at 100%; all text remains readable at 200% on 1920×1080.

## 11.6 Performance budget

| Scenario | Budget |
|---|---|
| Arrange, 64 tracks visible, playing, 3840×2160 at 150% | ≤ 16 ms per frame, p99 |
| Mixer, 32 strips + 4 buses + master, meters live | ≤ 16 ms per frame, p99 |
| Zoom or scroll gesture in the arrange | no frame over 16 ms during the gesture |
| Opening the dock or side panel | transition completes in 180 ms with no dropped frames |
| Project open, 32 tracks | interactive window in < 1 s; peaks fill in afterwards |
| Idle (stopped, nothing hovered) | 0 repaints per second except the status bar's 1 Hz |

## 11.7 Rendering rules

- Paint only what changed: meters, playhead and counters each repaint their own
  bounds on a 60 Hz timer; the arrange repaints clip rectangles only when the model
  or the view changes; scrolling blits.
- Waveforms come from the peak cache mip nearest to the pixel density; no mip is
  computed on the message thread. Zoomed in past the finest mip, they draw the
  file's samples, decoded in the background; until a block arrives that stretch
  draws from the mip.
- Text is cached per label; no per-frame font lookups.
- Images (icons) are vector paths rasterised once per scale.
- The playhead is drawn by a separate overlay component so the lanes beneath are not
  repainted as it moves.

## 11.8 Measurement

- The Diagnostics tab shows frame time p50/p99/max for the last 60 s from a paint
  timer in the main window.
- The `--benchmark-ui <seconds>` launch flag plays and scrolls the open project
  unattended and prints the frame-time percentiles, so a budget regression can be
  caught on a reference machine.

## Acceptance

- [contrast.md](contrast.md) covers every text/surface pair in the token file.
- A Narrator walkthrough of the three screens in 11.3.
- Frame-time percentiles for the six scenarios, recorded in `docs/architecture.md`.
