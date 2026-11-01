---
title: "Correcting towards a key"
description: "Tune a whole take, or a few notes, towards the key with the Correct sheet."
sidebar:
  order: 2
---

**Correct** tunes many notes in one pass. It does not snap them to pitch: each note
is moved most of the way towards a sensible note of the key, its vibrato and its
slides are kept, and the result is an ordinary set of note edits that you can adjust
one by one afterwards. Used gently, it tidies a take without making it sound
corrected.

## Correcting a take

1. Open the clip in the audio editor in **Pitch** mode (see
   [Vocal pitch editing](index.md#opening-a-vocal)).
2. Select the notes to correct, or select none to correct every note in the clip.
3. Click **Correct** in the editor's toolbar. A sheet opens, titled **Correct every
   note** or **Correct the selected notes**.
4. Check the **Key** (see below), and play the part. The notes already sound
   corrected: every change in the sheet is heard at once.
5. Adjust the settings until it sounds right, then click **Apply**. Clicking
   anywhere outside the sheet keeps the result too. **Cancel** puts every note back
   as it was.

The whole correction is one step in the undo history, however much you changed in
the sheet.

## The key

Correct aims at the project's key, the same key the
[piano roll](../midi/piano-roll.md) highlights. The sheet's **Key** button shows it.

When the project has no key yet, Northlight works one out from the vocal itself, from
which notes are sung and for how long, and shows it with "(detected)" after its name.
Click the button to confirm or change it. Whichever key you apply becomes the
project's key.

A short phrase can suggest the wrong key: a tune in A minor can easily look like
C major, which has the same notes. That does less harm than it might, because the key
only settles close calls between two neighbouring notes. Correct never pulls a note
onto a different step of the scale because the key says so.

The toolbar has its own key button, to the right of **Breaths**, that shows and sets
the project's key at any time. Its menu has **Detect from this vocal**, **No key**,
and every key by scale.

## How Correct chooses each note

For each note, Correct considers the scale notes either side of it and its nearest
semitone, and picks the one that best fits:

- the note closest to what was sung;
- the shape of the melody, so a phrase that rises still rises and one that falls
  still falls;
- the same target for the same sung pitch, so a repeated note stays repeated;
- with **Prefer chord tones** on, the notes of the chord playing at that moment.

A note sung in tune that is outside the key stays where it is, so a deliberate
chromatic note is not "corrected" away.

## The settings

| Setting | Default | What it does |
|---|---|---|
| **Key** | The project's (or detected) | The key the notes are corrected towards |
| **Strength** | 85% | How far each note moves towards its target |
| **Tolerance** | 10 ct | Notes already this close are left alone |
| **Drift kept** | 60% | How much of each held note's slow drift stays |
| **Vibrato** | 100% | How much vibrato stays, when **Keep vibrato** is off |
| **Keep vibrato** | On | Keeps every note's vibrato as sung |
| **Prefer chord tones** | Off | Leans towards the notes of the chords playing underneath |

**Strength** below 100% leaves a little of the singer's own pitch in each note.
That small remainder is much of what keeps a corrected vocal sounding human. Try
100% for a deliberately tight, modern sound, and 60 to 70% for a light touch.

**Tolerance** protects notes that are already good enough. Raise it to 15 or 20 cents
to correct only the notes that are clearly out.

**Drift kept** steadies long, held notes that wander. At 100% the drift is untouched;
at 0% a held note sits flat on its pitch. It only applies to notes longer than a
quarter of a second; shorter notes keep their drift.

**Keep vibrato** leaves the vibrato exactly as sung, which is almost always what you
want. Turn it off to use the **Vibrato** slider: below 100% calms a wide vibrato, above
100% (up to 200%) deepens it.

**Prefer chord tones** looks at the MIDI clips on other tracks that play under each
note and favours the notes of that chord. It helps when the melody passes through
notes the key alone cannot decide between. It has no effect when there is no MIDI
playing.

## What Correct leaves alone

- **Short notes.** Very short notes, under about a tenth of a second (ornaments,
  runs, quick passing notes), move much less than the strength says. Correcting them
  fully tends to sound mechanical.
- **Notes far from the key.** Correct does not move any note more than about a
  semitone on its own. A note further away than that is left alone and outlined in
  the editor, and the sheet says how many there are ("3 notes left alone: more than a
  semitone from the key"). These are usually notes the analysis misread, or notes
  sung on purpose. Check each one by ear and move it yourself if it needs it (see
  [Editing notes](notes.md#moving-notes)).
- **Slides.** The slides between notes keep their shape and move with the notes
  either side, so a singer's scoop up to a note is still a scoop.

## After correcting

Every corrected note is an ordinary edited note. Select one to see and change its
pitch, drift and vibrato in the [Inspector](shaping.md#the-inspector), drag it with
the pitch tool, or right-click it and choose **Reset pitch, drift and vibrato** to
hear it as it was sung. Running Correct again on some notes replaces what it did to
those notes.

A transposed clip is corrected at the pitch it plays, so the key is the one you
hear.
