---
title: "When a take goes wrong"
description: "What happens to a recording when Northlight crashes, the interface is unplugged or the disk can't keep up."
sidebar:
  order: 5
---

A performance can't be played again exactly, so Northlight writes every take to disk
as it goes: whatever goes wrong, as much of it as possible is kept. This page explains
what you see, and what to do, when a take is interrupted.

## If Northlight closes during a take

While a take is recording, its file is kept up to date on disk, and beside it is a
small note of which track it belongs to and where it started. If Northlight crashes,
or the computer loses power, the file is left unfinished, but almost all of the audio
in it is there.

The next time you start Northlight, or open the project the take was recorded in, a
message says, for example, "1 recording was not finished: the app closed while
recording." It has two buttons:

- **Recover** repairs the files and puts each take back where it was recorded, on the
  track it was recorded on. If that track no longer exists, a new track named
  "Recovered: " and the track's name is made for it. Recovering is one undo step.
- **Set aside** leaves the takes out of the project. They are not deleted: they stay in
  the project's media folder with ".discarded" in their names, so you can import them
  later if you change your mind.

Recovered clips keep their file's name, which is the track's name, the date and time
of the recording and ".recovered". Rename them as you like. Takes from a loop
recording come back as separate clips, one over another across the loop, rather than
as a take group; drag them onto their own tracks, or delete the ones you don't need.

If the project had unsaved changes when Northlight closed, you are also offered its
autosave (see [Saving and opening](../projects/saving.md)). Click **Restore** on that
message before **Recover**, so the takes are added to the restored project rather than
to one that is about to be replaced.

:::note
Recordings made in a project that hadn't been saved yet are in the shared folder
**Documents\Northlight\Recordings**, so they are offered at the next start. Takes
recorded into a saved project are offered when you open that project again.
:::

## If the audio interface is unplugged mid-take

If the interface is unplugged, switched off or loses its connection while you record,
the recording stops at that moment and playback stops with it. What was recorded up
to then is kept and placed on its tracks as usual. A message says what happened and
how much was saved, for example: "The Volt 2 stopped. Playback stopped; the recording
was saved up to 3:12. Reconnect it or choose another device in Audio Setup." Its
button opens **Audio Setup**.

Plug the interface back in. Northlight keeps trying to open it; until it can, the
status line at the foot of **Audio Setup** says "Device unavailable, retrying…", and a
message says when the device is back. If Windows moves Northlight to another device in
the meantime, it stays stopped and says which one it now plays through ("Northlight now
plays through Speakers"), so nothing starts playing from the wrong speakers. Press Play
when you are ready to go on.

## If the disk is too slow

Northlight holds a few seconds of each input in memory while it writes to disk, so a
disk that pauses briefly does no harm. If the disk falls behind for longer than that,
because another program is using it heavily, or it is a slow USB stick or a network
drive, the samples that didn't fit are lost. Northlight writes silence in their place,
so the rest of the take stays in time. When you stop, a warning says so: "Recording
has 4800 samples of silence from disk overruns".

To avoid it, record to a fast internal drive, ideally an SSD, and close programs that
use the disk heavily while you record. A gap in a take can often be patched with a
punch-in (see [Loop, punch and takes](takes.md#punching-in)).

## If the disk fills up

While you record, the status bar shows how much space is left where recordings go, for
example "Disk 1.2 MB/s · 210.4 GB free". An hour of a mono 32-bit float track at
48 kHz takes about 0.7 GB, and 24-bit files take a quarter less (see
[Recording files](recording-audio.md#recording-files)).

If the disk fills up, or Northlight can't write to it for another reason, that take's
file stops growing, but nothing is thrown away: what reached the disk is kept and
placed like any other take. The recording does not stop on its own, and there is no
warning until you stop it. Then a message says how much was lost: "Recording failed
on 1 file(s): 96000 samples never reached the disk; what did is placed". Free some
space before the next take.

## When recording won't start

If Record doesn't start, a message says why:

| Message | What to do |
|---|---|
| "No audio device is open." | Open **Audio Setup** (the message's button) and choose your interface. |
| "Arm a track to record." | Arm a track with **R**, or click **Arm selected**. |
| "Cannot record: arm a track and give it an input first" | The armed tracks have no input: choose one in each track's input menu (see [Inputs](inputs.md)). |
| "Cannot record: the device runs at 44100 Hz but the project is at 48000 Hz; ..." | Set the sample rate in **Audio Setup** to the project's rate. |
| "Cannot record: could not create ..." | Northlight can't make the media folder. Check the drive is connected and you can write to it, or save the project somewhere else. |

For crackles, silence and other trouble outside recording, see
[Troubleshooting](../reference/troubleshooting.md).
