---
title: "Inputs"
description: "Choosing what a track records from, and setting up stereo inputs."
sidebar:
  order: 0
---

An input is where a track's sound comes from: a socket on your audio interface, or a
pair of them. Each audio track records from one input, which you pick in its header.
This page explains the choices, how they are named, and how to set up stereo inputs
for sources that have a left and a right side.

## Picking a track's input

The input menu is on the second row of a track's header, under its name. Click it and
choose an input. It lists:

- **No input**: the track records nothing and monitors nothing.
- Under **Stereo**, the stereo inputs you have set up in Audio Setup, by name.
- Under **Mono**, every input channel of your interface that is turned on, one by one.

The **Stereo** and **Mono** headings only appear when there are stereo inputs to list;
with none, the menu is a plain list of channels.

Changing a track's input is not an undo step, so Ctrl+Z won't change it back, but it
does count as a change to the project and is saved with it. Arming and monitoring
work the same way (see [Recording audio](recording-audio.md)).

:::tip
A very short track hides its second row, and with it the input menu, **R** and the
monitor button. Drag the edge under the track to make it taller.
:::

## How inputs are named

A mono input is named by the number of its socket on the interface: **In 3** is the
interface's third input, whichever others are turned on. That matches the numbers in
**Audio Setup** and on most interfaces, so **In 1** is the socket labelled 1.

Only inputs that are turned on in Audio Setup appear. If the one you want is missing,
open **Audio Setup** (see [Setting up audio](../getting-started/audio-setup.md)) and
tick it under **Channels**. Each enabled input there has a small meter beside it, so
you can play or sing and watch which one moves.

A stereo input appears by the name you gave it. Hover over the menu to see which
sockets it reads, for example "Input: Vocals (In 1 + In 2)".

## Mono inputs

Most sources are mono: one microphone on a voice or a guitar amp, a guitar or bass
plugged straight in. Choose the single input it is plugged into. Northlight records a
mono file, and you place it in the stereo mix with the track's pan.

Every enabled input can be chosen on its own, so mono recording needs no setting up.
Two tracks can use the same input, for example to record a guitar through two
different sets of effects at once.

## Stereo inputs

Some sources come in as two channels that belong together: a keyboard or synth with
left and right outputs, a pair of overhead microphones on a drum kit, a stereo
microphone. Recording them as one stereo track keeps both sides in one file, exactly
in step, and lets you edit them as one clip.

To record in stereo, first pair the two inputs into a **stereo input**:

1. Open **Audio Setup**.
2. Under **Stereo inputs**, click **Add stereo input**. A row appears for the first pair
   of neighbouring inputs not used yet, named after them, such as "Stereo 1/2".
3. Type a name that says what is plugged in, such as "Keys" or "Overheads".
4. Choose the left input under **L** and the right under **R**. Picking an input that is
   turned off turns it on.
5. Close Audio Setup, and choose the new stereo input in a track's input menu.

A track with a stereo input records one stereo file and monitors in stereo. Its pan
control works as a balance between the two sides, as it does for a stereo clip.

Each name must be different: a repeated name gets a number added ("Keys 2"), and an
empty name goes back to what it was. **Remove** deletes a stereo input. Stereo inputs
can only be set up while a device is open; otherwise the section says so and
**Add stereo input** is unavailable.

### Stereo inputs belong to the interface

Stereo inputs are kept for each audio interface on this computer, not in a project.
Set them up once and every project you record with that interface can use them. If
you switch to another interface and back, its stereo inputs come back as you left
them.

A project only remembers the name of the stereo input each track chose. If you open
it with an interface that has no stereo input of that name (on another computer, say),
the track's menu shows it as missing, for example "Vocals (missing)", and the track
records nothing from it. Add a stereo input with that name in Audio Setup, or choose
another input for the track. Northlight never quietly swaps in different sockets, so
you won't record the wrong ones by surprise.

## MIDI tracks

A MIDI track has a MIDI input menu in the same place. It offers **All MIDI**, which
takes notes from every MIDI input turned on in Audio Setup, or one MIDI device by
name. A device the track chose that is not connected now shows as "(absent)". See
[Recording MIDI](recording-midi.md).
