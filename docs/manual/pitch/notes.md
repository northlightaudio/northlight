---
title: "Editing notes"
description: "Selecting, moving, splitting and joining the notes of a vocal."
sidebar:
  order: 1
---

Once Northlight has found the notes in a vocal (see
[Vocal pitch editing](index.md)), you can move each one to another pitch, split a
note it joined by mistake, join two it split, and mute or turn down single notes.
This page covers those edits, the tools, and moving around the editor.

## The tools

The tools sit along the top of the audio editor in pitch mode, after the **Wave** /
**Pitch** switch. The lit one is current. Click a tool or press its number while the
editor has the keyboard focus (click in the editor first):

| Key | Tool | What it does |
|---|---|---|
| 1 | **Pitch** | Drag a note up or down; drag a join to set its slide; double-click to split or merge |
| 2 | **Split** | Click a note to split it there; click a join to merge the two notes |
| 3 | **Drift** | Drag a note down to steady its slow drift, up to bring it back |
| 4 | **Vibrato** | Drag a note down to flatten its vibrato, up to deepen it |
| 5 | **Breath** | Click a breath to mute or unmute it |

Drift, vibrato and slides are covered in [Drift, vibrato and slides](shaping.md),
and the breath tool in [Breaths](breaths.md). With nothing selected, the info bar
under the editor says what the current tool does.

## Selecting notes

- Click a note to select it. Shift+click adds a note; Ctrl+click adds or removes one.
- Drag across empty space to draw a box around several notes.
- Left and Right select the previous or next note; with Shift they extend the
  selection.
- Ctrl+A selects every note in the clip, and Esc clears the selection.

Clicking a note also plays it, so you can hear what you are about to change. The
speaker button at the top right (**Hear a note when it is clicked**) turns this off
and on; it starts on.

The selected notes show in the [Inspector](shaping.md#the-inspector) (F5), where you
can type exact values.

## Moving notes

With the **Pitch** tool, drag a note up or down. It moves in whole semitones, and a
tooltip shows the note it will land on. Hold Alt while dragging to move it freely, in
cents. Every selected note moves together, by the same amount.

From the keyboard, Up and Down move the selected notes a semitone; with Shift, they
move 10 cents, for small corrections.

A moved note keeps its vibrato and its drift, and the slides into and out of it
follow, so the singer's phrasing comes along. The editor is made for the moves
tuning needs, a semitone or two; the further a note moves, the less natural it is
likely to sound.

To move a note to the nearest semitone exactly, right-click it and choose **Snap to
the nearest semitone** (with several notes selected, **Snap the notes to
semitones**). To tune many notes at once with more care for how they sound, use
[Correct](correct.md) instead.

## Splitting and joining

Sometimes the analysis treats two sung notes as one (a quick change of pitch on one
syllable), or splits one note in two (a strong vibrato or a scoop). To fix it:

- **Split:** with the **Pitch** tool, double-click a note where it should split. Or
  pick the **Split** tool (2) and click the note. The scissors cursor shows where it
  will cut.
- **Join:** where two notes touch, the point between them is a **join**. Hovering
  over it shows a small handle. Double-click the join with the **Pitch** tool, or
  click it with the **Split** tool, to merge the two notes. You can also right-click
  a note and choose **Merge with the next note**.

After a split or a join, Northlight works out each new note's centre again from what
was sung, and keeps any move you had made: a merged note takes the move of the longer
of the two.

## The note menu

Right-click a note for these, which act on every selected note:

- **Snap to the nearest semitone** (or **Snap the notes to semitones**).
- **Reset pitch, drift and vibrato** (or **Reset the notes' pitch**): puts the
  pitch, drift and vibrato back as they were sung.
- **Mute** / **Unmute**. M does the same from the keyboard. A muted note is drawn
  faint and is silent.
- **Louder (+3 dB)**, **Quieter (−3 dB)** and **Gain to 0 dB**, for a word that
  sticks out or gets lost.
- **Merge with the next note** (with one note selected).
- **Select all notes**.

## Moving around

| To | Do |
|---|---|
| Scroll up and down the pitches | Mouse wheel |
| Scroll in time | Shift+wheel |
| Zoom in time, around the pointer | Ctrl+wheel |
| Zoom the pitches (taller or shorter rows) | Ctrl+Shift+wheel, or Alt+wheel |
| Pan in any direction | Drag with the middle mouse button |

Two buttons at the top right change how the editor follows the arrange view. The
link button (**Link zoom to the arrange view**) keeps its zoom and position matched
with the arrange view's. The follow button (**Follow the playhead**) turns the page as
the playhead moves, so the notes being played stay in view.

## Keys

These work while the audio editor has the focus, in pitch mode:

| Action | Key |
|---|---|
| Pitch, split, drift, vibrato, breath tool | 1 to 5 |
| Move the selected notes up or down a semitone | Up / Down |
| Move them 10 cents | Shift+Up / Shift+Down |
| Select the previous or next note | Left / Right (Shift extends) |
| Mute or unmute the selected notes | M |
| Select every note | Ctrl+A |
| Clear the selection | Esc |

M, Ctrl+A and Esc can be changed in
[Preferences > Shortcuts](../reference/shortcuts.md); the tool numbers and arrow keys
are fixed.
