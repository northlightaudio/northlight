---
title: "Recording audio"
description: "Arm a track, choose how you hear its input, and record."
sidebar:
  order: 1
---

Recording puts what comes in through your audio interface onto a track as a new clip.
You choose which tracks record by arming them, choose whether you hear yourself
through Northlight while you play, and then press Record. This page covers each step,
the click and count-in that help you start in time, and where the files go.

## Recording your first take

1. Plug in your microphone or instrument and check that its input is turned on in
   **Audio Setup** (see [Setting up audio](../getting-started/audio-setup.md)).
2. Add an audio track if you need one, and choose its input in the track header's
   input menu (see [Inputs](inputs.md)).
3. Click the track's **R** button to arm it. It turns solid red.
4. Play or sing. The meter in the track header should move. Put on headphones if you
   want to hear yourself (see [Input monitoring](#input-monitoring)).
5. Turn on **Click** (C) in the header bar if you want the metronome.
6. Move the playhead to where the take should start: click in the ruler, or press
   Enter to go to the start.
7. Press **Record** in the header bar, or R. If a count-in is set, it counts you in
   first.
8. Press Space when you are done.

The take appears on the track as a clip, already moved back in time to make up for the
interface's delay (see [Latency and monitoring](latency.md)). A message at the bottom
says what was recorded. Ctrl+Z removes the whole recording in one step.

## Arming

Arming a track tells Northlight to record it the next time you press Record. Click the
**R** button in the track header, or select the track and press Shift+R. With several
tracks selected, Shift+R arms them all, so you can record a band, or several
microphones on one instrument, in one pass. Each armed track records its own input to
its own file.

While recording, an armed track's colour stripe turns red, and a red clip labelled
"● Recording" grows along it with the waveform drawn as it comes in.

If you press Record with nothing armed, a message says "Arm a track to record." Its
**Arm selected** button arms the selected track.

Arming is not an undo step. Ctrl+Z after arming undoes the edit you made before it,
as in most DAWs. The project still saves which tracks were armed.

## Input monitoring

Monitoring means hearing your input through Northlight, with the track's effects,
while you record. The monitor button (●) beside **R** cycles through three modes. Its
tooltip names the mode.

| Mode | What you hear | When to use it |
|---|---|---|
| **Off** | Never the input through this track. | Your interface has direct monitoring (a knob or switch that sends the input straight to your headphones), or you are recording a microphone in the same room as your speakers. |
| **Auto** | The input while the track is armed and the transport is stopped or recording; not while it only plays back. | Most recording. You hear yourself while you set up and while you record, and the track's existing clips when you play the take back. This is the default. |
| **On** | The input all the time, armed or not, playing or not. | Playing an instrument through the track's effects without recording, such as practising through [Corona](../effects/corona.md). |

The button is dim when monitoring is off and green while the input is being heard. In
**Auto** it turns yellow while it is waiting: the mode is on but the track is not armed,
or the song is playing back.

:::caution
With monitoring on and a microphone near your speakers, the speakers feed the
microphone and the sound howls. Monitor on headphones, or turn monitoring off.
:::

What you hear comes back a little late, by the round trip through your interface and
any effects that delay the sound. [Latency and monitoring](latency.md) explains how to
keep that delay small.

## Click and count-in

**Click** (C) turns the metronome on: a click on every beat, following the project's
tempo and time signature, both while you record and while you play back. Set its
volume with **Click level** in **Preferences > Recording**.

**Count-in** is the button next to **Click**. It gives you bars of click before
recording starts, so you can feel the tempo before you play. Click it to step
through Off, 1, 2 and 4 bars; the figure in its corner shows the count. While it
counts, the time display shows negative bars ("-1.4.000") in yellow. The count-in only
happens when you record, not when you play.

The count-in clicks only when **Click** is on. With Click off it still waits the same
number of bars, counting in the display, but silently.

**Preferences > Recording > Count-in** sets the count-in a project starts with. It
does not change the count of a project that is already open.

## Pre-roll

Pre-roll plays some bars before the playhead and starts recording at the playhead,
so you hear the music leading up to your entry. Set it with **Pre-roll** in
**Preferences > Recording**, from 0 to 8 bars (0, the default, means none). A
count-in, if set, comes before the pre-roll.

Pre-roll is not used when punching or loop recording: punching has its own one-bar
run-up, and a loop plays the whole range anyway (see
[Loop, punch and takes](takes.md)).

## Recording files

Northlight records WAV files, one per armed track and per take. Choose the kind in
**Preferences > Recording > Format**:

- **WAV 32-bit float**, the default. A 32-bit float file cannot clip, so a take
  that is too hot in Northlight can be turned down afterwards without damage. (Your
  interface can still clip before Northlight gets the sound, so keep an eye on its
  input level.)
- **WAV 24-bit**. Files are a quarter smaller, and 24 bits is plenty for any real
  recording.

Files are recorded at the project's sample rate. The audio device must run at the same
rate: if it does not, Record says so, and you change the rate in Audio Setup.

### Take names

Each new clip, and its file, is named by the pattern in **Preferences > Recording >
Take names**, which is `{track} {take}` to start with. In the pattern:

- `{track}` becomes the track's name.
- `{take}` becomes the take number.

So a third take on a track named "Vox" is called "Vox 3". The number counts on from
the clips and takes the track already has. Anything else you type is kept as it is,
so `{track} take {take}` gives "Vox take 3". If the pattern comes out empty, the track
name and number are used. Characters that Windows doesn't allow in file names are left
out of the file's name, and if a file of that name is already there, the new one gets
a different name rather than replacing it. A change to the pattern applies to the next
recording, not to ones already made.

### Where recordings go

In a saved project, recordings go in the project's media folder: a folder named after
the project with "Media" on the end ("Song Media"), beside the project file.

You can record into a new project before saving it. Its recordings go into a shared
folder, **Documents\Northlight\Recordings**, until you save. When you first save the
project, they move into its own media folder. If you close the project without saving
it, the recordings that nothing can use any more go to the Recycle Bin, so they don't
pile up and you can still get them back. See
[Media and missing files](../projects/media.md).

## While you record

- The **Record** button is solid red, the time display turns red, and the window's
  title starts with "●".
- The status bar shows how fast Northlight is writing to disk and how much space is
  left: "Disk 1.2 MB/s · 210.4 GB free".
- You can keep editing other parts of the song.

To stop, press Space, or Record (R) again. When recording stops, the takes are placed
on their tracks and a message confirms it. If something went wrong during the take,
see [When a take goes wrong](problems.md).
