---
title: "Importing"
description: "Bring audio files and Standard MIDI Files into a project."
sidebar:
  order: 3
---

Importing brings sound or notes made elsewhere into a project: a drum loop, a sample, a
part someone sent you, or a MIDI file from another program. This page covers the audio
formats Northlight reads, the ways to bring files in, and what happens to a MIDI file.

## Audio formats

Northlight opens these audio files:

| Format | Extensions |
|---|---|
| WAV, including Broadcast WAV | `.wav`, `.bwf` |
| AIFF | `.aif`, `.aiff` |
| FLAC | `.flac` |
| Ogg Vorbis | `.ogg` |
| MP3 and Windows Media audio, through Windows' own decoders | `.mp3`, `.wma` |

Mono and stereo files both work, at any bit depth. A file at a different sample rate
from the project's is converted as it loads, so it plays at the right speed and pitch;
the file itself is not changed.

WAV, AIFF and FLAC are lossless, and the best choice for anything you will mix. MP3 and
Ogg files are compressed and have already lost some detail; use them for reference
tracks or rough ideas.

:::note
While **Copy media** is on (**Preferences > General**, on by default), every imported
file is copied into the project's media folder and the project plays the copy. See
[Media and missing files](media.md).
:::

## Importing from the menu

Choose **Import audio** from the project menu (Ctrl+I) and pick one or more files.
Each file goes on a new audio track of its own, named after the file, starting at the
beginning of the song.

The empty arrange view of a new project has an **Import audio** button that does the
same.

## Dragging files in

Drag audio files from Explorer onto the arrange view. While you drag, an outline shows
where the clips will land, with the file name:

- Over an **audio track**, the files land on that track at the position under the
  pointer, snapped to the grid. Several files go one after another.
- Over any other track, or below the last track, each file makes a new track
  ("3 new tracks" shows in the track list while you drag).

Drop a `.northlight` project file anywhere on the window to open it instead (see
[Saving and opening](saving.md#opening)).

## From the browser

The **Files** tab of the **Browser** (F4) lists audio files in the project's folder,
your Music folder, and any folders you add with its **+** button. Click a file to hear
it, Escape to stop. Double-click it, or press Enter, to put it on a new track at the
playhead, or drag it onto a track.

## MIDI files

A Standard MIDI File (`.mid` or `.midi`) holds notes, not sound. Import one with
**Import MIDI file** in the project menu, or drag it onto the arrange view.

Northlight splits the file into parts, one for each track of the file and each MIDI
channel within it, and puts each part on a MIDI track of its own, named after the
file's track (with "(ch 2)" and so on after it when a track used several channels). The
clips start at the bar where the import lands (the playhead's bar, or the bar you
dropped on) and run whole bars.

Which instrument plays them:

- Dropped on a MIDI track or an instrument channel, every part plays that instrument.
- Imported from the menu while a MIDI track or instrument channel is selected, every
  part plays the selected track's instrument.
- Otherwise the parts share a new [Aurora](../instruments/aurora.md), except a part on
  MIDI channel 10, the General MIDI drum channel, which gets a new
  [Pulsar](../instruments/pulsar.md) drum kit of its own.

Change any track's instrument afterwards (see [MIDI tracks](../midi/midi-tracks.md)).

Into an empty project, a MIDI file also brings its tempo, tempo changes and time
signature, so the parts line up with the bars. In a project that already has tracks,
the project keeps its own tempo, and the notes keep their timing in seconds from the
file. The import is one undo step.

See [MIDI files](../midi/midi-files.md) for exporting MIDI clips to a file.
