# 8. Audio setup, preferences, first run

## 8.1 Audio setup

Window 720×620 default, `surface0`, sections as cards
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

STEREO INPUTS
  Pair two inputs into a stereo input that tracks can record from. Single inputs are always available.
  [Vocals          ]  L [1: INPUT 1 ▾]  R [2: INPUT 2 ▾]   [Remove]
  [Stereo 3/4      ]  L [3: INPUT 3 (off) ▾]  R [4: INPUT 4 (off) ▾]   [Remove]
  [Add stereo input]

LATENCY
  Reported      in 2.83 ms (136)  ·  out 1.50 ms (72)  ·  round trip 4.33 ms
  Measured      not measured                      [Calibrate…]
  Loopback      [LINE 1 ▾] → [INPUT 1 ▾]

MONITORING
  [ ] Pass inputs through to outputs   Level ──────o──  -6.0 dB
  Input ▁▃▅▇▆▅▃▁        Output ▁▃▅▇▆▅▃▁

● Running · ASIO · 2 in / 2 out · 0 dropouts
```

- Labels `body` `textSecondary` in a 120 px column, flush with the card's padding; controls `control` 28 px; values
  in `value` mono.
- Channel rows show a live 8 px input meter next to each enabled input so the user
  can find the right one by playing.
- **Stereo inputs** (like Cubase's input buses): each row is a name (a text field,
  180 px), the left and right device channels (combos listing every input as
  "3: INPUT 3", "(off)" after a disabled one) and Remove. **Add stereo input** adds
  the first neighbouring pair no row uses yet, named "Stereo 1/2". Picking a
  disabled channel enables it. Names are unique (a repeat gains " 2"); an empty name
  reverts. Four rows show; more scroll. Without an open device the hint reads "Open
  a device to set up its stereo inputs." and Add is disabled.
- Stereo inputs belong to the input device and are kept per device in the user's
  settings, so switching interfaces and back brings them back; a project stores
  only the name its tracks chose.
- Calibrate runs as an inline progress ("Playing click… 3/5") with the measured
  result appearing in `textPrimary` and the reported value dimming to
  `textSecondary` once a measurement exists; Clear resets.
- Pass-through is **off by default** and the app never turns it on: a microphone
  and speakers would feed back the moment the device opens. The checkbox carries the hint "Hear the selected inputs directly.
  Use headphones." and, when turned on while any input meter is live, a one-time
  toast warns about feedback. Track monitoring uses each track's monitor mode
  instead.
- Status line at the bottom in `timeSmall` mono with a `success`/`warning`/`danger`
  dot.
- Device loss: the status line turns `warning` with "Device unavailable, retrying…";
  the Device combo shows the preferred device greyed with "(unavailable)".

## 8.2 Preferences

A window, 820×600 default (640×420 minimum), in two planes and no divider: the page
list on `canvas` and the page on `surface0`, where settings sit in cards like Audio
Setup's. **Decision**: Preferences and Audio Setup share one card language, so the
Audio page, shown inline, looks like every other page.

```
 ┌──────────────┬─────────────────────────────────────────────────────┐
 │ ⚙ General    │  General                                            │
 │ ∿ Audio      │  ┌ PROJECTS ──────────────────────────────────────┐ │
 │ ● Recording  │  │ Folder     [C:\Users\…\Northlight      ] [Choose…]│ │
 │ ✎ Editing    │  │            New projects are saved here.         │ │
 │ ⊓ Plugins    │  │ Recent     [    8 ]                             │ │
 │ ⌨ Shortcuts  │  │ Autosave   [ 2 min ]                            │ │
 │ ⌁ Diagnostics│  └────────────────────────────────────────────────┘ │
 │              │  ┌ APPEARANCE ────────────────────────────────────┐ │
 │              │  │ Theme      ( Dark | Light )                     │ │
 │              │  │ Accent     ● ● ● ● ● ●   [#93ABC2]              │ │
 │              │  └────────────────────────────────────────────────┘ │
 └──────────────┴─────────────────────────────────────────────────────┘
```

### Page list

- 160 px wide on `canvas`, `space.xl` top inset so the first item lines up with the
  page title, `space.sm` side inset.
- Items 32 px (`controlLarge`), radius `sm`: a 16 px icon and the name in `body`,
  `space.sm` apart, `space.md` from the item's left edge. Icons: General
  `settings`, Audio `trackAudio`, Recording `record`, Editing `pencil`, Plugins
  `plugin`, Shortcuts `keyboard`, Diagnostics `diagnostics`.
- Rest: `textSecondary` icon and name. Hover: `surface1` fill, `textPrimary` name.
  Selected: `surface2` fill, `textPrimary` name, `accent` icon. Keyboard focus: the
  focus ring around the selected item. Up and Down move between pages, Home and End
  go to the first and last.

### Page

- `space.xl` inset; the title in `title` `textPrimary` on a 32 px row, `space.lg`
  above the first card. Cards are at most 600 px wide and left aligned, `space.md`
  apart; the page scrolls when they do not fit.
- A card: `surface1`, 1 px `lineSubtle` border, radius `md`, `space.lg` padding, its
  title in uppercase `caption` `textTertiary` on a 22 px row with `space.md` below.
- A row: the label in `body` `textSecondary` in a 120 px column (`labelWidth`, the
  same column as Audio Setup), the control beside it, rows `space.sm` apart. Every
  label fits its column at 100% without an ellipsis.
- A hint sits under its control in `small` `textTertiary`, `space.xs` below it, and
  wraps (never clips) to the control column's width.
- Controls take the width of what they hold, never the column: combos 200 px,
  numeric fields 96 px, segmented controls their natural width (their track in
  `surface0`, a well in the card), buttons their label
  plus `space.md` each side, path fields the column less the button beside them.
- Checkboxes stand alone in the control column with no text of their own; the
  label says what they set and clicking the label toggles them.
- A setting that is kept but not used yet carries a **Coming soon** badge (`info`
  at 20% with `info` text) `space.sm` after its control, with the tooltip "Saved
  now, used in a later version." A choice that does not work yet is disabled in its
  menu with "(coming soon)" after it. No hint talks about the implementation.

### Pages

- **General**
  - *Projects*: Folder (a path field with **Choose…**; when empty it shows the
    default, the Documents folder, as its placeholder; hint "New projects are
    saved here."), Recent (0 to 20, "Shown in the Project menu."), Autosave (0 to
    60 min, "Keeps unsaved changes for recovery after a crash. 0 turns it off.").
  - *Appearance*: Theme (segmented Dark | Light, Light disabled with the tooltip
    "The light theme is coming soon."), Accent (**Decision**: user accent override
    is in scope: six presets plus hex), Contrast (segmented Standard | High; "High
    brightens lines and secondary text."), UI scale (combo: Follow system, 100% to
    200%). All three apply at once: the colours load again into the running theme
    and every window repaints. The Light segment stays disabled (`textDisabled`, no
    hover, skipped by the arrow keys) with its tooltip until the light theme lands
    (design-system.md 2.9).
  - Accent swatches are 20 px circles `space.sm` apart, from the track palette
    entries named in `track.accentPresets` (Overcast, the default, first), each with
    its name as tooltip. Hover: a 1 px `lineStrong` ring 2 px outside. Chosen: a
    2 px `textPrimary` ring 2 px outside. Left and Right move the choice; the focus
    ring surrounds the row. The hex field (96 px, `value` mono) shows the chosen
    colour's hex and accepts any `#RRGGBB`; an invalid entry turns its border
    `danger` and reverts when the field loses focus.
- **Audio**: the Audio Setup content inline (8.1), its cards on the same left edge
  as every other page's.
- **Recording**
  - *Files*: Format (Coming soon; WAV 32-bit float; WAV 24-bit (coming soon)), Take names (a
    200 px field, Coming soon; "{track} and {take} become the track name and take
    number.").
  - *Count-in and click*: Count-in (segmented Off | 1 bar | 2 bars | 4 bars; "Used
    when a project opens."; the header's count-in toggle steps through the same
    four), Pre-roll (bars, Coming soon), Click level (dB).
  - *Transport*: Return on stop (checkbox, on by default; "Stop takes the playhead
    back to where playback started. Pause (Shift+Space) leaves it where it is.").
- **Editing**
  - *Snap*: Snap (checkbox; "On when a project opens."), Grid (combo: Adaptive, Bar,
    Beat, 1/8, 1/16, 1/32).
  - *Edits*: Ripple (checkbox; "On when a project opens."), Fade (ms, Coming soon;
    "The fade new clips start with.").
  - *View*: Follow playhead (checkbox; "Scrolls the arrange view to keep the playhead in
    sight.").
- **Plugins**
  - *Folders*: the extra scan folders as 28 px rows in a `surface0` well (radius
    `sm`): the folder icon, the path in `body` (ellipsis at the start, so the folder
    name shows) and a ghost remove button (`close`, "Remove folder"); hover
    `surface2`. Empty: "Only the standard VST3 and CLAP folders are scanned." in
    `textTertiary`, centred. Four rows show; more scroll. **Add folder…** below,
    with the hint "Scanned as well as the standard VST3 and CLAP folders."
  - *Scanning*: Scan on launch (checkbox; "Looks for new and changed plugins when
    Northlight starts."), Scan (**Scan for changes**), Blocklist ("None" or "3
    plugins" in `body` `textPrimary`, then **Clear**, which only shows when there are some;
    "Plugins that crashed or hung while being scanned are skipped until cleared.").
  - *Windows*: Keep on top (checkbox; "Plugin windows stay above the main window.").
- **Shortcuts** (the keyboard map editor; design-system.md "Key binding"): the
  search field (the `search` icon inside it, `space.sm` from its left edge) with
  **Reset all** (secondary, disabled while every key is the default) `space.sm` to
  its right; under them the hint "Click a shortcut's keys to change them. Shift
  with a panel's key detaches or reattaches it." in `small` `textTertiary`; then a
  card holding the table, which fills the rest of the page and scrolls. Actions are
  grouped under uppercase `caption` `textTertiary` headers (Transport, View,
  Tracks, Editing, Files, App, Piano Roll, Audio Editor), one row per action of the
  registry (interaction.md 10.4); rows are 28 px with the action in `body`
  `textPrimary` and its keys right aligned as key caps (the tooltip's: `caption`
  `textSecondary` on `surface2`, 1 px `lineStrong`, radius `sm`, 18 px high), with
  "or" in `small` `textTertiary` between alternatives, "/" between a fixed
  family's keys and "to" for a run of digits ("1 to 7"); an action with no key
  shows "None" in `small` `textTertiary`. Hover `surface2`. The search matches the
  action, its keys and its section; no match shows "No shortcuts match “…”."
  centred in `textTertiary`. Rebinding, conflicts and resets work as the Key
  binding pattern says; every change applies at once (keys, tooltips, menus) and
  is saved in the user settings.
- **Diagnostics**
  - *Crash reports*: Send reports (checkbox, Coming soon; "Reports are always kept on this
    computer, in the settings folder's Crashes folder.").
  - *Support*: Settings (the folder's path in `small` `textSecondary`, ellipsis at
    the start, then **Show in Explorer**), Report (**Copy system report**; "Copies
    device, plugin and system details for a bug report.").

## 8.3 First run

Two welcome sheets, 600 px wide, over the empty project on first launch. They do
not show again unless the settings file is missing (`--first-run` forces them).

Both open with the welcome hero: a 132 px band of the brand photograph at the
panel's width, its sky and moon clear at the top, dissolving into the `surface0`
panel behind the text, with "WELCOME TO"
(`captionCaps`, `textPrimary`) over the wordmark at 30 px, at the panel's
`space.xl` margin. Below it come the step in `captionCaps` `textTertiary`, the
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
   - Buffer size: "Smaller is more responsive; larger avoids clicks". The sizes
     the device offers (32 to 1024 when it lists none), as Audio Setup lists them,
     each reading "128 samples · 2.7 ms" at the device's rate; the size it runs at is
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

- **Top left**: the mark at 28 px in the sky, aligned to the margin, all in
  `textPrimary` (an Overcast horizon would vanish into the sky).
- **Bottom left**: the on-dark wordmark (Newsreader, 36 px) directly above the
  version ("Version 0.1.0", `small`,
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

- Audio Setup matches the card layout, 120 px label column, live input meters per
  channel, and the status line with its dot.
- Calibration shows the inline progress while it runs.
- Preferences: each page matches the card and row layout, no control is wider than
  what it holds, no label or hint clips, every setting not used yet carries its
  badge, and the Audio page's cards line up with the other pages'.
- The first-run sheets match and do not appear on a second launch.
