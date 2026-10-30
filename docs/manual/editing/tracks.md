---
title: "Tracks"
description: "The kinds of track, adding and selecting them, ordering, folders, colours and freezing."
sidebar:
  order: 0
---

A track is a row in the arrange view: a header on the left with its name and controls,
and a lane on the right that holds its clips. This page covers the kinds of track
Northlight has, and how to add, select, order, group, colour, rename, duplicate and
delete them.

## Kinds of track

Each track's header shows an icon for its kind. Hover over the icon and its tooltip
gives the track's number and kind.

| Kind | What it holds |
|---|---|
| **Audio track** | Audio clips: recordings and imported files. It records from an audio input. |
| **MIDI track** | MIDI clips: notes and controller moves. It makes no sound of its own; it plays an instrument channel. |
| **Instrument channel** | An instrument (Aurora, the Sampler, a VST3 or CLAP synth) and the effects after it. Its lane says which instrument it is and which MIDI tracks play it. |
| **Instrument output** | One extra stereo output of a multi-output instrument, as a track of its own so it can be mixed apart. |
| **Folder track** | Other tracks, so a long song can be tidied into groups such as drums or vocals. |

MIDI tracks and instrument channels are kept apart so that several MIDI tracks can
play one instrument. [MIDI tracks and instruments](../midi/midi-tracks.md) explains
how they fit together, and how an instrument output is made.

Under the tracks come the **buses** and **FX tracks**, then the master. A bus is a
channel that other tracks send their output into, so they can be processed and set
in level together. An FX track holds a shared effect, such as a reverb, that tracks
reach through sends. Their rows have no clips; the lane says what feeds them ("FX
track: 3 channels send here"). They are in the arrange view so their volume and
effects can be [automated](../automation/lanes.md) like a track's. The master is
always the last row. [Routing](../mixing/routing.md) explains buses and sends.

## Adding tracks

Click **+** at the top left of the track headers and choose:

- **Audio track** (Ctrl+T).
- **MIDI track**: **With a new instrument** (pick one from the list), **Playing** an
  instrument channel the project already has, or **Playing nothing yet**. Ctrl+Shift+T
  adds a MIDI track with a new Aurora. A new MIDI track is armed, so you can play it
  from a MIDI keyboard straight away.
- **FX track**: **Reverb**, **Delay** or **Empty**.
- **Bus** (Ctrl+B).
- **Folder track**, which takes the selected tracks into it.

A new track goes right after the selected track, inside the same folder. When the
selected track is a folder, the new one goes at the end of what the folder holds.

A project with no tracks shows **Add audio track**, **Add MIDI track** and **Import
audio…** in the middle of the arrange view. Dropping audio files onto empty space, or
below the last track, makes a track for each file (see
[Importing](../projects/importing.md)).

## Selecting tracks

Click a track's header to select it. Its header and lane are lit.

- Ctrl+click adds a track to the selection, or takes it out.
- Shift+click selects every track from the last one you clicked to this one.
- Up and Down select the track above or below; Shift+Up and Shift+Down extend the
  selection.

Arm, mute, solo and colour act on every selected track, as do Shift+R (arm), Shift+M
(mute) and Shift+S (solo). So does the track menu: right-click one of the selected
headers and its first line says how many tracks it acts on ("3 tracks").

## The track menu

Right-click a track's header (or an empty part of its lane) for its menu. Depending on
the kind of track, it has **Rename**, **Colour**, **Duplicate track**, **Send to**,
**Show automation lanes**, **Freeze track**, **Folder**, **Delete track** and **Add
bus**. A MIDI track's menu also has **MIDI output** and **Plays...**, which picks the
instrument channel it plays.

## Renaming

Double-click a track's name, choose **Rename** from its menu, or press F2 while the
arrange view has the keyboard focus. Type the new name and press Enter; Esc keeps the
old one. When a clip is selected, F2 renames the clip instead.

## Colours

Every track gets a colour from the palette by its position. To choose one, click the
coloured stripe at the left edge of its header, or use **Colour** in the track menu
(which colours every selected track). The menu lists the palette by name with a
swatch; **Default** gives the track its automatic colour back. Clips take their
track's colour unless you give a clip one of its own (see
[Clips](clips.md#names-and-colours)).

## Reordering

Drag a track by its icon, at the left of its name. A line shows where it will land.
When the dragged track is selected, every selected track moves with it.

- Dropped among a folder's tracks, it joins that folder.
- Dropped right under an open folder's header, it goes first in the folder.
- Dropped under a folder's last track, how far right the pointer is decides whether
  it lands in the folder or after it. The line is indented to show the depth.

Dragging a folder brings everything inside it along. Buses and FX tracks reorder
among themselves the same way; the master stays last.

## Folders

A folder track holds other tracks. Its header has an arrow before its name: click it,
or choose **Close folder** from its menu, to fold its tracks away so they take no
room. The folder's lane shows its tracks' clips as thin bars, one row per track, and
says how many tracks it holds. Its **M** and **S** mute and solo every track inside
it.

To put tracks in a folder, select them and choose **+** > **Folder track**, or use the
**Folder** submenu of the track menu: **New folder containing this track**, the name
of an existing folder, or **Not in a folder** to take it out. Folders can hold other
folders; each level indents the headers a little further.

A folder makes no sound of its own, so it has no volume, automation or freeze.
Deleting a folder leaves its tracks where they are, outside any folder.

## Duplicating and deleting

**Duplicate track** makes a copy right after the original, named "Vocals copy" for a
track called Vocals, with its clips, takes, effects, channel strip, sends and
automation. The copy is not frozen. Folders cannot be duplicated.

**Delete track** removes the track and everything on it. Both act on every selected
track, and both can be undone with Ctrl+Z.

## Row heights

The two small buttons at the right of the corner above the headers, **Smaller rows**
and **Larger rows**, set the height of every row. Taller rows show more of each
header: a second line with the input, a meter and the volume, and at the tallest a
third with pan, the output and the sends. To size one track, drag the bottom edge of
its header or lane; double-click that edge to give it the default height again. Drag
the boundary between the headers and the lanes to widen or narrow the headers.

## Freezing

Freezing renders a track to audio in place, so its instrument and effects stop using
the processor until you need to change them. Choose **Freeze track** from the track
menu; a frozen track's lane is veiled and marked FROZEN. See
[Freezing tracks](../mixing/freezing.md).
