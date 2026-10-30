---
title: "Recording automation"
description: "The automation modes Off, Read, Touch, Latch and Write, and recording moves from the mixer and from plugin windows."
sidebar:
  order: 2
---

Drawing points is precise, but some moves are easier to perform than to draw: riding
a vocal's level through a verse, sweeping a filter by ear. Northlight can record what
you do with a control while the song plays and turn it into automation. A channel's
**automation mode** decides whether its lanes play, and whether moving a control
records. This page explains the five modes and when to use each.

## Choosing a mode

Each channel has its own mode; a new one starts in **Read**. Right-click the **A**
button in its header and choose from the **Mode** list, where each mode has a few words
on what it does. The **A** is coloured by the mode, so you can see at a glance which
channels will record.

Changing the mode is an undo step, like any other edit.

## The five modes

| Mode | The lanes | Moving a control while the song plays |
|---|---|---|
| **Off** | Ignored | Changes the setting, as with no automation |
| **Read** | Play | Records nothing |
| **Touch** | Play | Records while you hold the control, then goes back to the lane |
| **Latch** | Play | Records from when you first move it until you stop |
| **Write** | Do not play | Every lane on the channel is recorded over from Play to Stop |

### Off

The lanes are ignored and every setting stays where its control is. Use it to hear a
channel without its automation, to compare, or while you set a new static level. The
lanes and their points are kept.

### Read

The lanes play, and nothing you do records. This is the normal mode once the
automation is how you want it: it cannot be changed by a stray touch.

### Touch

The lanes play until you take hold of a control. While you hold it (a fader dragged,
a knob turned), what you do replaces the lane. When you let go, the setting glides back
to the line that was already there, over a twentieth of a second.

Use Touch to fix one part of a pass: play through, grab the fader only where the
vocal needs help, and let go. Everything you did not touch stays as it was.

### Latch

Latch starts like Touch, but when you let go it keeps the last value you set, writing
it into the lane until you stop the song.

Use Latch when a change should last: bring the guitar down for the bridge and leave it
there, without holding the mouse until the end.

### Write

From the moment you press Play, every lane on the channel is recorded over, with each
setting's current value, until you stop. Moving a control writes its moves; a control
you leave alone writes a flat line at where it is. The lanes do not play in Write.

Use Write to record a fresh pass from scratch, or to flatten a stretch of automation
you no longer want. Because it records over every lane on the channel, switch the
channel back to Read or Touch once the pass is done.

## Recording a pass

1. Choose **Touch**, **Latch** or **Write** for the channel.
2. Play the song (you do not need to record audio).
3. Move the control: the fader or pan knob in the mixer or the track's header, a
   control on an effect or the channel strip in the Editor panel, or a knob in a
   plugin's own window.
4. Stop.

What you recorded appears in the lane as points, and the track's lanes open to show
it, including a lane that was hidden or collapsed. A control with no lane yet gets one
when the pass ends.

Each pass is one undo step, so Ctrl+Z takes back a whole take of a fader ride. With
loop on, each time the playhead wraps to the start of the loop ends one pass, and
the next loop round starts another: you can keep riding a fader round the loop and
undo the rounds you did not like.

The setting is heard as you move it, and recording sets points only while the
song plays. Moving a control while stopped changes its value as usual.

## Recording from a plugin's own window

Moves made in a VST3 or CLAP plugin's own window record the same way, as long as the
channel is in Touch, Latch or Write and the song is playing. In Touch, the pass ends
when the plugin says you let go of its control. Northlight tells a plugin's own
reports of what a lane sent it apart from your moves, so playback does not record
itself.

When nothing is being recorded, a change in the plugin's window is kept by the plugin
and is not an undo step (see [Undo](../editing/undo.md)). It is still saved with the
project.

Built-in effects and instruments record from their windows and from the Editor panel
as any control does.
