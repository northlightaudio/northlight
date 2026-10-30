---
title: "Latency and monitoring"
description: "Why what you hear is a little late, how to keep the delay small, and how Northlight keeps recordings in time."
sidebar:
  order: 4
---

Sound takes a little time to get into a computer, through Northlight and back out
again. That delay is called **latency**. When you listen to a mix it doesn't matter,
but when you play or sing while hearing yourself through Northlight, a large delay
feels like an echo and puts you off. This page explains where latency comes from, how
to keep it small while you record, and how Northlight makes sure recordings land in
time anyway.

## Where latency comes from

Your audio interface doesn't hand sound to the computer one sample at a time. It
collects a block of samples, called a **buffer**, and passes the whole block over;
Northlight works on it and hands back a block to play. The bigger the buffer, the
longer each block takes to fill, and the later you hear the result.

The buffer size is set in **Audio Setup**, in samples, with its length in
milliseconds beside it. At 48 kHz, a 64-sample buffer is 1.3 ms; a 1024-sample buffer
is over 21 ms. The sound waits for one buffer on the way in and one on the way out,
plus a little time in the interface's converters and driver. The total, from sound
going in to sound coming out, is the **round trip**. The status bar shows it, for
example "4.3 ms round trip".

As a rough guide, a round trip under about 10 ms is hard to notice when you play;
past 15 or 20 ms most players feel it.

## Choosing a buffer size

Smaller buffers mean less delay, but give the computer less time to work on each block.
If it can't keep up, you hear clicks and crackles (dropouts), and the status bar counts
them.

- **While recording**, use a small buffer: 64 or 128 samples, or the smallest your
  computer plays cleanly with your project.
- **While mixing**, nobody is playing along, so latency doesn't matter. Use a large
  buffer, 512 or 1024 samples, and you can run more effects without dropouts.

Changing the buffer size doesn't change the music, only the delay, so switch freely
between the two. On Windows, ASIO drivers give much lower latency than Windows Audio:
use your interface's ASIO driver if it has one (see
[Setting up audio](../getting-started/audio-setup.md)).

## How recordings are placed in time

When you record along to the song, you play to what you hear, and what you hear is
late by the output half of the round trip. Your playing then reaches Northlight late
again by the input half. Without correction, every take would land a few milliseconds
after where you played it.

Northlight corrects this: when recording stops, it moves each take earlier by the
round trip, plus any delay that plugins add to the mix (see below). The message after a
take says how far, for example "placed 208 samples early for latency". You don't need
to nudge takes into place by hand.

MIDI is corrected by the output delay only, because a note reaches Northlight the
moment you play it (see [Recording MIDI](recording-midi.md)).

### Measuring the round trip

The round trip Northlight uses comes from your interface's driver, which reports its
own delay. Many drivers report it slightly wrong, so takes can still be a little off.
For exact placement, measure it with a cable:

1. Connect one of your interface's outputs to one of its inputs with a cable.
2. Turn down your speakers and headphones: the test plays a click through that output.
3. In **Audio Setup**, under **Latency**, choose that output and input as the
   **Loopback** pair.
4. Click **Calibrate**. It plays a short test sound several times ("Playing click…
   3/5") and listens for it to come back.

The result appears beside **Measured**, and from then on recordings are placed by it.
The status bar's round trip turns brighter to show it is a measurement, not the
driver's figure. **Clear** forgets the measurement.

The round trip changes with the device, the sample rate and the buffer size, so each
measurement is stored for that combination. If you change any of them, Northlight goes
back to the driver's figure until you calibrate again with the new setting. It is
worth calibrating once for each buffer size you record at.

## Plugin delay while recording

Some effects need to hear a little of the sound ahead before they can work on it. The
[Limiter](../effects/limiter.md)'s **Lookahead** is one; some third-party plugins,
such as linear-phase EQs, add a lot more. To keep every track in time, Northlight
delays everything else by the same amount. This is called **delay compensation** (see
[Delay compensation](../mixing/delay-compensation.md)); mixer strips show the delay
each channel's plugins add, and the master shows the project's total.

Delay compensation keeps recordings in time: the delay is part of what each take is
moved back by. But it also delays what you hear while you monitor. One lookahead
limiter anywhere in the project, on another track or on the master, makes your
monitored input that much later too. Northlight doesn't yet have a low-latency
monitoring mode that leaves those plugins out while you record.

So while you record:

- Keep plugins that add delay off the project, or set their lookahead to zero. Add them
  when you come to mix.
- Bypassing such a plugin is not enough: a bypassed plugin keeps its delay, so that
  turning it on and off never shifts the timing.

## Playing guitar through Corona

[Corona](../effects/corona.md), the guitar amp, adds no latency at all, so you can play
through it as you record:

1. Plug the guitar into your interface's instrument (Hi-Z) input, and choose that input
   on an audio track.
2. Add Corona to the track's inserts.
3. Set the buffer size to 64 or 128 samples.
4. Arm the track. With monitoring on **Auto** you hear the guitar through Corona while
   the track is armed; set it to **On** to practise without arming.

What Northlight records is the dry guitar, straight from the input, not the sound of
the amp. Corona plays it back through the amp each time, so you can change the amp's
settings, or swap it for another amp plugin, after recording. The same is true of every
effect on a track: inputs are always recorded before the track's effects.

## Monitoring without Northlight

Many interfaces have **direct monitoring**: a knob or switch that sends the input
straight to your headphones inside the interface, with no delay at all. It is the
surest way to have no latency, but you hear the input dry, without Northlight's effects.
If you use it, set the track's monitor button to **Off**, or you'll hear yourself
twice, once a little late.

**Audio Setup** also has **Pass inputs through to outputs**, under **Monitoring**. It
sends the interface's inputs straight to its outputs, with a **Level** control, without
going through any track. It's useful for checking that an input works, but it has the
same buffer delay as track monitoring and no effects. It is off by default; use
headphones with it, since a microphone near speakers will howl.
