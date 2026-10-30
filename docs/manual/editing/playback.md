---
title: "Playing and moving around"
description: "The transport, locating, zooming, markers and regions, the loop range and the snap grid."
sidebar:
  order: 1
---

This page covers playing a song and finding your way around it: the transport,
moving the playhead, zooming and scrolling, markers and regions, the loop range, and
the snap grid that edits line up to.

## The transport

The transport buttons sit in the middle of the header bar: **Go to start**, **Play**,
**Stop** and **Record**. Beside them, the readout shows where the playhead is in bars
and beats and in minutes and seconds, then the tempo and the time signature.

| Key | What it does |
|---|---|
| Space | Play, or stop |
| Shift+Space | Pause, or carry on from the pause |
| Enter or Home | Go to the start |
| Shift+Enter or End | Go to the end of the last clip |
| R | Record (see [Recording audio](../recording/recording-audio.md)) |
| L | Loop on or off |
| Comma and full stop | Jump to the previous or next marker |

On the numeric keypad, 0 plays and stops, 3 records and 1 goes to the start, as in
Pro Tools. Every key can be changed in **Preferences > Shortcuts**.

### Stop and pause

Stop and pause are different. **Pause** (Shift+Space, or clicking **Play** while it
plays) stops and leaves the playhead where it is, and the next Play carries on from
there. **Stop** (Space) takes the playhead back to where playback started, so you can
listen to the same passage again and again without finding its start each time. After
a pause, Stop goes back to where the first Play started.

If you would rather Stop left the playhead where it stopped, turn off **Return on
stop** in **Preferences > Recording**.

### Moving the playhead

- Click in the ruler, or on empty space in a lane. While the song plays, it carries
  on from there, and Stop then returns to that point.
- Click a field in the readout and type a position. Dragging up or down on part of a
  field (the bar, the beat) nudges it.
- Use the marker keys, comma and full stop (see below).

## Following the playhead

While the song plays, the arrange view turns the page when the playhead nears its
right edge, so the playhead stays in sight. To stop it doing that, right-click the
ruler and untick **Follow playhead**, or turn it off in **Preferences > Editing**.

## Zooming and scrolling

| To | Do this |
|---|---|
| Zoom in or out around the pointer | Ctrl+wheel over the lanes or the ruler |
| Zoom by dragging | Drag up or down in the ruler's bar numbers |
| Zoom in or out | Ctrl+= and Ctrl+- |
| See the whole song | Z, or the **Zoom to fit** button in the header bar |
| Fill the view with the selected clips | Shift+Z |
| Scroll the tracks up and down | The wheel |
| Scroll along the song | Shift+wheel, or a sideways swipe |
| Drag the view around | Hold the middle mouse button and drag |

When you zoom far out, the ruler shows the time in seconds under the bar numbers.

## Markers and regions

A **marker** names a point in the song, such as "Chorus 2", so you can jump to it. A
**region** names a stretch of the song. Both sit in the top row of the ruler, and a
thin line runs down through the lanes from each.

- Press M to add a marker at the playhead. Its name is ready to type: type one and
  press Enter, or press Esc to keep the numbered name ("Marker 3").
- Press Alt+M to add a region. It covers the time selection if there is one (see
  [range editing](tools.md#range-editing)), otherwise the loop range.
- Comma and full stop jump to the previous and next marker. Comma before the first
  marker goes to the start of the song.
- Click a marker to select it and move the playhead to it. Drag it to move it along
  the song; it snaps to the grid.
- Double-click a marker to rename it.
- Press Delete to remove the selected marker.

Right-click the ruler for **Add marker at playhead** and **Add region from loop**
(**Add region from selection** when there is a time selection).

## The loop range

The ruler holds one range, shown as a band across the bar numbers. Turned on with
**Loop** (L), it plays over and over, which is useful for working on one part of a
song. With **Punch** (P) on, recording happens only inside it.

- Drag across the bar numbers in the ruler to set the range.
- While loop is on, drag either end of the band to change it, or drag inside it to
  move it.
- Double-click the bar numbers over a clip to set the range to that clip (the
  selected track's clip first), or over a region to set it to the region.
- With a time selection, its right-click menu's **Loop selection** sets the range to
  it and turns loop on.

**Loop** cannot be turned on until there is a range. Recording with loop and punch
is covered in [Loop, punch and takes](../recording/takes.md).

## The snap grid

When you move or trim a clip, it lines up to the grid of bars and beats. This is
called **snapping**, and it keeps edits in time with the music. The grid follows the
tempo and time signature, including changes on the
[tempo track](tempo.md).

- Press N, or click the **Snap** button in the header bar, to turn snapping on or off.
- Click the arrow on the **Snap** button to choose the grid. **Adaptive** picks bars,
  beats or smaller steps to suit how far you are zoomed in. **Bar**, **Beat**,
  **1/8**, **1/16** and **1/32** are fixed, and two submenus add triplet and dotted
  grids (**1/8 triplet**, **1/16 dotted** and so on).
- Hold Alt while you drag to move freely, off the grid.

While a clip is dragged, it also snaps to things close by: the edges of other clips
on the track, markers, the ends of the loop range and the playhead. A line shows what
it snapped to.

**Preferences > Editing** sets whether snap is on, and which grid is used, when a
project opens.
