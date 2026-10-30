---
title: "Delay compensation"
description: "Why some plugins delay the sound, and how Northlight keeps every track in time."
sidebar:
  order: 5
---

Some effects can't work on the sound the instant it arrives: they need to hear a little
of what comes next first. A limiter with lookahead, a linear-phase EQ or a pitch
corrector holds the sound back while it listens, and hands it on a few milliseconds
later. That delay is the plugin's **latency**.

On its own, one late track would play out of time with the others: a kick a few
milliseconds behind the bass can blur the beat. So Northlight delays every other path
through the mix by the right amount, so they all line up again at the master. This is
**plugin delay compensation**, often shortened to PDC. It is always on, and there is
nothing to set.

## What it covers

- **Tracks, buses and FX tracks**: each channel is lined up with the slowest path to
  the master, including the paths through sends and buses.
- **Sidechains**: a key arriving early is held back to match the channel it controls.
- **Hardware outputs**: a channel playing straight to another pair of outputs stays in
  time with the master.
- **The playhead**: it follows what you hear, not what the engine is working on.
- **Recordings**: a new take is moved back by the delay too, so it lines up with what
  you played along to. See [Latency and monitoring](../recording/latency.md).
- **Exports** start exactly in time.

A bypassed plugin still counts its latency, so switching bypass never moves anything
out of time. The modules of the [channel strip](channel-strip.md) add none.

## Where to see it

- **A plugin's latency**: on its card in the Editor panel, and as **Latency** in its
  insert menu, for example "32 smp (0.7 ms)" (smp is samples).
- **A channel's delay**: a line such as "+2.7 ms" at the bottom of its mixer strip,
  the total of its plugins. The line shows only while some channel has one.
- **The whole project**: "PDC 12 ms" under the master's gain. Hover over it for the
  number of samples. Everything you hear is that much later than the timeline.

## When it matters

While you mix, a few milliseconds of overall delay make no difference. It matters while
you **record**: compensation delays what you hear as well, so a plugin with a long
lookahead anywhere in the project, even on the master, makes your monitored playing
that much later. Northlight doesn't yet have a low-latency monitoring mode that leaves
those plugins out.

So if playing along feels late, look at the master's PDC readout and find the plugins
with the most latency (often a limiter on the master, or a linear-phase EQ). Bypassing
them won't help, since a bypassed plugin keeps its delay: remove them while you record.
Choose **Copy** in its menu before you remove it, and you can paste it back, with its
settings, when you have finished recording (see [Inserts](inserts.md#moving-and-copying)). The built-in [Limiter](../effects/limiter.md)'s **Lookahead** can also
be turned down to 0.
