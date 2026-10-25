# Plan: Vesper, a reverb for vocals

A built-in reverb that listens to the singing. It does the work engineers usually
build around a plain reverb by hand: a de-esser on the send, a ducker keyed from the
dry vocal, and throws automated at the end of each line. You set how much space the
vocal has, and Vesper decides which parts of the voice go into it and when.

Name: **Vesper**. Type `northlight.vesper`. In the effects menu under Reverb & Delay.

**Status (2026-10-25):** the branding is approved. Phase 1 is built: Bloom's
network is `plugins/FeedbackNetwork`, and Bloom's output is bit-identical to before
(checked across eight settings, then the check was removed). Phase 2 is built: the
Listener is `plugins/VocalListener`, sharing YIN (`plugins/Yin.h`) with the tuner; it
costs about 0.45% of a core at 48 kHz (release). The rest is not built.

## The name and the idea

Vesper is the evening star, the first light to show at dusk, and vespers is the
evening service that is sung. Both fit: it is a reverb for singing, and the brand is
Machair dusk. The name sits with Aurora, Orbit, Corona and Polaris.

The picture behind the face: **the voice is the land, the reverb is the sky.** While
the singer sings, the land stands tall and the sky behind it stays dark. When they
stop, the land drops away and the afterglow fills the gap. At the end of each line a
star comes out. That is what Vesper does to the sound, and the display draws it
literally, so the picture is how you read the effect, not decoration.

## Goals

- **Words stay clear.** Consonants, sibilance and breaths are kept out of the
  reverb. The tail gets quieter under the words and rises in the gaps.
- **One idea per control**, named in the user's words: Clarity, Duck, Ends. No
  sidechain routing, no automation lanes for throws.
- **Vocal spaces, not every space**: a plate, a chamber and a hall, tuned for a
  voice. Bloom stays the reverb for huge effects, and Reverb the convolution one.
- **Zero latency by default**, so it is safe while tracking. Lookahead is opt-in for
  mixing and reported to the engine's delay compensation.
- **You can see it work.** The display shows what was sung, what was kept out, where
  the reverb ducked and where each line ended.

## Not in this plan

- Reading the vocal editor's offline analysis (`engine/pitch/PitchAnalysis`) from an
  insert. That would give perfect phrase marks on recorded clips, but it couples an
  insert to the model. Lookahead gets most of the benefit without that. A follow-up.
- Pitch-shifted shimmer. Bloom has it; Halo (below) is the vocal version.
- Backing vocal stacks and choirs: the detector expects one voice. A stack works, but
  it is detected as one voice.

---

## 1. Signal path (DSP)

```
in ─┬───────────────────────────────────────────────────────────────── dry ──┐
    └─ Listener ─► Send shaper ─► Pre-delay ─► Space ─► Tail shaper ─► Duck ─┴─► Mix ─► out
       (voicing,   (Clarity,      (ms, Auto,   (Plate,  (Follow, Mud,  (and Ends)
        pitch,      Breaths)       Sync)        Chamber, Low cut,
        lines)                                  Hall)    Width, Centre)
```

### The Listener

It runs on the input's mono sum and decides, every 2.5 ms, what the singer is doing
(`plugins/VocalListener`). It decides from the past alone, so it adds no latency; its
pitch is about 20 ms behind the sound, so the first frames of a sung onset read as a
consonant. All of it is fixed-size and allocation-free, so it is real-time safe.

