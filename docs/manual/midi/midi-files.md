---
title: "MIDI files"
description: "Importing Standard MIDI Files as MIDI tracks, and exporting MIDI clips to one."
sidebar:
  order: 3
---

A Standard MIDI File (`.mid` or `.midi`) holds notes and controller movements,
not sound, so it is the usual way to move a part between programs: a drum pattern
from a pack, a song's parts from another DAW, a melody from notation software.
Northlight reads type 0 files (every part in one track) and type 1 files (a track
per part), and writes type 1.

## Importing a MIDI file

Choose **Import MIDI file...** in the project menu (click the project's name at the
top left), and pick one or more files. They land at the playhead. You can also drag
`.mid` files from Explorer onto the arrange view, and they land where you drop
them.

### What it becomes

- **Each part becomes a MIDI track** with one clip, named after the file's track
  ("Piano", or "Track 3" when the file names none). A file track that uses several
  MIDI channels, as every type 0 file does, is split into a MIDI track per channel,
  named with the channel ("Piano (ch 2)"). Each MIDI track keeps its part's channel
  as its MIDI channel.
- **The clips start at the start of the bar** the playhead (or the drop) is in, and
  run for whole bars, at least one.
- **The instrument.** If you drop the file on an instrument channel or a MIDI
  track, or have one selected when you import, every part plays that instrument
  (on its own MIDI channel, which a multitimbral plugin can tell apart). Otherwise
  Northlight makes the instruments: parts on channel 10, the General MIDI drum
  channel, play a new [Pulsar](../instruments/pulsar.md), and every other part plays
  one new [Aurora](../instruments/aurora.md).
- **What is read**: notes with their velocities, control changes, pitch bend,
  channel pressure and poly pressure. Program changes, system exclusive messages
  and text such as lyrics and markers are left out.
- The whole import is one undo step.

:::tip
A file with a piano, a bass and strings comes in as three MIDI tracks all playing
one Aurora. To give each part its own sound, click a track's chip and choose
**New instrument** (see [Changing what a MIDI track plays](midi-tracks.md#changing-what-a-midi-track-plays)).
:::

### Tempo and timing

Into an empty project (one with no tracks yet), a MIDI file brings its tempo: the
opening tempo, its tempo changes, and its opening time signature go on the
[tempo track](../editing/tempo.md), so the bars line up with the music.

Into a project that already has tracks, the tempo track is left alone, and the
notes keep the timing they had in the file at the file's own tempo. If the file's
tempo differs from the project's, its notes won't sit on the project's grid. Set the
project's tempo to the file's first, or import the file into a new project.

A file Northlight can't use says why: one that is not a Standard MIDI File, a type 2
file (rare: independent patterns), or one with no notes or controllers.

## Exporting MIDI clips

**Export MIDI clips...** in the project menu writes MIDI clips to a file, to take
the parts into another program or to print them as notation. Which clips go in:

1. the MIDI clips selected in the arrange view, if any,
2. otherwise every clip on the selected MIDI track,
3. otherwise every MIDI clip in the project.

Choose where to save it; the name starts as the project's, with `.mid` added if you
leave it out.

### What the file holds

- A type 1 file at 960 ticks per quarter note.
- A first track with the project's name, every tempo change and every time
  signature. A tempo ramp is written as a tempo change every sixteenth note along
  it, since MIDI files have no ramps.
- One track per Northlight track, named after it, on that track's MIDI channel,
  with its clips' notes, control changes, pitch bend and pressure. Muted notes are
  left out.
- Everything at its place in the song: the file starts where the song starts, so
  a clip in bar 9 begins in bar 9 of the file. Notes that run past the end of a clip
  end at the clip's end, as you hear them.

The file holds the notes as they are in the clips, before any
[MIDI effects](midi-effects.md): an arpeggiated chord exports as the chord.
