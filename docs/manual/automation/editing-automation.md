---
title: "Drawing and editing"
description: "Adding, moving and removing automation points, selecting them, and the range tool, pencil and eraser in lanes."
sidebar:
  order: 1
---

An automation lane's line is drawn through **points**: each point is a value at a
moment in the song, and between two points the value moves from one to the other.
This page covers drawing and editing points by hand. To record them by moving a
control instead, see [Recording automation](modes.md). Open a track's lanes first
(see [Automation lanes](lanes.md)).

## How the line plays

- Between two points, the value moves in a straight line. Volume moves evenly in
  decibels, so a fade sounds even rather than rushing at the end.
- Before the first point, the lane holds the first point's value; after the last, it
  holds the last one's.
- A lane with no points plays nothing: the setting stays where its control is.

:::note
Points are joined by straight lines only. For a curve, add more points, or draw it
with the pencil.
:::

## Adding, moving and removing points

These work with the select tool, and with every other tool except range, pencil and
erase.

- **Add:** click on the lane. The point goes where you clicked; keep the button down
  and drag to place it before you let go.
- **Move:** drag a point. Its time snaps to the grid (hold Alt to move it freely), and
  a tag beside it shows the value as you drag. Hold Shift to change only its value,
  keeping its time. A point cannot pass the points either side of it.
- **Remove:** double-click a point, or right-click it and choose **Delete point**.

Each edit is one undo step.

Removing a lane's last point leaves the lane, empty. To empty a lane in one go,
choose **Clear points** from its menu; **Delete lane** removes the lane as well.

## Selecting points

Select points to move, delete or copy several at once. A selection is always in one
lane.

- Click a point to select it.
- Shift+click adds a point to the selection; Ctrl+click adds or removes one.
- Hold Shift or Ctrl and drag across empty space in the lane to draw a box: the points
  inside it are added.
- Esc, or a click outside the lanes, clears the selection.

Drag one of the selected points to move them all. They snap by the point you drag,
Shift keeps their times, and they stay between the unselected points either side.

| Key | What it does |
|---|---|
| Delete | Removes the selected points |
| Ctrl+X | Cuts them |
| Ctrl+C | Copies them |
| Ctrl+V | Pastes at the playhead |

A paste goes into the selected lane, or the lane you last clicked in, starting at the
playhead and replacing the points already there over the pasted stretch. Copied points
can be pasted into another lane, or another track's.

## The range tool in a lane

With the [range tool](../editing/tools.md) (2), drag along a lane to select a stretch
of time in that lane only. It is shaded as a time selection is.

- Delete removes the points inside the stretch. The value at both ends stays as it
  was, so the line does not jump.
- Ctrl+C copies the stretch's exact shape, including the values at its ends, so it
  pastes back the same.
- Ctrl+X does both.

## Drawing with the pencil

With the pencil (3), drag along a lane to draw the line freehand. Points are added
where the pointer goes, a few pixels apart, and the points that were under the stroke
are replaced. The whole stroke is one undo step. Use it for movement that would take
many clicks: a slow swell, or a filter wobbling in time.

## Erasing points

With the erase tool (4), click a point to remove it, or drag across the lane to remove
every point you pass. The sweep is one undo step.

## Automation follows clips and ranges

When you move or copy clips along their track, the automation under them goes with
them (see [Moving and copying](../editing/clips.md#moving-and-copying)). Deleting a
[time range](../editing/tools.md#range-editing) across tracks removes their automation
inside it too, keeping the values at both ends. Both happen while **Move automation
with clips** is ticked in the ruler's right-click menu, as it is unless you untick it.

Changing the tempo keeps every point on its bar and beat (see
[What a tempo change moves](../editing/tempo.md#what-a-tempo-change-moves)).
