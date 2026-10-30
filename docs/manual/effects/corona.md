---
title: "Corona"
description: "A tube guitar amp and cabinet."
sidebar:
  order: 15
---

Corona is a tube guitar amp and its speaker cabinet, for playing and recording guitar
or bass without a real amp. It is modelled stage by stage on a 1960s-70s American
two-twelve combo with spring reverb and tremolo: clean and bright when turned down,
breaking up into a warm crunch as you turn it up. It is in the **Guitar** group of the
effects menu, with the [guitar pedals](pedals.md).

Plug your guitar into your audio interface's instrument (Hi-Z) input, set up the track
to record from it ([Inputs](../recording/inputs.md)), insert Corona on the track and
play.

![Corona's amp page: a champagne control plate with Bright, Volume, Treble, Middle, Bass, Reverb, Speed, Intensity, Master and Boost and a red Drive jewel, over a woven grille cloth with the Corona badge, and a dark strip along the foot with Input, its level LEDs, Gate, Sag, Output, the Quality keys, the Reverb and Tremolo footswitches and the Amp and Cabinet page keys](../images/corona.webp)

Corona has two pages, the amp and the cabinet. The **AMP** and **CABINET** keys at the
right of the strip along the foot switch between them; the lit LED shows which page
you are on. The strip is the same on both.

## The amp page

The knobs read 1 to 10, as an amp's do (1 is fully down). The plate is grouped as on
the real amp.

**PREAMP**

- **BRIGHT** (a lever, on by default) adds sparkle. Like the real switch, it does
  most at low volume and fades as **VOLUME** goes up.
- **VOLUME** is how hard the preamp is driven, and so how much it breaks up. It stays
  clean to about 5, starts to break up from 6, and is crunchy at 10.
- **TREBLE**, **MIDDLE** and **BASS** are the amp's tone controls. They are modelled
  on the real circuit, so they interact: turning one changes how the others sound,
  as on the amp.

**REVERB** sets the level of the amp's spring reverb.

**TREMOLO**: **SPEED** sets how fast the volume pulses and **INTENSITY** how deep,
up to a near chop. Switch the tremolo on with its footswitch on the strip.

**POWER**

- **MASTER** sets the level after the preamp. At 10 (the default) the amp is at full
  power, so the power stage adds its own drive as you push **VOLUME**. Turn it down
  to keep the preamp's break-up at a lower level.
- **BOOST** (a lever) adds about 10 dB in front of the second tube, so the amp breaks
  up much earlier.
- **DRIVE** is the jewel lamp. It glows faintly at rest and brightly when the power
  amp is being driven into clipping, so you can see when the amp is working hard.

## The strip along the foot

| Control | What it does |
| --- | --- |
| **INPUT** | Trims the level coming in, ±24 dB. The **LEVEL** LEDs beside it show the trimmed input; the top two are a warning. |
| **GATE** | A noise gate in front of the amp that quietens hum and hiss between notes. **Off** at the bottom; turn it up until the noise stops, but not so far that it cuts off the ends of notes. |
| **SAG** | How much the power supply droops under a loud chord, which softens the attack and makes the amp feel looser. |
| **OUTPUT** | The level out, -24 to +12 dB. |
| **QUALITY** | **2×**, **4×** (the default) or **8×**. Higher settings are smoother in the high end when the amp is driven hard, at the cost of more CPU. |
| **REVERB**, **TREMOLO** | The footswitch: switch the spring reverb and the tremolo on and off. The tremolo's LED pulses with it. |
| **AMP**, **CABINET** | The page keys. |

## The cabinet page

The cabinet page shows the speakers with the grille cloth taken off, and two
microphones in front of them. Where you put the mics changes the sound as much as the
amp's tone controls.

On the left are the speakers of the chosen cabinet, with the two mics, **A** and **B**,
shown as rings over the first speaker. Each is labelled with its distance ("A · 3.0 cm").
A mic's ring is smaller the further it is from the speaker and narrower the more it is
angled. A mic that is switched off is a faint dashed ring.

- **Drag a mic** across the speaker. At the centre (the dust cap) the sound is bright
  and cutting; towards the edge it gets darker and rounder.
- **Scroll** over a mic, or press Up and Down, to move it nearer or further. Close up,
  the bass grows (the proximity effect); further back you hear more of the other
  speakers and the floor.
- Left and Right move a focused mic across the speaker. Hold Shift for finer steps.
- Double-click a mic to put it back where it started.

On the plate to the right:

- **CABINET** chooses one of five cabinets: **2×12** (open back, the amp's own),
  **1×12** (open back), **4×10** (closed back), and for bass **1×15** and **4×10B**. The
  sixth position, **IR**, uses a response file you have loaded.
- **SPEAKER** is a lever. **ON** uses the cabinet; **LINE** takes the cabinet out
  altogether, for when you use your own cabinet simulation after Corona. The header
  then reads **Line out**.
- **ROOM** adds the reflections of the room around the cabinet.
- **LOAD IR** opens a file chooser for an impulse response: a recording of a real
  cabinet and mic (WAV, AIFF or FLAC; only the first half second is used). Loading
  one switches **CABINET** to **IR**, and the file's name shows under the key.
- **MIC A** and **MIC B** each have an **ON** key, a **Ø** key that flips the mic's
  polarity, the mic type (**DYN** dynamic, **RIB** ribbon, **CON** condenser),
  **DISTANCE** (1 to 100 cm), **ANGLE** (0 to 45°; angled is duller), **LEVEL** and
  **PAN**. Mic A starts as a dynamic; mic B is a ribbon and starts switched off.

Blending two mics at different distances makes them arrive at slightly different
times, which changes the tone, as it does with real mics. If the blend sounds thin,
try **Ø** on one of them.

## Presets

**Default**, **Clean Sparkle**, **Surf**, **Tremolo Swamp**, **Funk Clean**,
**Country Twang**, **Jazz Warm**, **Edge of Breakup**, **Blues Lead**,
**Boosted Crunch**, **Two Mics, Wide**, **Bass Clean**, **Bass Growl** and
**Line Out, No Cabinet**. The bass presets use the bass cabinets and leave the reverb
off.

## Tips

- **Set the input level first.** Set **INPUT** so the **LEVEL** LEDs reach about the
  middle when you play your hardest. The amp responds to how hard it is hit, as a real
  one does, so a weak signal sounds cleaner than it should and a hot one breaks up
  too soon.
- **Play through it live.** Corona adds no [latency](../reference/glossary.md), so
  with a small buffer (64 or 128 samples in [Audio setup](../getting-started/audio-setup.md))
  you can monitor through it while you record. See
  [Latency and monitoring](../recording/latency.md).
- **You record the dry guitar.** What goes onto the track is your guitar as it came in,
  not the amp's sound. You can change the amp settings, the cabinet, or swap Corona for
  another amp after recording, and the performance stays the same.
- **2× is enough while recording.** Switch **QUALITY** to 2× to keep the CPU load
  low while you track, and back to 4× or 8× for the mix.
- **Pedals before the amp.** Put the [guitar pedals](pedals.md) before Corona in the
  track's inserts, as on a real pedalboard.
- **Keep your response files where they are.** A project remembers where a loaded
  response file was, rather than keeping a copy of it, so moving or deleting the file
  leaves Corona unable to read it.
