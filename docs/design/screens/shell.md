# 3. Application shell

## Purpose and anatomy

One main window holds a session; secondary windows are only for plugin editors,
audio setup and the plugin browser. The shell has five zones:

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

- **A Header bar**: `size.headerBar` 48 px, `surface1`, its clusters in recessed `surface0` trays.
  Left: the app menu button (the mark at 20 px in `textPrimary`, optically centred
  on its grid), then the project title, then undo and redo in their own tray. The
  project title is borderless and left-aligned, two lines centred on the bar: a
  `captionCaps` label, "PROJECT" in `textTertiary`, or "PROJECT · EDITED" in
  `textSecondary` while there are unsaved changes, over the name in `label`
  semibold `textPrimary` with the menu chevron right after it (`textTertiary`).
  Hover shows a `surface2` face; a click opens the project menu (New, Open, Save,
  Save As, Recent, Export); the tooltip gives the file's path. Its width fits the
  name (up to 200 px; 104 px when the bar is short of room). Centre: the transport
  cluster and time display (section 6). Right: snap and ripple controls, zoom-fit,
  and three view toggles (Mixer, Editor, Browser) as icon toggle buttons.
- **B Arrange**: fills the remaining area; specified in section 4.
- **C Dock**: a bottom panel with tabs. Mixer, Editor (clip/insert generic editor),
  Piano Roll, Diagnostics. Height user-draggable, default 320 px, min
  `dockMin` 200, snaps closed. `surface0`. A panel shown in the dock can need
  more: the Editor's minimum is its rows plus its tallest card's layout (a
  card laid out in pixels at its preferred height, a rendered face at half its
  size), and the dock grows to it when the panel opens or a taller card arrives.
  Where the window is too short for that, the Editor scrolls rather than
  squashing a card.
- **D Side panel**: right, default 300 px wide, min `panelMin` 240, snaps closed.
  Tabs: Browser (plugins, presets, project media), Inspector (selected track, clip or
  insert). `surface0`.
- **E Status bar**: `size.statusBar` 24 px, `surface0`, `timeSmall` mono text.

Splitters between B/C and B/D per the Splitter component. Dock and side panel
remember their size and open state per user, not per project.

## Panel model: fixed shell, detachable panels

**Decision**: v1 has a fixed shell with detachable panels, not a free docking system.
Every secondary view is a *panel* that one of three *hosts* owns at a time; the user
can move a panel between hosts but cannot split or nest hosts. Panels do not depend
on their host, so full docking can come later.

Panels (each a component with a stable id, title, icon and minimum size):

| Panel | Default host | Notes |
|---|---|---|
| Mixer | Dock | Pop-out keeps the strip layout; 240 px min height |
| Editor (insert chain / clip) | Dock | Follows the selection in whichever host it is in |
| Piano Roll | Dock | |
| Diagnostics | Dock | |
| Browser | Side panel | |
| Inspector | Side panel | |
| Audio Setup | Floating | Never docks; a settings window, not a work panel |
| Plugin browser (window form) | Floating | The same panel as Browser, pinned floating by the app menu |
| Plugin editors | Floating | Always their own windows (native plugin GUIs); never dock |

Hosts:

- **Dock** (bottom): tabs across the top, one panel visible at a time, resizable
  height, snaps closed.
- **Side panel** (right): same, resizable width.
- **Floating window**: one panel per window, native title bar, remembers size and
  position per panel, optional always-on-top (pin icon in the panel header), appears
  on the taskbar as a child of the main window.

Panel header: every hosted panel has a 32 px header row (the dock and side panel
draw it as the tab row; a floating window draws it under its native title bar) with
the panel's icon and title on the left and, on the right, the icon buttons
**Detach** (⤢, "Open in window") or **Attach** (⤡, "Return to dock" / "Return to
side panel"), and **Close**. Where a panel can live in both the dock and the side
panel (Editor, Browser), the attach button's menu offers both.

Interactions:

- Detach: click ⤢, or drag a tab out of the tab row past 24 px; the panel lifts into
  a floating window under the pointer at its last floating size (default 60% of the
  main window). The tab disappears from the host; if it was the only tab the host
  snaps closed.
- Attach: click ⤡ in the floating window, or drag the floating window's header onto
  the dock or side panel; the host highlights with `dropTarget` while a valid drop is
  over it; releasing inserts the tab at the pointer position.
- Keyboard: the panel's view shortcut (F2 Mixer, F3 Editor, F4 Browser, F5
  Inspector, F6 Piano Roll, F7 Diagnostics) shows the panel in its current host and
  focuses it; Shift+Fn toggles it between docked and floating.
- Closing a floating panel returns it to its default host, hidden, so it is never
  lost; the view toggle in the header lights only while the panel is visible
  somewhere.
- Multi-monitor: floating windows remember which monitor they were on and reopen
  there if it is present, else on the main window's monitor.

Persistence: a per-user layout file (`layout.json` beside the settings) records each
panel's host, order, visibility, dock height, side panel width and floating window
bounds. Projects do not carry layout. "Reset layout" in the View menu restores the
defaults. Two presets ship: "Mix" (mixer floating, dock and side panel closed) and
"Edit" (defaults).

Deferred: user-named presets, splitting a host into two tab groups, docking top or
left, tabbing floating windows together, per-project layouts.

## Layout rules

- Minimum window 1280×720. Below 1440 wide the side panel defaults closed; below 900
  tall the dock defaults closed.
- The arrange view always has priority: panels never shrink it below 480×240.
- Secondary windows: native title bars, `surface0` content, remember position and
  size per window type; plugin windows per plugin type.

## Window chrome

