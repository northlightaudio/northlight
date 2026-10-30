---
title: "Freezing tracks"
description: "Render a track's instrument and effects to audio, to free up processing."
sidebar:
  order: 7
---

Instruments and effects take processing power, and a big project can ask more of your
computer than it can give, which shows as crackles and dropouts. **Freezing** a track
renders it to an audio file once, with its instrument, inserts and channel strip, and
from then on plays that file instead. The frozen track's plugins stop working, so the
power they used is free for the rest of the project. Unfreeze it whenever you want to
change it again.

## Freezing a track

1. Right-click the track's header in the arrange view.
2. Choose **Freeze track**. With several tracks selected, the item reads **Freeze
   tracks** and freezes them all.

Northlight renders the track in the background, from the start of the song to its end
plus three seconds for reverb and release tails, and says "Froze Bass: it plays its
render until you unfreeze it." The file goes in a Freeze folder in the project's media
folder.

A frozen track has a snowflake by its name, and its lane reads **FROZEN** over its
clips.

## What a frozen track keeps

The render holds what the track does *before* its fader: its clips or MIDI, the
instrument, the inserts, the channel strip and their automation. Everything after that
stays live, so you can carry on mixing it:

- the fader, pan, mute and solo, and their automation;
- its sends, so it still feeds its reverb;
- its output.

## Unfreezing and editing

Right-click the header and choose **Unfreeze track**. The track runs its instrument and
effects again, at once, as they were. Freezing and unfreezing are undo steps.

While a track is frozen, its plugins don't run, so changes to its clips, notes,
instrument or inserts aren't heard. To change any of those, unfreeze the track, make
the change, and freeze it again. Audio files can't be dropped onto a frozen track.

## What can't be frozen

- **Tracks that make no sound of their own**, such as a MIDI track, which plays an
  instrument channel. Freeze the instrument channel instead; the MIDI track's notes are
  part of its render.
- **Buses, FX tracks and the master**: only tracks can be frozen.
- **An instrument whose extra outputs feed output tracks** (a drum instrument with each
  drum on its own track, for example). This isn't supported yet.
- Only one freeze runs at a time, and not while an export is running.

If a track can't be frozen, a message says why.
