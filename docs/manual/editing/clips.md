---
title: "Clips"
description: "Selecting, moving, trimming, fading, splitting, joining and muting clips, and ripple editing."
sidebar:
  order: 2
---

A **clip** is a piece of audio or MIDI on a track's lane. An audio clip plays part of
an audio file, so trimming or splitting it never changes the file itself: you can
always drag an edge back out. This page covers the edits you make to clips with the
select tool, the keys and the clip menu. The other tools are on
[The tools](tools.md).

## Selecting clips

- Click a clip to select it. Shift+click adds a clip; Ctrl+click adds or removes one.
- Drag across empty lane space to draw a box: the clips it touches are selected.
- Ctrl+A selects every clip.
- Ctrl+Left and Ctrl+Right select the previous or next clip on the selected track;
  with Shift they add it to the selection.
- Esc clears the selection. A click on empty space clears it too, and moves the
  playhead there.

The clips that make up a comp (built from [takes](../recording/takes.md)) select
together, so the comp moves and deletes as one.

## Moving and copying

Drag a clip to move it, with every other selected clip. Clips move between tracks of
their own kind: audio onto audio tracks, MIDI onto MIDI tracks. While you drag, the
original stays faint, a tooltip shows the new start in bars and beats, and the clip
[snaps](playback.md#the-snap-grid) to the grid. Hold Alt to move it freely.

Hold Ctrl when you let go to drop a copy instead: the originals stay where they were
and the copies are selected.

Automation moves with clips. When clips move along their own track, the
[automation](../automation/lanes.md) under them goes with them, replacing the points
where they land, so a volume ride stays with the phrase it belongs to. To move clips
on their own, right-click the ruler and untick **Move automation with clips**.

To move the selected clips by small steps, press Left or Right (or Alt+Left and
Alt+Right): each press moves them by one grid step. Alt+Shift+Left and Alt+Shift+Right
move them by a quarter of a grid step.

## Trimming

Drag a clip's left or right edge to shorten or lengthen it. The audio stays where it
is in time: trimming only changes how much of it the clip plays. A tooltip shows the
new length, and the edge snaps to the grid and to nearby clip edges.

## Fades

A **fade** brings a clip's level up from silence at its start (a fade-in) or down to
silence at its end (a fade-out). Short fades stop clicks where a clip starts or stops
in the middle of a sound; longer ones make a sound arrive or leave gradually. Fades
are for audio clips.

1. Hover over an audio clip: a small square handle appears at each top corner.
2. Drag a handle sideways to set the fade's length.
3. Drag up or down at the same time to shape its curve. Up makes it **fast** (the
   level comes up soon, then levels off); down makes it **slow** (it stays quiet for
   longer, then rises). The curve stops at **linear**, the default, on the way past.

The tooltip shows the length and the curve ("Fade 0.50 s  fast 40%"). The fade is
drawn over the clip, and the waveform under it is scaled by it, so a faded edge looks
as quiet as it sounds.

## Crossfades

When two audio clips on a track overlap, a **crossfade** fades the first out while the
second fades in, over the overlap, so the join is smooth. Select both clips and press
X, or choose **Crossfade** from the clip menu. To change it afterwards, drag the fade
handles of either clip.

## Clip gain

**Clip gain** turns one clip up or down before the track's fader, which is useful when
one phrase is quieter than the rest. Hold Ctrl+Shift and drag an audio clip up or down:
each pixel is a quarter of a decibel, from -60 to +24 dB. The waveform grows and
shrinks to match, and the clip's header shows the gain.

**Normalise** (Ctrl+Shift+N, or the clip menu) sets each selected audio clip's gain so
its loudest peak reaches -0.3 dB. It changes only the clip's gain, never the file.

## Splitting and joining

- S splits the selected clips at the playhead. With no clip selected, it splits every
  clip under the playhead, on every track.
- Alt+S splits at both ends of the time selection, or of the loop range when there is
  no time selection.
- **Split here** in the clip menu splits where you right-clicked.
- J joins the selected clips; **Join with next** in the clip menu joins a clip with
  the one after it.

MIDI clips join across any gap, taking the notes of both. Audio clips join only when
the second carries straight on from the first: the same file, touching, with nothing
missing between them and the same gain, stretch and pitch settings. In practice that
means a split put back together. The [split and glue tools](tools.md) do the same with
a click.

## Duplicating, copying and pasting

- Ctrl+D puts a copy of each selected clip right after it.
- Ctrl+C copies the selected clips, and Ctrl+X cuts them.
- Ctrl+V pastes at the playhead, starting on the selected track. Clips keep their
  spacing, and pasted clips from several tracks keep their places on the tracks below.
  A clip only lands on a track of its own kind.

## Muting clips

A muted clip stays where it is and can still be edited, but plays nothing. It is drawn
hatched, with a faint waveform or faint notes. Press Ctrl+M to mute the selected
clips, or to unmute them when they are all muted. The clip menu has **Mute clip**, and
the [mute tool](tools.md) mutes clips with a click or a sweep.

## Deleting, and ripple editing

Delete (or Backspace) removes the selected clips.

**Ripple** editing closes the gap a deletion leaves: the later clips on the track move
back to fill it. It is useful when you cut a mistake out of a spoken recording, or a
bar out of an arrangement, and want everything after it to follow. Press Shift+N, or
click the **Ripple** button in the header bar, to turn it on or off. With ripple on,
deleting audio clips moves the later audio clips on their tracks back by the length of
each one deleted. Deleting a [time range](tools.md#range-editing) with ripple on closes
the gap on every track the range covers, MIDI clips included (and automation, while
**Move automation with clips** is on).

**Preferences > Editing** sets whether ripple is on when a project opens.

## Names and colours

A clip is named after its file or its recording ("Vox 3"). To rename it, select it and
press F2, or choose **Rename...** from the clip menu: type over the name in its header
and press Enter, or Esc to leave it.

A clip takes its track's colour. To give the selected clips a colour of their own,
choose **Colour** from the clip menu; **Default** gives them the track's colour back.

## The clip menu

Right-click a clip for its menu. It acts on every selected clip, and its first line
names the clip ("+2" when two more are selected). Each item shows its key, and an item
that would do nothing is greyed out.

- **Split here**, **Join with next** (**Join** for several), **Crossfade**, **Mute
  clip**, **Duplicate**, **Rename...**, **Colour** and **Normalise**.
- **Cut**, **Copy** and **Delete**.
- For audio: **Stretch to project tempo...**, **Follow tempo changes**, **Transpose**
  and **Reset stretch and pitch** (see [Tempo and time](tempo.md#stretching-and-transposing-clips)),
  and **Open in Sampler**, which makes a new [Sampler](../instruments/sampler.md) track
  that plays the part of the file the clip uses.
- For MIDI: **Open in piano roll**.
- **Track...** opens the track's menu.

## Opening a clip

Double-click a MIDI clip to open it in the [piano roll](../midi/piano-roll.md), or an
audio clip to open it in the Audio Editor, where you can
[edit the pitch of a vocal](../pitch/index.md). Double-click empty space on a MIDI
track to make a new clip a bar long there.

When a clip's file is missing, the clip says so and shows **Locate…**: click it to
find the file (see [Media](../projects/media.md)).