The main window has no Windows title bar: the header bar is the title bar. Its far
right holds the window buttons, minimise, maximise (restore when maximised) and
close: 46 px wide and the bar's full height, flat, `textSecondary` 10 px glyphs; hover
fills `surface2` (close fills `record` with a `textOnColour` glyph). Any part of the
bar that is not a control is the caption: drag to move, double-click to maximise,
drag to a screen edge to snap. Edge resizing, Aero Snap and the Snap Layouts flyout
over the maximise button stay native; the top 4 px is the top resize edge. The
window title (project name and app) still shows in the taskbar. Secondary windows
keep native title bars.

## Header bar detail

Left to right, with `space.md` gaps between groups and `space.xl` between clusters:

1. App button: 28×28 ghost with the mark; menu: About, Preferences, Audio Setup,
   Plugins, Quit.
2. Project title: as described under A.
3. Undo / Redo: icon buttons, disabled when there is nothing to undo; the tooltip
   names the action ("Undo Move clip").
4. Transport cluster: section 6.
5. Right cluster, all icon toggles 28×28 with tooltips: Snap (magnet icon with a
   chevron that opens the grid menu; the tooltip reads "Snap: Adaptive"), Ripple,
   Zoom to fit, a `lineStrong` divider, then Mixer, Editor and Browser as icon
   toggles that light `accent` when their panel is open.

The header shows no text except the project name, the time displays and the
tempo/signature values. Everything in the header has a keyboard shortcut shown in
its tooltip.

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
  the last one's time. Counts device xruns, late callbacks and disk gaps (a streamed
  clip that ran dry in steady playback; a locate's silence is not one). The warning
  toast after ten new dropouts names the disk when most of them are disk gaps, and
  then has no Audio Setup action, since a bigger buffer would not help.
- Disk: shown only while recording: "Disk 12 MB/s · 2.4 GB free".

Right: a single line for transient confirmations ("Saved", "Bounced mix.wav") in
`textSecondary`, fading after 4 s. Anything requiring action is a toast, not a status
message.

## Empty state

New project with no tracks: the arrange area shows the mark at 48 px in
`textTertiary`, the heading "Start a project" in `heading`, and three ghost buttons in
a row: "Add audio track", "Add MIDI track", "Import audio…", plus a `small`
hint "or drop audio files here". No other chrome changes.

## Opening a project

Opening a project (File > Open, a recent file, New from template, or Restore on the
autosave toast) never freezes the window. A loading sheet shows the progress:

- **Anatomy**: a blocking sheet (design-system.md, "Dialog (sheet)"), 480 wide. Title
  `title`: "Opening Big", "New project from Band", "Restoring Big" (the file's name
  without its extension). No body. Under the title a progress row: one line of `body`
  `textSecondary` (cut with an ellipsis) above a 4 px bar, `surface2` track with the
  `accent` fill (design-system.md, "Progress"). The button row holds one ghost
  "Cancel".
- **Stages and wording**, joined by middle dots:
  1. "Reading the project" while the file is read and parsed.
  2. "Reading audio · 3 of 12 · kick.wav" while its audio files are decoded.
  3. "Loading plugins · 2 of 5 · Reverb Pro" while third-party plugins load, naming
     the one loading now; "Setting up instruments and effects" when the project uses
     only built-ins.
  4. "Preparing playback" while the engine takes the project.
- **Progress** is determinate and never goes back. It is weighted by the work: each
  audio file one unit, each third-party plugin two, each built-in a twentieth, reading
  and preparing playback one each.
- **Timing**: the sheet holds the window's input from the start but appears only once
  the load has run `motion.progressDelayMs` (400 ms), with the sheet's usual entrance,
  so a quick open shows nothing. Third-party plugins are the exception: as soon as the
  file is read and decoded and any are left to load, the sheet starts its entrance
  without waiting out the delay, and no plugin loads until the sheet is fully on
  screen naming it (each one holds the window while it loads). It leaves with the
  sheet's fade once the project is open. Reduce-motion removes the entrance, not the
  delay.
- **While it shows** no key reaches the window and clicks outside the panel do
  nothing. The current project stays in the window, untouched, while the file is read
  and the audio decoded. When the plugins start loading the new project replaces it
  behind the sheet and Cancel turns disabled: from then on the load runs to the end.
- **Cancel** (or Esc) before then stops the load and leaves the current project as it
  was; the status bar says "Stopped: Opening Big".
- **Failure**: the sheet goes and an error toast says why ("Cannot open Big.northlight:
  file not found"); the current project stays.
- **Responsiveness**: reading and decoding run off the message thread. Plugins must be
  made on it, so they are made one at a time and the sheet is painted before each, so
  the longest pause is one plugin's own load time.

`--show-loading <file> [stage]` opens a file this way and holds the load at a stage,
so `--capture` can photograph the sheet; `--time-load <file>` prints each stage's time.

## First run

On first launch, welcome sheets over the empty project choose the audio device and
scan for plugins (section 8.3). Skipping is allowed; the status bar then shows the
`warning` dot.

## Acceptance

- At 100% and 150% scaling the window matches the zone diagram; header, status bar
  and ruler heights are exact.
- Dock and side panel open, close and resize with a 180 ms transition and remember
  their state across restarts.
- Every panel in the table detaches and reattaches by button, drag and Shift+Fn; the
  drop highlight shows over a valid host; layout survives a restart; closing a
  floating panel does not lose it; "Mix" reopens the mixer on its recorded monitor.
- The empty state and first-run sheet match their descriptions.
- Opening a large project shows the loading sheet with each stage's wording and a
  determinate bar; the window repaints throughout, and Cancel before the plugin stage
  leaves the current project unchanged.
- No zone draws a border where a surface change already separates it.
