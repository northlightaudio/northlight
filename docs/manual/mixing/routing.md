---
title: "Routing"
description: "Buses, FX tracks, sends, outputs and sidechains: where each channel's sound goes."
sidebar:
  order: 1
---

Routing is the path a channel's sound takes on its way to your speakers. By default
every track plays straight into the master. This page covers the other places it can
go: buses, FX tracks, other outputs of your audio device, and the key input of a
compressor.

## Buses and FX tracks

Both are channels with no clips of their own. They take in sound from other channels,
run it through their own effects and fader, and pass it on. The difference is how the
sound gets there.

- A **bus** collects whole channels. You set a track's **Output** to the bus, and all
  of that track now goes through the bus instead of straight to the master. Use a bus
  to treat a group of tracks as one: send every drum track to a "Drums" bus and one
  fader turns the whole kit up or down, and one compressor on the bus works on all of
  it together.
- An **FX track** is fed by **sends**: a copy of a channel's sound, at a level you
  choose, while the channel itself still plays to its own output. Use an FX track for
  an effect that many tracks share, most often a reverb or a delay. Each track sends
  as much as it needs, and the reverb's output is mixed in with the dry sound.

A simple rule: if the tracks should be *replaced* by the processed sound, use a bus;
if the processed sound should be *added* to them, use an FX track and sends.

FX tracks also show as rows in the arrange view; buses appear only in the mixer.

### Adding one

- **+ Bus**, after the last strip in the mixer, offers **Bus** or **FX track** with a
  choice of **Reverb**, **Delay** or **Empty**. Ctrl+B adds a bus.
- The project menu has **Add bus** and **Add FX track**.

An FX track made with a reverb or delay has that effect on it already, set fully wet
(**Mix** at 100%), since the sends carry only what should be processed and the dry
sound reaches the master by its own path. A new one is named after its effect
("Reverb", "Reverb 2"). Remove a bus with **Remove bus** in its strip's menu.

## Sends

A send taps a channel's sound and feeds it to an FX track or a bus.

1. On the channel's strip, click **+** under the sends.
2. Choose the FX track or bus. A bus that would feed the channel back into itself is
   left out, and a plain bus is marked "(group)".

With no FX tracks yet, the menu says "No FX tracks or buses yet" and offers **New FX
track…**: choose an effect, and Northlight makes the FX track and the send to it in
one undo step. In the arrange view, a track's right-click menu has the same under
**Send to**.

Each send row has the FX track's name and a small slider for the send level: drag it,
double-click to reset it, and hover for the value in dB. The dot beside it shows
where the send taps the channel:

- **Post-fader** (the dot empty, the default): the send follows the channel's fader.
  Pull the vocal down and its reverb comes down with it, so the balance between dry
  and wet stays the same. This is what you want for reverb and delay.
- **Pre-fader** (the dot filled): the send ignores the fader. Use it when the FX track
  should get a steady level whatever the fader does, for example to fade a track out
  while its echo carries on.

Click the dot to switch. Right-click a send for:

- **Pre-fader**.
- **Mute send**, to stop the send without losing its level. A muted send's name is
  struck through.
- **Pan**, from L100 to R100: where the sent copy lands in the FX track, on top of the
  channel's own pan. Wide strips show a pan control on each send row too (drag it up
  and down, double-click to centre it).
- **Remove send**.

Muting a channel mutes its sends as well. The **Editor** panel (F3) shows the selected
channel's sends with a larger level control, the **Pre** / **Post** choice, pan, mute
and remove.

## Outputs

The **Output** selector at the bottom of a strip sets where the channel plays:

- **Master**, the default.
- A **bus**, to group it with others. A bus that already feeds this channel is greyed
  out, since that would make a loop.
- Under **Hardware outputs**, another pair of your audio device's outputs ("Out 3/4"),
  if it has more than one pair.

A channel set to a hardware output plays straight out of that pair, past the master's
inserts, fader and meters. Use it to send a headphone mix to a performer, or to feed
an outboard effect. It stays in time with the master (see
[Delay compensation](delay-compensation.md)), and an export, which renders the master,
mixes it in.

If you open the project with a device that lacks that pair, the output stays listed,
greyed, as "(not on this device)", and the channel plays to its bus or the master
until the pair is back.

## Sidechains

A sidechain lets one channel's effect react to a different channel. The usual example
is ducking: a compressor on the bass that listens to the kick drum, so the bass dips a
little each time the kick hits and the two stop fighting for the low end. The other
channel's signal is called the **key**.

The built-in [Compressor](../effects/compressor.md) insert takes a key, and so does any
VST3 or CLAP plugin that has a sidechain input. The compressor in the
[channel strip](channel-strip.md) does not.

To duck a bass under the kick:

1. Select the bass track and open the **Editor** panel (F3).
2. Add the **Compressor** from the effects menu's **Dynamics** group (see
   [Inserts](inserts.md)).
3. Click **Sidechain** on the compressor's card, or right-click the insert's slot in
   the mixer and open **Sidechain**.
4. Choose the kick track. A track that would make a loop is greyed out.
5. Choose **Pre-fader** to take the kick before its fader, so moving the kick's fader
   doesn't change the ducking, or **Post-fader** to follow it.
6. Play the song and lower the threshold until the bass dips by a few dB on each kick.
   Use a fast attack and a release that lets the bass come back before the next hit.

The card's button now reads "Key: Kick", and a hosted plugin's card shows a "key:"
chip under its header that you can click to change it. Choose **None** to take the key
away. On a bus, the other buses can be keys too.
