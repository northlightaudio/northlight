---
title: "Undo"
description: "Undo and redo, what counts as one step, and what is not an undo step."
sidebar:
  order: 5
---

Almost every change you make to a project can be undone, so you can try an edit and
take it back if it does not work. This page covers undo and redo, what makes one undo
step, and the few changes that are kept out of the history on purpose.

## Undo and redo

| Key | What it does |
|---|---|
| Ctrl+Z | Undo the last change |
| Ctrl+Y or Ctrl+Shift+Z | Redo what you undid |

The **Undo** and **Redo** buttons sit at the left of the header bar, beside the
project name. Hover over **Undo** and its tooltip names the step it will take back
("Undo Move clip"), so you know what is about to change.

Making a new change after undoing clears what you could have redone.

## What one step is

One action is one step, however many things it changes:

- A whole drag: moving clips, a trim, a fade, a fader or knob move from press to
  release.
- An edit to a selection: deleting ten clips, colouring five tracks, muting a sweep of
  clips with the mute tool.
- Loading a preset: every parameter it sets comes back with one undo.
- A recording: everything one press of Record made, on every armed track. Undo removes
  the new clips; the recorded files stay in the project's media folder.
- One pass of written automation (see [Recording automation](../automation/modes.md)).

Changes to how things are shown are undo steps too: a track's height, opening or
closing a folder or a track's automation lanes, collapsing or hiding a lane, a name or
a colour. So are mute, solo and the automation mode.

## What is not an undo step

How a track is set up for recording stays out of the history, as in most DAWs:

- arming a track
- input monitoring (off, auto or on)
- the track's audio or MIDI input

If you arm a track and then press Ctrl+Z, the edit you made before arming is undone,
and the track stays armed. These changes still count as changes to the project: they
are saved with it, and closing asks you to save them.

Changes made in a hosted plugin's own window (a VST3 or CLAP plugin's editor) are
kept by the plugin, not by the history, so Ctrl+Z does not take them back. They are
still saved with the project. Loading a preset from the plugin window's preset menu is
an undo step. Moving the controls of Northlight's built-in effects and instruments,
in their windows or in the Editor panel, is undoable as any other edit.

Nothing that only changes your view of the project is an undo step:

- selecting clips, tracks, markers or automation points
- the time selection
- zooming and scrolling
- the current tool
- snap, the snap grid and ripple
- the loop range, and turning loop, punch, click and count-in on or off
- moving the playhead
- showing or hiding the tempo track, and **Follow playhead**
- panels, and **Preferences**

## How far back undo goes

The history keeps the last 1,000 steps, and drops the oldest when it grows past about
256 MB, which only very large edits (deleting many long tracks) come near. It lasts
until the project is closed: it is not saved with the project.

Undo knows where you last saved. If you undo or redo back to that point, the project
counts as saved again and closing does not ask.

:::note
There is no list of undo steps to jump back through yet. Press Ctrl+Z as many times as
you need; the **Undo** tooltip names each step before you take it.
:::
