---
title: "Metering and loudness"
description: "Reading the level meters, the clip lights and the master's loudness readout."
sidebar:
  order: 6
---

Meters show how loud each channel is playing, so you can see what your ears might
miss: a track that is too hot, a peak that clips, a mix that is quieter than most
music. Every mixer strip has a level meter, and the master adds a loudness readout.

## The level meters

Each strip's meter sits beside its fader, one bar for the left channel and one for the
right. It shows the **peak** level, the highest point of the waveform, in **dBFS**:
decibels below full scale, the loudest level digital audio can hold. 0 dBFS is the top;
everything else is a negative number. The meters read from -60 dB at the bottom.

A channel's meter shows its level after the fader, so moving the fader moves the
meter. The colours mark rough zones:

| Colour | Level | What it means |
|---|---|---|
| Green | below -18 dB | A comfortable level for a single track. |
| Yellow | -18 to -6 dB | Healthy and strong. |
| Orange | -6 to 0 dB | Hot: close to the top. |

A thin line on each bar marks the highest recent peak for a second and a half, so a
short hit is easy to read.

Wide strips (see [The mixer](mixer.md#the-strips-menu)) add a dB scale beside each
meter; the master always has one.

### The clip light

The small square above each bar lights red when the level reaches 0 dBFS, and stays lit
until you click the meter, so you see it even if you were looking away. When the master
clips, a light shows in the header bar's transport too.

Inside Northlight a channel going over 0 dB doesn't distort by itself, since the mix is
worked out with plenty of headroom (though some plugins do distort when pushed). The
**master** going over does distort, in the exported file and on your speakers. If the master's clip light comes
on, pull faders down, or catch the peaks with a [Limiter](../effects/limiter.md) on the
master. There is no need to aim for the top: a mix peaking around -6 dB is easy to work
with.

## The loudness readout

Under the master's meter are three numbers in **LUFS** (loudness units relative to full
scale). Where a peak meter shows the highest points, loudness follows how loud the
music sounds to a listener, weighting the frequencies the way the ear does. It is
measured to the ITU-R BS.1770 standard that streaming services and broadcasters use.

| Reading | What it measures |
|---|---|
| **M** (momentary) | The last 0.4 seconds: it moves with every phrase. |
| **S** (short-term) | The last 3 seconds: steadier, good for comparing sections. |
| **I** (integrated) | The whole stretch since you last pressed play: the song's overall loudness. |

The integrated reading starts again each time playback starts from stop, so play the
song from the start to the end to read its loudness. Very quiet passages are left out
of it, as the standard asks, so a fade or a silent intro doesn't pull it down.

The **I** reading changes colour when it is within 1 LU of -14 LUFS, a common target
for streaming. Most streaming services turn louder songs down to about that level, so
making a mix louder than that gains little and costs dynamics. Treat it as a guide,
not a rule.

To set the loudness of the exported file itself, use the loudness options when you
[export](../exporting/index.md).

## Other meters

Many effects have their own meters: the **IN**, **GR** (gain reduction) and **OUT** bars
of the [channel strip](channel-strip.md)'s modules and the compressors, for example.
The [Stereo Widener](../effects/stereo-widener.md) shows the stereo image and the
correlation between left and right.
