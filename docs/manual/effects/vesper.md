---
title: "Vesper"
description: "Gives a vocal space without clouding the words."
sidebar:
  order: 13
---

Vesper is a reverb for vocals that listens to the singing and shapes the reverb around
the words. An ordinary reverb catches everything: the consonants smear, the "s" sounds
hiss in the tail, breaths ring on, and the tail of one word sits on top of the next.
Vesper keeps consonants, sibilance and breaths out of the reverb, lowers the tail while
the singer sings, and lets it bloom in the gaps and at the end of each line. Insert it
on a vocal track; it is in the **Reverb & Delay** group of the effects menu.

![Vesper's face: a dusk-coloured panel with the Vesper name and three lights (Singing, Kept Out, Line End) at the top, the evening star labelled Catch at the top right, the Evensong display on the left, and Decay, Clarity, Duck and Ends knobs with rows of choices and smaller knobs on the right](../images/vesper.webp)

## The lights and the evening star

Beside the name, three lights show what Vesper hears right now:

- **SINGING** lights while the singer is singing.
- **KEPT OUT** lights when Vesper is keeping something out of the reverb: a consonant,
  an "s" or a breath.
- **LINE END** flashes when a line has ended (a pause as long as **Gap**).

The star at the top right is **Catch**. It glows with the voice and flares at the end
of each line. Click it to hold the tail as a pad under whatever comes next (it reads
**HELD**); click again to let it go. While held, the input is kept out of the reverb.

## Reading Evensong

The display, Evensong, shows the last six seconds, with now at the right edge.

- **The sky** is the reverb's tail, rising from the horizon as it gets louder.
- **The land** in front of it is the dry voice. Because the land hides the sky behind
  it, you see the reverb only between the words, which is what you hear: watch the
  sky dip under each phrase (the duck) and rise in the gaps.
- **Under the horizon**, coloured bars mark where the singer sang. Small ticks hang
  below them where Clarity kept a consonant or an "s" out of the reverb: the longer
  the tick, the more it took out. Dots mark breaths.
- **A star on the horizon** marks each line end. Its glow is larger the higher
  **Ends** is set.
- **The dotted line** through the sky follows the sung note: it is where **Follow**
  cuts the low end of the tail.
- **Ticks along the top** mark the beats at the project tempo.
- **The corners** read out the space and decay ("Plate · 1.8 s") with the pre-delay
  under them at the top left, and at the top right **DUCK**, how far the tail is
  ducked right now.

The effect's header reads **Held** while Catch holds, **Line end** for a moment after
one, or how far the tail is ducked.

## The controls

**The large knobs**

| Control | What it does |
| --- | --- |
| **Decay** | How long the tail lasts, 0.3 to 8 s (1.8 s by default). |
| **Clarity** | How far consonants and "s" sounds are turned down in what goes into the reverb, so the words stay crisp. At 100% they are kept out almost entirely. |
| **Duck** | How far the tail goes down while the singer sings, up to 18 dB (6 dB by default). |
| **Ends** | How much the tail lifts and lengthens where a line ends, so the last word blooms. |

**The row of choices**

- **SPACE**: **Plate** (bright and smooth, the classic vocal reverb), **Chamber**
  (smaller and more intimate) or **Hall** (large and long).
- **LOOKAHEAD**: **Off**, **Short** or **Long**. With lookahead, Vesper hears the voice
  a little before you do, so it can act in time. **Short** (30 ms) has the reverb's
  input turned down before an "s" starts. **Long** (half a second) knows a line has
  ended while its last word is still to come, so it throws that word into the reverb,
  and it makes **Swell** possible. See the tip on latency below.
- **ONE LINE** draws what one sung line becomes with the current settings: the voice
  as a block, the tail ducked under it, coming back afterwards, lifted at the line's
  end and fading over the decay.

**The smaller knobs**, in five groups:

- **TIME**: **Pre-delay** is the gap before the reverb starts. The chip beside the
  caption chooses **Auto** (the default: the pre-delay follows how fast the singing
  is, shorter for fast lines), **Free** (set in milliseconds, up to 250 ms) or a note
  value. **Swell** makes the tail rise into each line before it is sung; it needs
  **Long** lookahead and is dimmed without it.
- **DUCK**: **Release** is how quickly the tail comes back after the singer stops. Its
  chip sets it as a note value (1/8 by default) or **Free** (50 ms to 2 s). **Gap** is
  how long a pause counts as the end of a line (120 to 500 ms).
- **VOICE**: **Breaths** turns breaths down in what goes into the reverb. **Follow**
  keeps rumble out of the tail by cutting the lows a set distance under the sung note
  (Off at the bottom). **Mud** cuts a muddy build-up between 250 and 500 Hz when there
  is one.
- **TONE**: **Size** stretches or shrinks the space. **Low cut** and **Damping** make
  the lows and highs past them fade faster.
- **OUTPUT**: **Width** is the tail's stereo width (up to 150%). **Centre** turns the
  middle of the tail down, leaving room for the voice there. **Mix** is the balance of
  reverb and dry voice (25% by default). **Output** is the level out.

## Presets

Vocal Plate is the default. The presets marked with a lookahead add latency (see
below).

- **Vocal Plate**: a 1.8 s plate, no lookahead.
- **Pop Lead**: a shorter plate with more ducking and line-end lift (Long).
- **Ballad Hall**: a 3.2 s hall with a little swell (Long).
- **Intimate Chamber**: a short, close chamber (Short).
- **Rap Clarity**: short, heavily ducked and very clear, no lookahead.
- **Throws Only**: ducked hard, so you mostly hear the reverb on line ends (Long).
- **Swell In**: a long hall that rises into each line (Long).

## Tips

- **Lookahead adds latency.** Short delays Vesper's output by 30 ms and Long by half a
  second. Northlight makes up for it on playback, so the mix stays in time (see
  [Delay compensation](../mixing/delay-compensation.md)), but anything you hear live
  through Vesper is that much late. Keep **LOOKAHEAD** on **Off** while recording a
  vocal through it, and turn it on when you mix.
- **Start with Duck and Ends.** More **Duck** with more **Ends** gives a dry-sounding
  vocal whose lines still ring out; less of both sounds like an ordinary reverb.
- **If the tail sounds lispy or hissy**, turn up **Clarity**. Watch the ticks under
  the horizon to see what it takes out.
- **Catch for transitions.** Automate Catch on the last word before a break to hold
  it under the next section.
- **Use it as an insert.** Vesper shapes the reverb by what it hears coming in, so it
  works best straight on the vocal track. On an FX track shared by several singers or
  instruments it would be listening to all of them at once.
