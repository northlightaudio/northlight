# 8. Audio setup, preferences, first run

## 8.1 Audio setup

The current Audio Setup window is structurally right; the pass refines it rather
than redesigns it. Window 720×620 default, `surface0`, sections as cards
(`surface1`, radius `md`, `space.lg` padding, uppercase `caption` section titles).

```
DEVICE
  Driver        [ASIO ▾]
  Device        [Universal Audio Volt ▾]        [Control panel]
  Sample rate   [48.0 kHz ▾]   Buffer  [64 ▾]  1.3 ms

CHANNELS
  Inputs                        Outputs
  [✓] 1  INPUT 1   ▁▃           [✓] 1  LINE 1
  [✓] 2  INPUT 2   ▁            [✓] 2  LINE 2
  [ ] 3  INPUT 3                [ ] 3  LINE 3
  link 1+2 as stereo ⟷          link 1+2 ⟷

LATENCY
  Reported      in 2.83 ms (136)  ·  out 1.50 ms (72)  ·  round trip 4.33 ms
  Measured      not measured                      [Calibrate…]
  Loopback      [LINE 1 ▾] → [INPUT 1 ▾]

MONITORING
  [✓] Pass inputs through to outputs   Level ──────o──  -6.0 dB
  Input ▁▃▅▇▆▅▃▁        Output ▁▃▅▇▆▅▃▁

● Running · ASIO · 2 in / 2 out · 0 dropouts
```

- Labels `body` `textSecondary` in a 120 px column; controls `control` 28 px; values
  in `value` mono.
- Channel rows show a live 8 px input meter next to each enabled input so the user
  can find the right one by playing.
- Stereo link toggles ("⟷") pair adjacent channels; linked pairs show as "1/2" in
  track input selects.
- Calibrate runs as an inline progress ("Playing click… 3/5") with the measured
  result appearing in `textPrimary` and the reported value dimming to
  `textSecondary` once a measurement exists; Clear resets.
- Status line at the bottom in `timeSmall` mono with a `success`/`warning`/`danger`
  dot.
- Device loss: the status line turns `warning` with "Device unavailable, retrying…";
  the Device combo shows the preferred device greyed with "(unavailable)".

## 8.2 Preferences

A window with a left list of pages (160 px, `surface0`) and a content area:

- **General**: project folder, autosave interval, recent files count, theme (Dark;
  light later), accent colour (**Decision**: user accent override is in scope: a
  swatch row of 6 presets plus hex), UI scale override (Follow system, 100, 125,
  150, 200%).
- **Audio**: opens the same Audio Setup content inline.
- **Recording**: file format (WAV float / WAV 24-bit), recordings folder, count-in
  default, pre-roll, metronome sound and level, auto-name pattern.
- **Editing**: snap defaults, default fade length and shape, ripple default,
  follow playhead.
- **Plugins**: scan folders list (add/remove), scan on launch toggle, blocklist
  management, "Open plugin windows on top".
- **Shortcuts**: a searchable table of actions and keys; click a key cell to rebind;
  conflicts shown in `warning`; Reset to defaults.
- **Diagnostics**: crash reporting opt-in, log folder, "Copy system report".

Each page: `title` heading, settings in rows of label (200 px) and control, `space.md`
vertical rhythm, section headers in uppercase `caption`.

## 8.3 First run

Sheet over the empty project on the very first launch (section 3): title "Choose
your audio device", one sentence "Aural works best with an ASIO device. You can
change this any time from the status bar.", Driver and Device selects, Buffer size,
a live input meter row, "Use this device" primary, "Skip" ghost. After choosing,
a second step offers "Scan for plugins now?" with Scan / Later. Never shown again
unless the settings file is missing.

## Acceptance

- Audio Setup screenshot matches the card layout, 120 px label column, live input
  meters per channel, and the status line with its dot.
- Calibration flow screenshot mid-run shows the inline progress.
- Preferences: each page screenshot matches the row layout; Shortcuts rebinding and
  conflict display verified.
- First-run sheet screenshot matches; it does not appear on a second launch.
