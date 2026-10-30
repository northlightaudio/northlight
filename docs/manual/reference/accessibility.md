---
title: "Accessibility"
description: "Screen readers, working from the keyboard, contrast, motion and size."
sidebar:
  order: 2
---

Northlight aims to be usable with a screen reader, from the keyboard, and by people
who need more contrast, less movement or larger text. None of the options below
change how Northlight looks unless you, or Windows, ask for them. This page covers
what works today and the few places that still need a mouse.

## Screen readers

Northlight speaks to screen readers through Windows' UI Automation, which Narrator
and other Windows screen readers use.

Every control has a name, and says its value and state:

- Buttons that turn something on or off (play, loop, click, snap, the tools) say
  whether they are on.
- The position counters in the header bar read out the playhead's position.
- Track header and mixer controls name their track: "Mute Bass" rather than "M".
- Faders, pan controls and knobs read their values in dB or as a pan position, and
  meters read their level.
- The media and plugin browsers read file and plugin names.
- Every setting in Preferences, Audio Setup and the Export sheet is named after its
  label.
- Messages in the status bar and pop-up notices are announced as they appear.

Much of a DAW is drawn rather than made of standard controls: clips, notes, lanes.
While a screen reader is running, Northlight also makes these readable, as lists:

- Each track's clips, with their name, kind, position in bars and beats, and whether
  they are muted, have gain or have missing media; then its take lanes, with each
  take and where the comp plays it.
- The markers and regions.
- The tempo track's tempo and time signature changes.
- Each open automation lane, with its value at the playhead and its points.
- The piano roll's notes in time order, with pitch, position, velocity and whether
  they are muted.
- The mixer's channel names, insert slots, sends and channel strip sections.

Pressing one of these items does what a click does: it selects the clip or note, or
switches the channel strip section. Its menu is the one a right-click opens. When you
step through clips or notes with Ctrl+Left and Ctrl+Right, the screen reader follows.

None of this is made unless a screen reader (or another UI Automation program) is
running, so it costs nothing otherwise.

Not yet readable: the notes in the audio editor's pitch mode, the piano roll's
controller lane and velocity bars, and the clip being recorded. Third-party plugins
draw their own windows, so how well they read depends on the plugin; Northlight's
built-in effects and instruments name their controls.

## Working from the keyboard

Every action in the [shortcuts list](shortcuts.md) can be reached by a key, and most
keys can be changed.

**Moving the focus.** Tab moves through the main window in order: the header bar,
the selected track's header, the arrange view, the dock, the side panel, then the
status bar, and back round to the start. Shift+Tab goes backwards. Only the selected
track's header is a stop on the way; Up and Down move to the track above or below,
and the focus moves with them. A focus ring shows what has the focus once you press
Tab or an arrow key; it stays hidden while you use the mouse.

**In the arrange view.** Up and Down select tracks (Shift extends the selection).
Ctrl+Left and Ctrl+Right select the previous or next clip on the selected track, with
Shift to extend. Left and Right nudge the selected clips. Esc clears the selection.

**Menus.** Shift+F10, or the Menu key, opens the menu of what has the focus: the
selected clip's menu, else the time selection's, else the selected track's. In the
piano roll it opens the note menu.

**Notes.** In the piano roll, Insert adds a note after the selected one (or at the
playhead), Up and Down move notes, and Alt+Up and Alt+Down make them louder or softer.
The audio editor has keys to step through the notes of a vocal and move them (see
[Editing notes](../pitch/notes.md#keys)).

**Windows and dialogs.** Tab stays inside a dialog until it closes, and closing a
dialog or window returns the focus to where it was. Esc closes plugin and insert
editor windows. In Preferences, Up and Down move between pages, and Home and End go
to the first and last.

A few edits still need a pointer: dragging automation points and clip gain,
stretching and fading clips, reordering inserts, dragging from the browsers, and the
menus of the tempo track and automation lanes.

## Contrast

**Preferences > General > Contrast** has three choices:

- **Standard**: the normal look.
- **High**: brightens lines, secondary text, disabled text and the small print on
  effect faces, so every piece of text stands out clearly from its background. Where
  colour alone shows a state, High adds a shape: a clip light that has lit gets a
  bright outline, and a note that [Correct](../pitch/correct.md) left alone gets a
  dashed outline.
- **System**, the default: High when Windows is using a contrast theme, Standard
  otherwise. Northlight notices if you change the Windows setting while it runs.

Changes apply at once, without a restart. You can also pick an **Accent** colour on
the same page; it colours selections, lit buttons and the focus ring.

Track colours are never the only way to tell things apart: clips carry their names
and mixer channels carry numbers.

## Motion

**Preferences > General > Reduce motion**: **On** makes panels, sheets and menus
appear without sliding and stops animated glows. Meters still move, since they carry
information. **System**, the default, follows Windows' **Show animations** setting,
and notices if it changes while Northlight runs. **Off** keeps the animations
whatever Windows says.

Nothing in Northlight flashes more than three times a second.

## Size

**Preferences > General > UI scale** sets the size of the whole interface: **Follow
system** (the default, Windows' own display scale), or **100%**, **125%**, **150%**
or **200%**. Northlight redraws sharply when you move it between monitors with
different scales.

## Something not working for you?

If something you need cannot be reached, read or seen, please tell us: see
[Getting help](help.md). Accessibility problems are bugs.
