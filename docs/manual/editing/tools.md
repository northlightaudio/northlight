---
title: "The tools"
description: "The seven arrange tools, and editing a range of time across tracks."
sidebar:
  order: 3
---

The tools change what a click or a drag in the lanes does. Most editing needs only
the select tool, but the others make some jobs a click instead of a drag and a key:
cutting a recording into phrases, deleting a run of clips, muting every other bar.
This page covers each tool, and range editing, which works on a stretch of time
rather than on whole clips.

## Choosing a tool

The tools are the row of buttons at the top of the track headers, after **+**. Click
one, or press its number. The current tool is lit, and the pointer takes its icon over
the lanes. When the headers are too narrow for all seven buttons, one button shows the
current tool; click it for a menu of them all.

| Tool | Key | A click | A drag |
|---|---|---|---|
| **Select** | 1 | Selects a clip | Moves, trims or fades clips; on empty space, draws a selection box |
| **Range** | 2 | Ends the time selection | Selects a range of time across tracks |
| **Pencil** | 3 | On a MIDI track, makes a clip a bar long | On a MIDI track, draws a new clip |
| **Erase** | 4 | Deletes a clip | Deletes every clip it passes over |
| **Split** | 5 | Cuts a clip at the line | |
| **Glue** | 6 | Joins a clip with the next one | |
| **Mute** | 7 | Mutes or unmutes a clip | Mutes or unmutes every clip it passes over |

The number keys pick tools while the arrange view has the keyboard focus. The
[piano roll](../midi/piano-roll.md) has the same select, pencil, erase, split, glue
and mute tools, with the same keys and icons, for notes.

## Select

The select tool does everything on [Clips](clips.md): click to select, drag a clip's
body to move it, its edges to trim it, and the handles at its top corners to fade it.

## Range

The range tool selects a stretch of time, which can start and end in the middle of
clips. Drag in a lane: the range is shaded through the lanes it covers, and dragging
across several tracks takes them all in. See [Range editing](#range-editing) below.

## Pencil

On a MIDI track, drag through empty space to draw a new, empty clip. It is drawn in
whole grid steps from the grid line where you pressed, in either direction, and a
tooltip shows its length in beats. A click makes a clip a bar long. The new clip is
selected: double-click it to open it in the piano roll and add notes.

On a clip, the pencil works as the select tool. On an audio track it draws nothing.
With any tool, a double-click on empty space on a MIDI track makes a clip a bar long.

## Erase

Click a clip to delete it. Drag to sweep across several: each clip the pointer passes
over dims, and when you let go they are all deleted, as one undo step. The clips of a
comp go together. With [ripple](clips.md#deleting-and-ripple-editing) on, the gaps
close as they do when you press Delete.

## Split

Over a clip, a line shows where the split tool will cut, with the position in a tag
("Split at 5.1.000"). The line sits on the grid line nearest the pointer, or at the
pointer when no grid line falls inside the clip. Click to cut there.

- Hold Shift to cut every track at that point: the line runs through every lane and
  the tag reads "Split all at".
- Hold Alt to cut exactly where the pointer is, off the grid.

On empty space, the split tool works as the select tool: a click moves the playhead
and a drag draws a selection box.

## Glue

Over a clip that can join the next one, an outline shows the single clip they will
become. Click to join them. A MIDI clip joins the next MIDI clip on its track across
any gap. An audio clip joins the next one only when it carries straight on from it, as
the two halves of a split do. Over a clip that cannot join, a tag says why: "Joins
only a clip that continues this one" or "No clip after this one to join". A muted clip
and a playing one never join each other.

## Mute

Click a clip to mute it, or click a muted clip to unmute it. Drag to sweep across
several: every clip the sweep passes gets what the first one got, so a sweep that
starts on a playing clip mutes the rest. One sweep is one undo step. Ctrl+M mutes or
unmutes the selected clips with any tool.

## Clicks on a selection

With erase, split, glue and mute, a press anywhere on a clip uses the tool: the edges
and corners do not trim or fade. A press on a clip that is one of several selected acts
on the whole selection, so you can select a row of clips and mute, glue or erase them
with one click.

## Range editing

Range editing works on a stretch of time across tracks, whatever clips it cuts
through. Use it to take a bar out of a whole arrangement, to copy a chorus to later in
the song, or to loop a passage.

To select a range, use one of these:

- With the range tool (2), drag in a lane. Drag up or down as you go to take in more
  tracks.
- With any tool, hold Shift and drag across the bar numbers in the ruler. The range
  covers the selected tracks.

A click with the range tool, or any other press in a lane, ends the time selection.
Esc clears it too.

### Editing the range

| Key | What it does |
|---|---|
| Delete | Deletes the range from those tracks |
| Ctrl+X | Cuts the range: copies its parts, then deletes them |
| Ctrl+C | Copies the parts of the clips inside the range |
| Ctrl+V | Pastes at the playhead |
| Alt+S | Splits the clips at both ends of the range |
| Alt+M | Adds a region over the range |

When a range is deleted, clips inside it go, and clips that cross one of its ends are
cut there. Automation keeps its value at both ends, so nothing jumps. With
[ripple](clips.md#deleting-and-ripple-editing) on, everything after the range on those
tracks moves back to close the gap.

Pasted clips keep their distance from the start of the range they were copied from,
so a copied passage lands with the playhead where its start was.

### The range menu

Right-click inside the range for its menu: **Delete range** (**Ripple delete range**
with ripple on), **Cut**, **Copy**, **Paste at playhead**, **Split at range**, **Loop
selection** (the loop range takes the selection and loop turns on), **Add region from
selection** and **Clear selection**.

## Tools in automation lanes

In [automation lanes](../automation/lanes.md), the range tool selects a stretch of
one lane, the pencil draws a curve freehand and the eraser removes points. The other
tools work there as the select tool. See
[Drawing and editing automation](../automation/editing-automation.md).