| It finds | How |
|---|---|
| **Pitch and voicing** | YIN (shared with the tuner, `plugins/Yin.h`) on the input decimated to at least 11 kHz: a 20 ms window over lags for 65 Hz to 1.1 kHz, every other frame (5 ms). Voiced when the normalised-difference dip is under 0.2 (0.3 at most), the level is above the floor and under half the power is above 4 kHz |
| **Silence** | Under -60 dBFS, or 30 dB under the line's level (a 3 ms level, so a pause shows within about 20 ms) |
| **Consonants and sibilance** | Any other sound without pitch. Sibilance is the 5-9 kHz band's share of the power, 0 at -18 dB and 1 at -6 dB, so an "s" inside a voiced word shows too |
| **Breaths** | Without pitch, not bright (under 55% of the power above 4 kHz), 12 dB or more under the line's level, and 100 ms or more after the last pitch. Decided as it is heard: whether a line follows cannot be known without lookahead |
| **Line ends** | Voice or consonants, then a pause as long as **Gap**; breaths do not count as singing. Reported once, on the frame the gap is reached |
| **Singing rate** | Syllables a second over the last 3 s: each voiced start after 30 ms without pitch, and each 6 dB rise from a dip inside a held voice (after a 3 dB fall from the last syllable's peak), for Auto pre-delay |

Without lookahead a line end is only known Gap after the last word, so Ends can only
lengthen the tail that is already there. With lookahead the Listener runs ahead of
the audio, so it also knows when the last word is sung and can push that word into
the reverb: a true throw. The detection rules are the same in both cases. Lookahead
only changes how early the Listener sees each event.

### Send shaper

- **Clarity** (0-100%, default 60%): turns consonants and sibilance down in the send
  only, by up to 24 dB. The dry vocal is never touched. A 2 ms attack and a 30 ms
  release, smoothed in dB, so the send does not click.
- **Breaths** (0-100%, default 100%): how far breaths are turned down in the send.

### Pre-delay

0-250 ms, **Auto** or a note value (**Sync**, the Delay's chip). Auto sets the
pre-delay from the singing rate: about 90 ms for slow, held lines down to 25 ms for
fast ones, gliding, so the tail starts after the consonant that opens each word.
Default: Auto.

### Space

One network, three tunings: Bloom's eight-line feedback delay network (the same
code, moved to a shared header), with shorter lines and its own diffusers.

| Space | Character | Tuning |
|---|---|---|
| **Plate** (default) | The classic vocal sound: dense at once, bright, no early reflections | Four input allpasses, fast build, highs damped late |
| **Chamber** | A small live room: a little early slap, warm | Sparse early taps (8-30 ms), mid-band decay longest |
| **Hall** | Big and slow to build, for ballads | Early cluster to 60 ms, long build, low end decays longest |

**Decay** 0.3-8 s (RT60, default 1.8 s). **Size** 50-150% scales the lines.
**Damping** 2-20 kHz and **Low cut** 20-1000 Hz inside the loop as shelves, as in
Bloom.

### Tail shaper

- **Follow** (Off, or ½-2 octaves below the sung note, default 1 octave): a
  high-pass on the tail that tracks the sung pitch, gliding over 80 ms, so there is
  no rumble or mud under the voice. It holds the last pitch through gaps.
- **Mud** (0-100%, default 30%): a dynamic cut of up to 6 dB at 250-500 Hz in the
  tail, deeper as that band builds up.
- **Width** (0-150%) and **Centre** (0-100%, default 40%): Centre takes the tail's
  middle down, so the tail sits round the vocal, not on top of it. It stays mono
  safe because only the mid is lowered, never phase-flipped.

### Duck and Ends

- **Duck** (0-18 dB, default 6 dB): the tail goes down while the voice is active.
  It is keyed from the Listener's activity, not the raw level, so a quiet held note
  ducks as much as a loud one and a breath does not duck at all.
- **Release** 50 ms-2 s or a note value (default 1/8 at the project tempo): how fast
  the tail comes back up in a gap, so the bloom lands in time.
- **Ends** (0-100%, default 40%): at a line end the tail rises up to +6 dB and its
  decay lengthens up to 2 times, then settles back over the next line. With
  lookahead, the last word's send also rises up to +6 dB.
- **Gap** 120-500 ms (default 250 ms): how long a pause counts as a line end.

### Lookahead and Swell

**Lookahead**: Off (0 ms, default), Short (30 ms: no consonant onset slips into the
send) or Long (500 ms: true throws and Swell). The latency is reported and kept
while bypassed. Changing it changes the reported latency, so it is not automatable.

**Swell** (0-100%, Long only): the tail rises into the start of each line, like a
reversed reverb, without bouncing or flipping audio. Dimmed when lookahead is not
Long, and the tooltip says why.

### Catch and Halo

- **Catch** (a latch, automatable): holds the last sung vowel as a pad. The tank's
  input is kept out and its decay goes to infinite, as Bloom's Freeze does, but it
  is taken at the end of the last voiced run so no consonant is held. Release fades
  it out over the decay.
- **Halo** (0-100%, default 0): a bank of 12 resonators tuned from the Listener's
  notes, so the tail rings at the pitches just sung. Each sung note excites its
  pitch class and the octave above for a few seconds. It is always in tune with the
  singer, even when the singer is out of tune with the track. Built in the last
  phase, and dropped if it doesn't sound good enough.

### Mix and output

**Mix** 0-100% (default 25%), **Output** ±12 dB. On an FX track fed by one vocal, set
Mix to 100%. The Listener works from the send's signal, so it still works there.

### Parameters

24: space, decay, size, preDelay, preDelaySync, clarity, breaths, follow, mud,
lowCut, damping, width, centre, duck, release, releaseSync, ends, gap, lookahead,
swell, catch, halo, mix, output. Every switch and choice fades over 10 ms. A governor
holds the network's energy as in Bloom.

## 2. The face (UI)

Vesper is not modelled on vintage gear, so it gets a **flat face**, not a hardware
one. Like the instruments (Aurora, Pulsar, Nebula), it is branded as its own
product: a scene behind glass panels, its own wordmark, and a backdrop that moves
with the sound. The scene is a dusk sky over the machair. The controls' hue is
**Thrift**, the sea pink of the machair and the colour of afterglow. **Straw** is
used only in the scene and the wordmark, as the gold low on the horizon.

### The scene (behind everything)

- **Dusk sky**: the body's ground is `canvas` at the top, warming toward the
  horizon: Thrift at 9% two thirds down, Straw at 10% just above the land. The
  afterglow's brightness follows the tail's level (smoothed over 300 ms, from 60% of
  those alphas in silence to 140% at -12 dB). When the reverb blooms, the face
  glows.
- **The land**: a low silhouette of machair and dunes along the bottom 34 px,
  filled `canvas` 3% darker. Along its crest is a 1 px rim of Straw at 30%, where
  the last light catches the grass. The glass panels stand on it. It is generated
  once from a seed, so it is the same in every instance, and drawn into the static
  layer.
- **Stars come out**: each line end lights a small star (2-3.5 px, `textPrimary`)
  at a seeded place in the sky above the panels. It fades in over 400 ms and out
  over the decay, as bright as Ends made the bloom. At most 24 are alive, the oldest
  going first. Over a song, the sky fills and empties with the singing. With
  reduced motion they appear and go without fading. Silent, no new stars.
- **Glass panels** (Aurora's): `surface1` at 72% over the scene, radius `lg`, a 1 px
  `textPrimary` rim at 7% and a top highlight. Each panel's caption has a 6 px hue
  dot before it.

### The header row (inside the face, 88 px)

- **The wordmark**: "Vesper" in **Newsreader** (`font.display`) italic, 38 px, in a
  gradient from Straw at the left to Thrift at the right. It is the only hero effect
  set in the serif: the brand's voice for something sung, and a quiet mark that it
  is not hardware. `assets/fonts` has the upright variable cut; the italic is
  Newsreader's separate italic file (same licence), added beside it, with the
  third-party notices rerun. Under it, VOCAL REVERB · NORTHLIGHT in `captionCaps` with 0.2
  tracking, `textTertiary`.
- **The preset bar** (previous, the name as a menu, next; "Custom" off-preset).
- **The Listener's lights**: three 6 px lamps with captions, SINGING (Thrift),
  KEPT OUT (`textSecondary`) and LINE END (`textPrimary`). They light as the
  Listener hears each thing, so you can see it listening. They decay over 120 ms
  (600 ms for LINE END). Dark: `surface3`.
- **The evening star** (Catch), at the right end: see below.

### Evensong (the display)

At the left, as tall as the body, 1.6 times as wide as it is tall. The last six
seconds scroll from right to left, with now at the right edge.

- **The sky**: the tail's level after the duck (tap after the Duck stage, -60..0 dB)
  as an area rising from the horizon. It is brightest at the horizon (the hue at 55%)
  and fades upward to 6% at its top edge, with a 1.5 px hue line along the top.
  While frozen by Catch, the area's top edge goes flat and the line turns
  `textPrimary`.
- **The land**: the dry voice's level (50 ms RMS, -60..0 dB) as a ridge drawn over
  the sky, filled `canvas` 4% toward the hue, with a 1 px `textSecondary` edge at
  60%. While the singer sings, the land hides the sky. In the gaps the afterglow
  shows. That is the duck, seen directly.
- **The word lane**: a 6 px strip under the horizon: voiced runs as hue bars (2 px
  rounded), consonants and sibilance kept out of the send as `textTertiary` ticks
  whose height is how much was removed, and breaths as `textTertiary` dotted runs.
  Its caption at the bottom left is KEPT OUT.
- **Stars**: a four-point star (7 px, `textPrimary`) on the horizon at each line end,
  where Ends began. Its glow (the hue at 30%) is as large as the boost Ends gave.
- **The pitch** (when Follow is on): a 1 px dotted `textPrimary` line at 20% through
  the sky, at the height of the Follow cutoff, on a quiet log scale.
- **Grid**: horizon at the bottom of the sky. Dotted lines at -20 and -40 dB
  (`gridBeat`), and beat ticks along the top at the project tempo, so Release's sync
  can be seen landing.
- **Corners** (captions `captionCaps` `textTertiary`, values mono):
  - Top left: SPACE, then "Plate · 1.8 s" in `title + 1` `textPrimary`, and under
    it "45 ms before" (or "Auto 45 ms" or the note value) in `value` `textSecondary`.
  - Top right: DUCK and the live reduction ("-5.8 dB", the hue while ducking, "–"
    when not).
  - Bottom left: the legend. A hue dot TAIL, a `textSecondary` bar VOICE, a tick
    KEPT OUT.
  - Bottom right: "6 S · NOW AT THE RIGHT".
- **Lookahead**: when on, the last 30 or 500 ms at the right is drawn ahead of a
  1 px `textPrimary` "now" line at 30%, labelled AHEAD, because the Listener has
  already heard it.
- **Reduced motion**: the display still scrolls (it is data), but stars appear
  without their fade.

### The evening star (Catch)

The brand's one big gesture, and a control. At the right of the header row, 64 px:
the evening star, a `textPrimary` core (5 px) with four long, thin spikes (1 px,
fading out to 30 px) and a Thrift halo.

- **It reacts to the voice**: the halo grows with the dry level (50 ms RMS) and the
  spikes flare to 1.4 times their length for 300 ms at each line end. Silent, it
  shines quietly. With reduced motion, the halo holds and nothing flares.
- **It is the Catch button**. Under it, CATCH in `captionCaps` `textTertiary`.
  Hover brightens the halo to 40% and the caption to `textSecondary`.
- **Held**: the core grows to 8 px and turns warm (`textPrimary` 20% toward Straw),
  the spikes reach full length and stop moving, the halo becomes a steady ring of
  Straw at 35%, and the caption reads HELD in `textPrimary`. The scene's afterglow
  stays up while it holds.
- Behaves as Bloom's seed: click, Enter or Space; focus ring round the halo; the hit
  area is a 44 px circle; one undo step. Tooltip "Catch: hold the last word".

### Controls

At the right, in Bloom's layout so the two reverbs feel related:

- **Large** (52 px bud knobs in four tiles): **Decay**, **Clarity**, **Duck**,
  **Ends**. These are the four controls that make Vesper what it is.
- **Space** (a tile of three pills, as Bloom's interval pills): Plate, Chamber,
  Hall.
- **One line** (a tile beside it, as Bloom's One hit): what one sung line becomes:
  the voice as a block, the tail ducked under it, the rise at the end of the line
  (Release), the Ends boost and the decay. It redraws as Duck, Release, Ends and
  Decay move.
- **Secondary** (28 px bud knobs in 50 px cells), in five groups under captions and
  hairlines:
  - TIME: Pre-delay (Auto/Sync chip), Size, Release (Sync chip), Gap.
  - VOICE: Breaths, Follow ("Off" at the bottom), Mud.
  - TONE: Low cut, Damping, Halo.
  - LOOK AHEAD: a three-way rotary selector Off / Short / Long, and Swell.
  - OUTPUT: Width, Centre, Mix.
- **Header readout**: "Held" in `textPrimary` while Catch holds, "Line end" in the
  hue for 600 ms after a star, else "Duck -5.8 dB" in `textSecondary` while it ducks.

Body 450 px: the 88 px header row, the panels (316 px) and the land showing under them. The width is the display's field, a 20 px gap and about
680 px of controls.

### Icon

For the effects menu (design-system.md 2.5): a four-point star above a horizon line
with a low arc of land under it, drawn on the icon grid at 1.5 px.

### Before building

The design rule: patterns first. Add "Evensong display" and "Star button" to
design-system.md, or generalise Bloom's seed into a "Seed button" both use. Add the
spec to plugins.md 7.2a, and Vesper to the hue list (Thrift). No new colour tokens are
needed; the face uses the house surfaces and the hue. Capture at 100% and 200% with
a demo vocal (a launch flag that loads a test phrase into a track with Vesper on).

## 3. Presets

Through `EffectPresets`:

- **Vocal Plate** (the defaults).
- **Pop Lead**: Plate, 1.4 s, Duck 9 dB, Ends 60%, Lookahead Long.
- **Ballad Hall**: Hall, 3.2 s, Duck 4 dB, Ends 50%, Swell 30%.
- **Intimate Chamber**: Chamber, 0.9 s, Mix 18%, Clarity 80%.
- **Rap Clarity**: Plate, 0.8 s, Duck 12 dB, Clarity 90%, Ends 70%, Gap 180 ms.
- **Dream Halo**: Hall, 4 s, Halo 50%, Swell 50%, Lookahead Long.
- **Throws Only**: Duck 18 dB, Ends 100%, Mix 40%, so the reverb is heard almost only
  at line ends.

## 4. Tests

In `tests/unit/plugins/VesperTests.cpp`, using synthetic vocals (a glottal-pulse
source through vowel formants, noise bursts for consonants, filtered noise for
breaths), so the tests are deterministic:

- Listener: a vowel at 110-880 Hz is voiced and the right pitch within 20 cents; "s"
  and "t" bursts are consonants; a breath before a line is a breath; a 300 ms pause
  is a line end at Gap 250 ms and a 200 ms one is not.
- Clarity at 100% keeps an "s" 20 dB or more lower in the send than a vowel of the
  same level. The dry path is unchanged to the sample.
- Duck: the tail is lower during a held note by the Duck amount ±1 dB and returns
  within the release.
- Ends: the tail after a line end is louder and longer than with Ends at 0.
- Lookahead: the reported latency is 0, 30 ms or 500 ms, and the dry path is delayed
  to match. With Long, the last word's send is boosted.
- Follow: no tail energy more than 2 octaves under the sung note.
- Centre at 100% leaves the tail's mid at least 12 dB under its side. The mono sum
  never cancels more than the mid lowering.
- Catch holds the vowel, not the consonant after it, and holds steady for 10 s.
- Real time: no allocation on the audio thread (the debug hook), every parameter
  automated, no denormals on silence after a long tail.
- Same behaviour at 44.1, 48 and 96 kHz.
- Bench: CPU per instance (the Listener's YIN is the cost to watch).

In `tests/unit_juce/VesperCardTests.cpp`: the display's mapping, star placement, the
card's layout at its sizes, and Vesper in `uiperf::builtInTypes()`.

## 5. Phases

Each phase ends buildable, tested and formatted, and is its own commit.

1. **Shared network**: move Bloom's feedback delay network to a shared header,
   Bloom unchanged (its tests prove it).
2. **The Listener**: voicing, pitch, consonants, breaths, line ends, and its tests.
3. **Vesper core**: Space, the send shaper, pre-delay, tail shaper, Duck and Ends,
   with zero latency. Registered, generic editor only.
4. **Lookahead and Swell**, and Catch.
5. **Presets.**
6. **Design and face**: design-system patterns, the plugins.md spec, the display,
   the star, the controls, the icon, a launch flag, captures at 100% and 200%.
7. **Halo**, kept only if it sounds good enough.
8. **Docs**: architecture.md (the Listener), decisions.md (activity keying rather
   than level; opt-in lookahead), the user guide, CHANGELOG, roadmap.

## Open questions

1. **The name.** Vesper, or the runner-up Halo, which is plainer but taken by other
   audio products.
2. **The hue.** Thrift (afterglow pink) is shared with Glue. Straw would read as
   "evening star gold" but Corona has it.
3. **The wordmark in Newsreader italic.** No other hero effect uses the serif.
   The alternative is the instruments' style: capitals tracked wide, in sans.
4. **Default lookahead.** Off is safe for tracking. Long sounds best when mixing. An
   alternative is Off on a track that is armed and Long otherwise, but that changes
   the latency under the user.
5. **Halo in the first release**, or later?
