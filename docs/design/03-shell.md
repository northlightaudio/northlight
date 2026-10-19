# 3. Application shell

## Purpose and anatomy

One main window holds everything a session needs; secondary windows exist only for
plugin editors, audio setup and the plugin browser. The shell has five zones:

```
+--------------------------------------------------------------------------+
| A  Header bar: file/edit menus · transport · time · tempo · view toggles   | 48
+-------------------------------------------------------+------------------+
|                                                       |                  |
| B  Arrange (ruler, track headers, lanes)              | D  Side panel    |
|                                                       |    (browser /    |
|                                                       |     inspector)   |
+-------------------------------------------------------+                  |
| C  Dock (mixer · editor · piano roll · diagnostics)   |                  |
+-------------------------------------------------------+------------------+
| E  Status bar: device · rate · buffer · latency · load · disk · messages   | 24
+--------------------------------------------------------------------------+
```

- **A Header bar**: `size.headerBar` 48 px, `surface0`. Left: app menu button
  (wordmark icon) and the project name as a ghost button that opens a menu (New, Open,
  Save, Save As, Recent, Export). Centre: the transport cluster and time display
  (section 6). Right: snap and ripple controls, zoom-fit, and three view toggles
  (Mixer, Editor, Browser) as icon toggle buttons.
- **B Arrange**: fills the remaining area; specified in section 4.
- **C Dock**: a bottom panel with tabs. Mixer, Editor (clip/insert generic editor),
  Piano Roll (Phase 8), Diagnostics. Height user-draggable, default 320 px, min
  `dockMin` 200, snaps closed. `surface0`.
- **D Side panel**: right, default 300 px wide, min `panelMin` 240, snaps closed.
  Tabs: Browser (plugins, presets, project media), Inspector (selected track, clip or
  insert). `surface0`.
- **E Status bar**: `size.statusBar` 24 px, `surface0`, `timeSmall` mono text.

Splitters between B/C and B/D per the Splitter component. Dock and side panel
remember their size and open state per user, not per project.

## Layout rules

- Minimum window 1280×720. Below 1440 wide the side panel defaults closed; below 900
  tall the dock defaults closed.
- The arrange view always has priority: panels never shrink it below 480×240.
- Secondary windows: native title bars, `surface0` content, remember position and
  size per window type; plugin windows per plugin type.

## Header bar detail

Left to right, with `space.md` gaps between groups and `space.xl` between clusters:

1. App button: 28×28 ghost with the mark; menu: About, Preferences, Audio Setup,
   Plugins, Quit.
2. Project name: ghost button, `label`, unsaved indicator as a 6 px `accent` dot
   after the name; menu as listed above. Hover shows the file path in a tooltip.
3. Undo / Redo: icon buttons, disabled when nothing to undo; tooltip names the action
   ("Undo Move clip").
4. Transport cluster: section 6.
5. Right cluster: Snap (toggle button with a chevron menu for grid size), Ripple
   (toggle), Zoom to fit (icon), then the three view toggles.

Everything in the header has a keyboard shortcut shown in its tooltip.

## Status bar detail

Left, in `timeSmall` mono, separated by middle dots in `textTertiary`:

- Driver and device: "ASIO · Universal Audio Volt"; a `warning` dot before it if the
  preferred device is unavailable, click opens Audio Setup.
- Sample rate and buffer: "48.0 kHz · 64" with the buffer's ms in a tooltip.
- Round trip: "4.3 ms RT" (measured in `textPrimary`, reported in `textSecondary`,
  tooltip explains which).
- Load: "CPU 4%" with p99 and max in the tooltip; turns `warning` above 70%, `danger`
  above 90%.
- Dropouts: hidden when zero; "2 dropouts" in `warning`, click resets, tooltip shows
  the last one's time.
- Disk: shown only while recording: "Disk 12 MB/s · 2.4 GB free".

Right: a single line for transient confirmations ("Saved", "Bounced mix.wav") in
`textSecondary`, fading after 4 s. Anything requiring action is a toast, not a status
message.

## Empty state

New project with no tracks: the arrange area shows the mark at 48 px in
`textTertiary`, the heading "Start a project" in `heading`, and three ghost buttons in
a row: "Add audio track", "Add instrument track", "Import audio…", plus a `small`
hint "or drop audio files here". No other chrome changes.

## First run

Before the main window shows for the first time, a sheet over the empty project:
title "Audio device", a one-line explanation, the driver and device selects, buffer
size, a live input meter, and a primary "Use this device" button. Skipping is
allowed; the status bar then shows the `warning` dot. Specified in section 8.

## Acceptance

- Screenshot at 1920×1080 100% and 3840×2160 150% matches the zone diagram with the
  sizes above; the header, status bar and ruler heights are exact.
- Dock and side panel open, close and resize with the 180 ms transition and remember
  state across restarts.
- The empty state and first-run sheet match the descriptions.
- No zone draws a border where a surface change already separates it.
