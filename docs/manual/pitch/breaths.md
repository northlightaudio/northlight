---
title: "Breaths"
description: "Find the breaths in a vocal and turn them down or mute them."
sidebar:
  order: 4
---

A close-miked vocal is full of breaths, and once it has been compressed they can be
nearly as loud as the words. When Northlight analyses a vocal for
[pitch editing](index.md), it also finds the breaths, so you can turn them all down
in one go or deal with them one at a time.

## Finding breaths

In pitch mode, the lane along the bottom of the audio editor shows what lies between
the notes:

- **Breaths** are grey blocks with diagonal hatching, labelled "breath" when there
  is room. A breath that has been turned down shows its new level ("−12.0 dB"); a
  muted one is darker and labelled "muted".
- **Consonants** (the hiss of an "s", the click of a "t") are fainter blocks.
- **Silence** between phrases is left blank.

Northlight decides a sound is a breath when it is noisy rather than pitched, lasts a
tenth of a second or more, and is clearly quieter than the singing around it. Short,
bright sounds next to the voice count as consonants.

## Turning every breath down

Click **Breaths** in the editor's toolbar and choose:

- **Turn breaths down 12 dB**: the usual choice. The breaths stay, but sit behind
  the voice.
- **Turn breaths down 6 dB**: a lighter touch.
- **Mute every breath**: silence them completely.
- **Restore every breath**: put them all back as recorded.

Turning breaths down usually sounds more natural than muting them. A vocal with no
breaths at all can sound oddly airless, and listeners notice the gaps. Mute only
when a breath is distracting, or for a deliberately clean, edited sound.

Each choice is one undoable step, and like every pitch edit it leaves the recording
itself untouched.

## One breath at a time

Click a breath in the lane to mute it, and click it again to unmute it. This works
with any tool. With the **Breath** tool (5) you can also click anywhere above the
breath, in the note area.

Right-click a breath for more:

- **Mute** / **Unmute**.
- **Turn down 12 dB**.
- **Restore**: back to its recorded level.

Every breath fades over a few milliseconds where it is turned down, so a change never
clicks.

## When Northlight gets it wrong

The analysis can mistake a breath for a consonant, or a soft consonant for a breath.
Right-click the block in the lane and choose what it really is: **It is a breath**,
**It is a consonant** or **It is silence**. The **Breaths** menu then includes it, or
leaves it out, from then on.

Consonants are best left at their recorded level: they carry the words, and turning
them down makes a vocal hard to follow. To soften harsh "s" sounds across a whole
vocal, use a de-esser instead (see [The channel strip](../mixing/channel-strip.md)).
