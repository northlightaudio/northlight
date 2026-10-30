---
title: "Troubleshooting"
description: "What to do when something sounds or behaves wrong."
sidebar:
  order: 3
---

Most problems in a DAW come down to a handful of causes: the audio device, the
computer's power settings, a plugin, or a file that has moved. This page goes
through the common ones, what Northlight shows you about each, and what to try. If
none of it helps, [ask for help](help.md).

## Crackles, clicks and dropouts

A **dropout** is a moment when the computer could not prepare the next slice of
audio in time, so the audio device played a gap instead. You hear it as a click, a
crackle or a stutter. When one happens, the status bar at the bottom of the window
shows a count in amber ("3 dropouts"); hover over it to see when the last one was,
and click it to reset the count. The **CPU** figure beside it shows how much of the
available time Northlight is using to process each slice; it turns amber, then red,
as the busiest slices get close to the limit.

Try these, roughly in order:

1. **Raise the buffer size.** In **Audio Setup** (click the device name in the status
   bar), choose a larger **Buffer size**: 256 or 512 samples instead of 64 or 128.
   A larger buffer gives the computer more time for each slice, at the cost of more
   delay between playing and hearing (see
   [Latency and monitoring](../recording/latency.md)). Mixing works fine with a large
   buffer; keep small ones for recording.
2. **Check the power settings.** Windows' power saving slows the processor down and
   wakes it up again, which causes clicks even when there is plenty of processing to
   spare. In **Windows Settings > System > Power & battery**, set the power mode to
   **Best performance**, and on a laptop, plug it in.
