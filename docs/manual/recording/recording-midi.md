---
title: "Recording MIDI"
description: "Record from a MIDI keyboard as takes, or merge and replace notes in a clip."
sidebar:
  order: 3
---

Recording MIDI captures what you play on a MIDI keyboard or controller: which notes,
how hard, how long, and controller moves such as the sustain pedal and mod wheel.
Unlike audio, the result is a clip of notes you can edit afterwards in the
[piano roll](../midi/piano-roll.md): fix a wrong note, tighten the timing, change the
sound. This page covers how MIDI recording works and the three ways a MIDI track can
record.

## Recording a MIDI take

1. Connect your keyboard and turn its input on under MIDI inputs in **Audio Setup**
   (see [Setting up audio](../getting-started/audio-setup.md)).
2. Select a MIDI track, or add one with an instrument (see
   [MIDI tracks](../midi/midi-tracks.md)).
3. In the track header's input menu, leave **All MIDI** to take notes from every
   enabled input, or choose your keyboard by name.
4. Arm the track with **R** (or Shift+R). With monitoring on **Auto**, the default, you
   can now play the instrument.
5. Press Record (R), play, and press Space to stop.

The notes appear in the record colour as you play, on the track and in the piano roll,
and settle into the track's colour when you stop. A message says how many MIDI clips
were recorded, or "Recorded no MIDI: nothing was played" if no notes came in.

Monitoring works as it does for audio (see
[Input monitoring](recording-audio.md#input-monitoring)): in **Auto** you hear the
instrument while the track is armed and the song is stopped or recording. Set it to
**On** to play the instrument without arming the track.

The click, count-in, pre-roll, loop and punch all work for MIDI as they do for audio
(see [Recording audio](recording-audio.md) and [Loop, punch and takes](takes.md)). You
can record MIDI and audio tracks in the same pass.

## How MIDI recordings are placed

You play along to what you hear, and what you hear leaves the speakers a little late
(see [Latency and monitoring](latency.md)). Northlight moves recorded notes back by
that output delay, so they land where you meant them. It doesn't take off the input
delay that audio recordings lose, because a MIDI note reaches Northlight as soon as you
press the key.

## Takes, Merge and Replace

What a pass does to the track is set in the **Recording** card of the Inspector. Select
the MIDI track (click its header, not a clip) and choose one of three modes:

| Mode | What a pass does |
|---|---|
| **Takes** | Each pass is a take. Loop passes become takes you comp, as for audio. The default. |
| **Merge** | The notes join the clip under the pass, adding to what is there. |
| **Replace** | The notes replace what the clip under the pass had over the stretch you recorded. |

The mode belongs to the track, and is saved with the project.

### Takes

In **Takes** mode, MIDI recording follows the same rules as audio: loop passes, a
punch, or a pass over an existing take group make takes in a take group, and a single
pass anywhere else is a new clip. The take lanes show each take's notes in miniature,
and you comp them by swiping exactly as for audio: see
[Loop, punch and takes](takes.md#take-lanes). Each piece of the comp holds the notes
that start in it; a note held across a boundary is cut there. There are no crossfades,
as there is nothing to click.

Use Takes when you play a whole part several times and want to pick the best.

### Merge

**Merge** is overdubbing: the notes you play are added to the clip under the pass, and
the clip grows if the pass runs past its ends. With **Loop** on, every pass adds to the
same clip, so you can build up a drum pattern one sound at a time: kick on the first
pass, snare on the second, hi-hats on the third.

### Replace

**Replace** records over a stretch of a clip. Everything the clip had between where
recording started and where it stopped is removed and replaced with what you played.
Of the clip's controller moves (sustain pedal, mod wheel and so on), only the
controllers you used in the pass are replaced; the others stay. Use it to redo a few
bars of a part, ideally with **Punch** on so the stretch is exactly the range you set.

In both Merge and Replace, a pass with no clip under it becomes a new clip.

## MIDI effects are not recorded

If the track has [MIDI effects](../midi/midi-effects.md), such as an Arpeggiator or a
Chord effect, you hear them while you play, but the recording keeps the notes you
played, not the notes the effects made. The effects keep working on playback, so you
can change their settings, or take them off, after recording. Hold a chord through the
Arpeggiator and the clip holds the chord; the arpeggio is made again each time it plays.
