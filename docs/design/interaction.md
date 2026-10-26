# 10. Interaction

## 10.1 State matrix

Every interactive element implements these states; a component may not skip one.

| State | Trigger | Visual rule |
|---|---|---|
| Rest | none | Tokens as specified per component |
| Hover | pointer over | One surface step lighter or text one step brighter; 100 ms; never both at once |
| Pressed | button down | `surfacePressed` or the pressed step; immediate |
| Active | toggle on, selected | `accent` (or the semantic colour) at 20% fill, colour text, 1 px inset outline |
| Focused | keyboard focus | 2 px `lineFocus` ring outside; only for keyboard focus |
| Disabled | not applicable now | `textDisabled`, no hover, tooltip explains why when known |
| Dragging | drag in progress | Source at 40% alpha, ghost follows pointer, snap guide and value tooltip |
| Drop target | valid drag over | `dropTarget` fill and `accent` outline on the target |
| Loading | async work | Progress component in place; controls that depend on it disabled |
| Error | failed | `danger` outline or text and a message within 1 line |

Hover is suppressed while any drag is in progress and during keyboard navigation
until the pointer moves.

## 10.2 Pointer conventions

- Left button: select, move, adjust.
- Right button: contextual menu, always; never an action.
- Middle button or Space+drag: pan the arrange or piano roll.
- Wheel: scroll; Shift+wheel: horizontal; Ctrl+wheel: zoom at pointer; wheel over a
  valued control: step it (only when the control is focused or hovered for 300 ms,
  to avoid accidental fader moves while scrolling).
- Double-click: rename (names), reset (controls), open (clips open the editor,
  inserts open the plugin).
- Modifiers, consistent everywhere:
  - Shift: fine adjustment, extend selection, constrain drag axis.
  - Ctrl: toggle selection, duplicate on drag, coarse adjustment on fields.
  - Alt: override snap, trim content instead of edges, break group link.
- Cursors: default, hand (clip header), resize-horizontal (trim and splitters), fade
  (custom: a small curve), move (four arrows), not-allowed (invalid drop), I-beam
  (range tool), and the edit tools' icons (custom: pencil, eraser, scissors, glue,
  struck-through clip) with the hot spot where the tool acts.

## 10.3 Snapping

- Grid snap is on by default at "Adaptive" (bar/beat/sub-beat depending on zoom);
  options: Adaptive, Bar, Beat, 1/8, 1/16, 1/32, Off; triplet and dotted variants in
  a submenu.
- Snap targets in priority: clip edges on the same track, markers, loop and punch
  edges, playhead, grid. Within 6 px of a target the ghost snaps and a 1 px
  `snapGuide` vertical line is drawn through all lanes; the tooltip names the target
  ("to 02 Hats end").