3. **Use your interface's ASIO driver** if it has one (see
   [ASIO drivers](#asio-drivers) below).
4. **Close other programs**, especially ones that use audio or the disk heavily:
   browsers playing video, other music software, backups and updates.
5. **Freeze heavy tracks.** A track with a demanding instrument or a long chain of
   plugins can be [frozen](../mixing/freezing.md), which renders it to audio and
   frees its processing.
6. **Move the project to a faster drive.** If the dropout warning says the disk was
   the cause, the drive could not deliver the audio quickly enough. Projects on an
   internal SSD are far less likely to have this than ones on a USB stick or a
   network drive.

If clicks continue at a low CPU figure, they are probably coming from outside
Northlight: another driver (often Wi-Fi, Bluetooth or graphics) holding up the
computer, or power saving. The **Late callbacks** row of the Diagnostics panel
counts these.

## The Diagnostics panel

The **Diagnostics** panel (F7) shows how the audio engine is coping, live. A value
that needs attention turns amber or red.

| Row | What it tells you |
|---|---|
| **Callback p50**, **p99**, **max** | How much of each audio slice's time Northlight used: typically (p50), in the worst 1% (p99), and at its worst (max), with a graph of the last minute. Near 100% means dropouts |
| **Overruns** | Slices Northlight did not finish in time: dropouts caused by processing |
| **Late callbacks** | Times the audio driver asked for audio late. These are heard as clicks even when processing kept up, and usually come from another driver or from power saving |
| **Driver dropouts** | Dropouts the driver itself counted. Many ASIO drivers cannot report these, and the row then says "not reported" |
| **Disk gaps** | Moments the disk could not deliver a streamed clip in time |
| **Device** | The device, its sample rate and its buffer size |
| **Worker threads** | The threads that process tracks in parallel, and how many run at the priority Windows gives to audio (MMCSS). Fewer than all is flagged |
| **Output latency** | The output delay, in samples |
| **UI frame**, **Paint** | How long the screen takes to draw. These matter if the window feels sluggish, not for the sound |
| **Plugin latency per track** | The delay each track's plugins add, in samples (see [Delay compensation](../mixing/delay-compensation.md)) |

**Copy report** copies all of it as text, ready to paste into a bug report.

### Finding where a click comes from

For a stubborn click, start Northlight with `--output-tap` followed by a folder, for
example from a command prompt:

```
Northlight.exe --output-tap "C:\Users\You\Desktop\tap"
```

Northlight then records exactly what it sends to the audio driver into that folder,
with a list of any clicks it finds and of late callbacks. If the recording has the
click, it came from Northlight's processing; if the recording is clean but you heard
a click, it came from the driver or the system. Attach the files to a bug report if
you are not sure.

## No sound

- **The status bar says "No audio device".** Open **Audio Setup** and choose a
  **Driver type** and device. If the device is listed but will not open, another
  program may be using it: many ASIO drivers, and Windows Audio in its exclusive
  modes, allow only one program at a time. Close the other program and try again.
- **The device was unplugged.** Audio Setup's status line reads "Device unavailable,
  retrying…". Plug it back in and Northlight picks it up again. If Northlight
  switched to another device meanwhile, the status bar's device name has a warning
  dot; click it to choose your device again.
- **The outputs are turned off.** In Audio Setup's **Channels** card, make sure the
  outputs your speakers or headphones are connected to are ticked. The **Output**
  meter in the **Monitoring** card should move while the song plays.
- **Something in the mix is silent.** Check the master fader, any track that is
  muted, and any track left soloed (solo silences every other track). Look at the
  meters in the [mixer](../mixing/mixer.md) (F2) to see where the signal stops.
- **The interface's own volume.** Check the interface's output knob and, in its own
  software (**Control Panel...** in Audio Setup opens it), its routing and mute.

**You can play but not hear your input**, or a recording is silent: see
[Inputs](../recording/inputs.md) and
[Recording audio](../recording/recording-audio.md#input-monitoring). The input must be
ticked in Audio Setup, chosen on the track, and the track armed or monitoring.

## ASIO drivers

**ASIO** is a way for audio software to talk to an audio interface directly, with
less delay than Windows' own audio system. The driver from your interface's maker is
the best choice: install the latest one from their website.

- **Generic ASIO drivers** such as **ASIO4ALL** and **FlexASIO** sit on top of
  Windows' audio system so that devices without their own ASIO driver can use it.
  They can work, but they add complications: the device may be locked away from
  other programs, settings are made in the generic driver's own panel, and latency is
  often no better than Windows Audio. For built-in sound or a USB headset, choose a
  **Windows Audio** driver type in Audio Setup instead.
- **Settings in the driver's control panel.** Many ASIO drivers decide the buffer
  size, and sometimes the sample rate, in their own control panel rather than letting
  the program choose. If Audio Setup does not offer the size or rate you want, click
  **Control Panel...** and change it there.
- **Sample rate.** The **sample rate** is how many times a second the audio is
  measured: 44.1 or 48 kHz is normal. An empty project takes the device's rate. A
  project that already has audio keeps the rate it was made at, so set Audio Setup's
  **Sample rate** to match it. If another program has set the interface to a
  different rate, set it back in Audio Setup or the driver's control panel.
- **Only one program at a time.** Most ASIO drivers serve one program. If Audio
  Setup cannot open the device, close other music software, including programs in
  the notification area.

## Plugins that fail or crash

Northlight checks new plugins in a separate program when it scans them, so a plugin
that crashes or hangs while being scanned cannot bring Northlight down. It goes on
the **Blocklist** in **Preferences > Plugins** and is skipped until you click
**Clear** there.

A plugin in a project can be in trouble in three ways. Its slot in the mixer, and its
card in the Editor panel, say which, with the reason when you hover over it:

- **Not installed**: the plugin is not on this computer, or has not been scanned yet.
  Its settings are kept in the project. Install it, then choose **Scan for plugins**.
- **Failed**: it is installed but would not load. **Retry** tries again; **Blocklist**
  stops Northlight trying.
- **Crashed**: it crashed while playing and was switched off; its audio passes
  through unprocessed. Save your project, then choose **Reload plugin**, which starts
  it again from its last saved state without restarting Northlight.

When a project opens with plugins missing or failing to load, a message names them
once. If a plugin keeps crashing, check its maker's site for an update, and report it
to them. A plugin is code you chose to run: Northlight cannot always catch a plugin
that misbehaves, and a bad one can occasionally close Northlight altogether. If it
does, the next launch offers your autosaved changes back.

## Missing media

If a project opens with audio files that have moved or been deleted, a message says
so, and the clips that use them are marked as missing. Click **Locate…** in the
message or on a missing clip, find the file, and Northlight relinks it and offers to
relink any others it finds in the same folder. See
[Media and missing files](../projects/media.md).

## A project that will not open

- **Use the backup.** Each save keeps the previous version beside the project, as
  `Song.northlight-bak` for a project called `Song`. It opens like any project: in
  **Open...**, choose it, then save it under a new name. (This needs **Keep backup**
  on in [Preferences > General](preferences.md#general), which it is by default.)
- **Restore the autosave.** If Northlight closed with unsaved changes, the next launch
  says so and offers **Restore** or **Discard**. Restore opens the changes it saved;
  check them, then save. Autosave runs every two minutes unless you changed it.
- **Recover recordings.** If Northlight closed while recording, the next launch offers
  to **Recover** the unfinished takes and put them back where they were recorded.
- **Cancel a slow open.** While a project opens, a sheet shows what is loading. You
  can cancel it until the plugins start loading.

If a project crashes Northlight every time it opens, please report it and attach the
project if you can (see [Getting help](help.md)).

## Where Northlight keeps things

| What | Where |
|---|---|
| Settings, plugin list, autosaves and crash reports | `%APPDATA%\Northlight` |
| Crash reports | `%APPDATA%\Northlight\Crashes` (moved to `Crashes\Seen` once you have seen the notice) |
| Templates, and recordings of projects not yet saved | `Documents\Northlight` |
| A project's recordings and imported audio | The `<project> Media` folder beside the project file |

To open the settings folder, use **Show in Explorer** in
[Preferences > Diagnostics](preferences.md#diagnostics). After a crash, the next
launch says "Northlight closed unexpectedly last time" and **Show** opens the report:
a `.dmp` file and a `.txt` file with the same name. Reports stay on your computer;
Northlight does not send them anywhere.

Northlight does not keep a running log. For a bug report, **Copy system report** in
Preferences > Diagnostics and **Copy report** in the Diagnostics panel give the
details that help most.
