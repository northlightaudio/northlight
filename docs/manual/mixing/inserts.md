---
title: "Inserts"
description: "Adding effects to a channel, and moving, copying and bypassing them."
sidebar:
  order: 2
---

An **insert** is an effect placed in a channel's path: the whole sound goes through it,
from the first insert to the last, and on to the fader. Every track, bus, FX track and
the master has its own chain of inserts. They can be Northlight's built-in effects or
[VST3 and CLAP plugins](plugins.md) you have installed.

Besides its inserts, every track and bus has a [channel strip](channel-strip.md), six
modules that are always there and switched on in place. Inserts are for everything
else.

## Where inserts show

- In the **mixer** (F2), each insert is a slot on the strip, under the name. A slot
  shows the effect's name and, for a plugin, its format (VST3 or CLAP).
- In the **Editor** panel (F3), the selected channel's inserts are cards in a row, left
  to right in the order the sound passes through them. A built-in effect's card holds
  its controls and display; a plugin's card shows its name, maker, format and delay,
  with an **Open editor** button (or double-click it) for the plugin's own window.

Click a slot's name in the mixer to open its editor. Click its format badge to find the
plugin in the Browser panel.

## Adding an insert

1. Click **+** in a strip's insert slots, or the **+** card at the end of the chain in
   the Editor panel.
2. Choose an effect.

To put one between two cards in the Editor panel, hover in the gap between them and
click the round **+** that appears. You can also drag a plugin from the
[Browser panel](plugins.md#the-browser-panel) onto a strip, the chain in the Editor
panel or a track header; a line shows where it will go.

The effects menu lists:

- **Recent**: the effects you used last.
- **Effects**, by kind: **Dynamics**, **EQ & Filter**, **Reverb & Delay**,
  **Modulation**, **Drive & Colour**, **Guitar**, **Stereo & Utility** and **Other**.
  In each, Northlight's built-ins come first, then your plugins of that kind with
  their maker and format beside them.
- **Favourites**: plugins you have starred in the Browser panel.
- **By maker**: your plugins grouped by manufacturer.
- **Scan for plugins...** and **Plugin browser...**.

The built-in effects are the [Compressor](../effects/compressor.md) and the
[Opto Compressor](../effects/compressor.md#the-opto-compressor),
[Glue Compressor](../effects/glue-compressor.md), [Limiter](../effects/limiter.md),
[EQ](../effects/eq.md), [Gain](../effects/gain.md), [Reverb](../effects/reverb.md),
[Delay](../effects/delay.md), [Tape Echo](../effects/tape-echo.md),
[Bloom](../effects/bloom.md), [Vesper](../effects/vesper.md),
[Exciter](../effects/exciter.md), [Patina](../effects/patina.md),
[Orbit](../effects/orbit.md), [Stereo Widener](../effects/stereo-widener.md),
[Corona](../effects/corona.md) and the [pedals](../effects/pedals.md).

## The insert menu

Right-click a slot in the mixer, or click **⋮** on a card's header, for the menu every
insert shares:

| Item | What it does |
|---|---|
| **Open** | Opens the effect's editor window. |
| **Bypass** | Switches the effect off without removing it (below). |
| **Presets** | A built-in's presets, or a plugin's factory programs and preset files. See [Presets](../effects/presets.md). |
| **Replace** | Swaps this effect for another in the same place. |
| **Move up**, **Move down** | Moves it earlier or later in the chain. |
| **Wet/dry** | How much of the processed sound is heard: 0, 25, 50, 75 or 100%. Below 100% the dry sound is mixed back in. |
| **Sidechain** | For effects that take a key: which channel they listen to. See [Routing](routing.md#sidechains). |
| **Outputs** | For an instrument with extra outputs: a track for each pair. |
| **Copy**, **Copy chain** | Copies this insert, or every insert on the channel, with their settings. |
| **Paste after** | Pastes what you copied after this insert. |
| **Show in plugin browser** | Finds the plugin in the Browser panel. |
| **Latency** | How much the effect delays the sound, when it does. See [Delay compensation](delay-compensation.md). |
| **Remove** | Takes the insert out. |

A plugin that is missing, failed to load or crashed puts what went wrong, and the way
out, at the top of the menu (see [Plugins in trouble](plugins.md#plugins-in-trouble)).

## Bypass

A bypassed insert passes the sound through untouched but keeps its settings, so you
can hear the difference it makes. Hover over a slot in the mixer and click the power
icon at its left, use the power button on a card's header or in the plugin's window,
or choose **Bypass** from its menu. A bypassed slot's name is struck through, and a
bypassed card is dimmed.

A bypassed plugin still delays the sound by its latency, so switching bypass never
moves anything out of time.

## Moving and copying

- **Within a channel**: drag a slot by the handle at its right (it shows on hover), or
  a card by its header. A line shows where it will land.
- **To another channel**: drag the slot onto another strip, the master included. The
  insert moves with its settings and automation.
- **Copy instead of move**: hold Ctrl as you drop. A plugin is copied with its full
  state.
- **By menu**: **Copy** or **Copy chain** on one channel, then **Paste after** on a slot
  of another.

Every add, move, copy and removal is one undo step.

## The master's inserts

The master strip has insert slots under its name like any other strip, with **+**
to add one. They process the whole mix before the master fader, so the loudness
readout and every export hear them. This is the place for a gentle bus compressor such
as the [Glue Compressor](../effects/glue-compressor.md), or a
[Limiter](../effects/limiter.md) at the very end to stop peaks going over. Their menu
is the same as any insert's, without Sidechain and Outputs.

Channels set to a hardware output (see [Routing](routing.md#outputs)) bypass the
master, so its inserts don't touch them.