- Alt during a drag disables snap for that drag; the guide disappears.
- Relative snap (keep the clip's offset to the grid) for moves when the clip did not
  start on the grid; absolute for new clips and trims.

## 10.4 Keyboard map

Defaults; every action except the fixed families (tools by number, the arrow keys)
is rebindable on the Preferences Shortcuts page (screens/setup.md 8.2), and the keys
show in tooltips and menus. As implemented, the keys live in one registry
(`ui/Shortcuts`): each action has an id, a name, a section, a context and its
default keys, and the window, the panels, the arrange view, the piano roll and the
audio editor dispatch from it while the Shortcuts page, tooltips and menus print
it. A context is where a key works: **global** keys work anywhere in the main
window; **arrange** keys when the key reaches the arrange view (the focus is not
in a view that takes it); **piano roll** and **audio editor** keys while that view
has the focus, and they are tried before the global ones. So a key may be bound
once per context, and a view's key and a global key collide (the page warns).
**Decision**: the one default overlap is F2, which renames the selected track
while the arrange view has the focus and shows the mixer elsewhere.

**Global**

| Action | Key |
|---|---|
| Play / stop | Space (or Num0) |
| Pause (keeps the position; Play resumes from it) | Shift+Space |
| Record | R (or Num3) |
| Go to start / end | Enter, Home or Num1 / Shift+Enter, End |
| Loop / punch / click on or off | L / P / C |
| Zoom in / out / fit / selection | Ctrl+= / Ctrl+- / Z / Shift+Z |
| Snap / ripple on or off | N / Shift+N |
| Undo / redo | Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z |
| Mute / solo / arm the selected track | Shift+M / Shift+S / Shift+R |
| Add audio / MIDI track / bus | Ctrl+T / Ctrl+Shift+T / Ctrl+B |
| New / open / save / save as | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Import audio | Ctrl+I |
| Export (bounce) | Ctrl+E |
| Mixer, Editor, Browser, Inspector, Piano Roll, Diagnostics, Instruments, Audio Editor | F2 to F9; with Shift added, the panel detaches or reattaches |
| Audio Setup / Plugins / Preferences | Ctrl+Shift+A / Ctrl+Shift+P / Ctrl+, |
| Search the browser | Ctrl+F |

**Arrange view**

| Action | Key |
|---|---|
| Rename the selected track | F2 while the arrange view has the focus |
| Previous / next marker | , / . |
| Add marker / region from the loop | M / Alt+M |
| Cut / copy / paste / duplicate / delete | Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+D / Delete or Backspace |
| Select all / clear the selection | Ctrl+A / Esc (Esc also gives the arrange view the focus) |
| Split at the playhead / at the time selection | S / Alt+S |
| Join / crossfade | J / X |
| Normalise | Ctrl+Shift+N |
| Mute / unmute the selected clips | Ctrl+M |
| Open or close the selected track's automation lanes | A |
| Show / hide take lanes | Alt+T |
| Tools: select, range, pencil, erase, split, glue, mute | 1 to 7 (fixed) |
| Nudge the selection | Left / Right (fixed); Alt+Left / Right by a grid step, with Shift a quarter (fixed) |
| Newer / older take at the playhead (comp) | Alt+Up / Alt+Down (fixed) |

**Piano roll** (while it has the focus)

| Action | Key |
|---|---|
| Select / pencil / erase tool | 1 / 3 / 4 |
| Delete / duplicate the selected notes | Delete or Backspace / Ctrl+D |
| Select every note / clear the selection | Ctrl+A / Esc |
| Snap on or off | G |
| Quantise starts / starts and ends | Q / Shift+Q |
| Coarser / finer grid | [ / ] |
| Transpose up / down a semitone | = (or +) / - |
| Nudge by a grid step (Shift: fine) | Left / Right (fixed) |
| Move up or down a semitone (Ctrl: an octave) | Up / Down (fixed) |

**Audio editor** (pitch mode, while it has the focus)

| Action | Key |
|---|---|
| Tools: pitch, split, drift, vibrato, breath | 1 to 5 (fixed) |
| Move notes up or down a semitone (Shift: 10 cents) | Up / Down (fixed) |
| Mute or unmute the selected notes | M |
| Select every note / clear the selection | Ctrl+A / Esc |

**Not keys**: tap tempo is T while the tempo field is focused; a clip stretches with
Alt+drag of its right edge, and its gain follows a Ctrl+drag on its body
(screens/arrange.md).

Focus model: the arrange view owns focus by default; Tab cycles header controls,
track headers, lanes, dock, side panel, status bar; Esc returns focus to the arrange
and clears selection. Arrow keys inside a focused region move the selection (tracks
up/down, clips left/right).

## 10.5 Tooltips and help

- Every control has a tooltip: name, value if any, shortcut in mono. Appears after
  500 ms, instantly when moving between adjacent controls, never during a drag.
- The status bar's right side shows a one-line description of the control under the
  pointer after 1 s hover ("Pan: drag up/down, double-click to centre").
  **Decision**: on by default for new installs; can be turned off in Preferences.

## 10.6 Notifications and errors

- Transient confirmations (saved, scanned): status bar message, 4 s. An export's
  progress is in the status bar; its outcome is a toast (it carries the measured
  loudness and the gain applied, and offers Show in folder).
- Needs attention (dropouts over threshold, device lost, plugin failed, recording
  write failure, recoverable recordings): toast with an action; persistent for
  errors.
- Blocking decisions (unsaved changes on close, overwrite a file, delete a bus that
  tracks route to): sheet with the consequence in the first line and the destructive
  action styled Danger.
- Error text is three parts in one or two sentences: what happened, what it means,
  what to do. "The Volt was disconnected. Playback stopped; recording was saved up to
  3:12. Reconnect it or choose another device in Audio Setup."
- Never a dialog that only says OK.

## 10.7 Undo feedback

The Undo button's tooltip and the Edit menu show the action name. After an undo the
status bar shows "Undid Move clip" for 2 s. Gestures (fader drags, knob drags, slider
drags) are one undo step each; typing in a field is one step per commit.

## Acceptance

- Every component on every screen implements every state in 10.1.
- An automated test posts the keyboard map's key events to the main window's
  handler and checks the resulting commands, before and after a rebinding
  (`tests/unit_juce/ShortcutsTests.cpp`); another checks no context has two actions
  on one default key.
- Every error path listed in sections 4 to 8 produces the specified toast or sheet
  wording.
