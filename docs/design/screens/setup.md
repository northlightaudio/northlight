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
  [ ] Pass inputs through to outputs   Level ──────o──  -6.0 dB
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
- Pass-through is **off by default** and is never turned on by the app: a
  microphone on an input and speakers on the output would feed back the moment the
  device opens. The checkbox carries the hint "Hear the selected inputs directly.
  Use headphones." and, when turned on while any input meter is live, a one-time
  toast warns about feedback. Track monitoring uses each track's monitor mode
  instead.
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

Two welcome sheets over the empty project on the very first launch, 600 px wide.
Never shown again unless the settings file is missing (`--first-run` shows them).

Both open with the welcome hero: a 132 px band of the brand photograph at the
panel's width, nudged up 16 px so its moon sits in the upper part (`canvas` wash at
12%). It stays clear over the top 40%, then dissolves into the `surface0` panel (85%
by about three quarters down, solid at the bottom), with "WELCOME TO"
(`captionCaps`, `textPrimary`) over the wordmark at 30 px, at the panel's
`space.xl` margin. The photograph is drawn with high-quality resampling. Below it, the step in `captionCaps` `textTertiary`, the
title (`title`), one sentence (`body`, `textSecondary`), the content, then the
button row with a footnote (`caption`, `textTertiary`) at its left.

1. **Step 1 of 2: Choose your audio device.** "Pick the interface you record and
   listen through. Northlight checks it before you start." A card (`surface1`,
   radius `md`, `lineSubtle` rim and hairlines between rows, 56 px rows): each row a
   label (`label`, `textPrimary`) over a one-line hint (`caption`, `textTertiary`)
   on the left, the control (260 px) on the right:
   - Driver: "The lowest latency, for recording and monitoring" when ASIO is
     chosen, else "ASIO, when your interface has it, gives the lowest latency".
   - Device: "Where Northlight plays and records".
   - Buffer size: "Smaller is more responsive; larger avoids clicks". Each size
     reads "128 samples · 2.7 ms" at the device's rate; the device's own size is
     always in the list (Windows Audio often runs at 480).
   - Input: a live meter; "Play or sing: the meter should move".
   Footnote "You can change this any time in Audio Setup."; "Skip" and "Use this
   device" (primary).
2. **Step 2 of 2: Find your plugins.** "Northlight looks in the standard VST3 and
   CLAP folders. The scan runs in the background, so you can start working straight
   away." Footnote "Scan again any time from the Browser."; "Later" and "Scan now"
   (primary).

## 8.4 Splash screen and About

A 600 by 400 borderless window with a drop shadow, centred on the main display, on
top while the app starts. The brand photograph (moon over wheat, Gytis Bukauskas on
Unsplash; `assets/splash`) fills it, with a `canvas` wash at 10% and a gradient
darkening the field into `canvas` at 94% along the bottom, where the text sits in a
`space.xxl` margin:

- **Top left**: the mark at 28 px in the sky, aligned to the margin, all in `textPrimary`
  (the Overcast horizon would vanish into the photograph's overcast sky).
- **Bottom left**: the on-dark wordmark (Newsreader, 36 px) directly above "Version 0.1.0" (`small`,
  `textSecondary`) and what is loading now (`caption`, `textTertiary`): preparing the
  audio engine, loading the plugin list, opening the main window, opening the audio
  device, ready.
- **Right**, right-aligned in `caption`: "© <year> the Northlight contributors" and
  "Free software under the GNU AGPLv3" in `textSecondary`, the photo credit in
  `textTertiary`.
- **Progress**: a 2 px `accent` line along the bottom edge.
- A 1 px `lineStrong` frame.

It stays at least 1.8 s, then fades out over 280 ms; a click once the app is ready
closes it at once. `--no-splash` starts without it. About Northlight (app menu)
shows the same window over the main window, without the progress line and with
"Click anywhere to close"; a click, Escape or switching away closes it.

## Acceptance

- Audio Setup screenshot matches the card layout, 120 px label column, live input
  meters per channel, and the status line with its dot.
- Calibration flow screenshot mid-run shows the inline progress.
- Preferences: each page screenshot matches the row layout; Shortcuts rebinding and
  conflict display verified.
- First-run sheet screenshot matches; it does not appear on a second launch.
