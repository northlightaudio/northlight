---
title: "Drift, vibrato and slides"
description: "Shape a note's drift, vibrato, slides and gain, with the tools or in the Inspector."
sidebar:
  order: 3
---

Moving a note changes the pitch it sits on. The way the pitch moves within the note
is a separate matter: how it wanders, how it wavers, and how it slides in from the
note before. Northlight lets you shape each of these on its own, by dragging in the
audio editor or by typing values in the Inspector. This page explains each one and
when you would change it.

## Drift

**Drift** is the slow wander of the pitch across a held note: a singer who starts a
long note in tune and sags flat by the end, or creeps sharp as they push. It is
measured as how much of the drift is kept: 100% is as sung, 0% holds the note
steady on its pitch.

To change it, pick the **Drift** tool (3) and drag a note: down steadies it, up
brings the drift back. A tooltip shows the amount as you drag, in steps of 5%. Every
selected note changes together.

Reduce drift on long notes that sound unsteady, and leave it alone on short ones,
where it is part of how the words are shaped. Northlight never steadies the first
few hundredths of a second of a note, so the singer's scoop into it survives even at
0%.

## Vibrato

**Vibrato** is the faster, regular wave in the pitch of a held note. 100% is the
vibrato as sung; lower values calm it, down to none at 0%, and values up to 200%
deepen it.

Pick the **Vibrato** tool (4) and drag a note: down flattens the vibrato, up deepens
it. Calming a vibrato that is too wide for the song is the usual reason; adding a
little to a note that sounds static is the other. Correcting pitch never touches the
vibrato unless you ask it to (see [Correcting towards a key](correct.md)).

## Slides

Where one note runs straight into the next, the singer slides between them. The
**slide** is how long that glide takes. When you move one of the notes, the slide
keeps its shape and stretches to meet the new pitch, so the phrase still sounds
sung rather than stepped.

To change a slide, pick the **Pitch** tool (1) and hover over the point where two
notes meet. A handle appears: drag it sideways to make the slide longer or shorter.
The tooltip shows its length in milliseconds. A slide can take at most two fifths of
either note.

A longer slide sounds lazier and more expressive; a shorter one sounds more precise.
Shortening a slide is also a way to tidy a scoop that reaches the note late.

## Note gain and mute

Each note has its own **Gain**, from −48 to +12 dB, and can be muted. Use them to
bring up a word that gets lost or tame one that jumps out, without automation. The
note's menu has **Louder (+3 dB)**, **Quieter (−3 dB)** and **Gain to 0 dB**, and M
mutes or unmutes the selected notes. Gain changes fade in over a few milliseconds at
the note's edges, so they do not click.

## The Inspector

With notes selected in the audio editor, the Inspector (F5) shows a **Vocal note**
card (**Vocal notes** with a count, for several). Each value can be dragged, or
clicked and typed:

| Field | Range | What it is |
|---|---|---|
| **Pitch** | cents | The pitch the note plays, shown as a note and its offset ("A3 +12"); type it in cents, where 6000 is C3 and 6900 is A3 |
| **Drift** | 0 to 100% | How much of the slow drift is kept |
| **Vibrato** | 0 to 200% | How much vibrato; 100% is as sung |
| **Slide in** | 0 to 2000 ms | The slide into the note from the one before |
| **Slide out** | 0 to 2000 ms | The slide out of the note into the next |
| **Gain** | −48 to +12 dB | The note's level |
| **Mute** | | A muted note stays in the clip and plays nothing |

With several notes selected, changing **Pitch** moves them all by the same amount,
and the other fields set every selected note to the value you enter. A slide is
limited to two fifths of the note, however long a value you type.

Each change is one step in the [undo history](../editing/undo.md).
