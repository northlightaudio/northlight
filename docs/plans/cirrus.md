# Plan: Cirrus, an FM synth

A built-in FM synth, the sixth bundled instrument. Aurora covers warm subtractive
sounds, Nebula pads, Starbit chip sounds and Pulsar drums; none of them makes the
sounds FM is known for: electric pianos, bells, glassy keys, mallets, slap and
plucked basses, brass stabs and metallic textures. Cirrus does, without making the
user think in operator maths.

Name: **Cirrus**. Type `northlight.cirrus`. In the instrument menus after Starbit,
described as "FM synth".

**Status (2026-10-29):** phases 1 and 2 are built on the `cirrus` branch: the FM
core (`plugins/fm/Fm`) and the instrument (`plugins/Cirrus`) with the generic
editor. The build went ahead on the plan's
answers to the open questions: four operators, no patch import, High quality by
default. Phase 3 has all ten presets written as best guesses, each tested to sound
and stay below full scale across the keyboard; they still need tuning by ear on a
release build. Phases 4 and 5 are done: the face spec (screens/plugins.md 7.4j),
its patterns in design-system.md and `ui/strip/CirrusEditor`, with
`cirrus::Activity` for its live parts, captured at 100% and 200%. The first face,
an evening sky over glass panels, was replaced by a precise one (four mockups
were drawn and this one chosen): the sidebands of the selected operator over a
table of all four. Phase 6 is
done: the changelog, the user guide, the roadmap, the README and architecture.md.
What is left is listening: tuning the presets by ear on a release build, then the
`SynthFingerprintTests` entry once the sound is final.

## The name and the idea

Cirrus are the thin, high clouds made of ice. In the early evening, after the land
is in shadow, they still catch the sun and burn gold, then pink, then violet, while
the sky under them goes blue. Their ice crystals bend the light into halos and
sundogs: bright copies of the sun standing either side of it.

That is FM's sound and how it works. FM is thin, bright and glassy, and its
brightness changes as a note fades, as the colour on the cloud does. A modulator
makes sidebands, bright copies of the note either side of it, as the crystals make
sundogs. The name sits with Aurora and Vesper in the Machair dusk brand.

The face keeps the idea in what it shows rather than in a picture: the display
draws the sidebands themselves, the copies of the note either side of it, so a
user can see why turning up a modulator makes the sound brighter, and the
routing is drawn as a diagram.

## Goals

- **The classic FM sounds in a few presets and a few knobs.** Every preset sounds
  finished and has its character on two macros, Tone and Time.
- **Readable routing.** Four operators and eight algorithms drawn as a diagram you
  click, not a number you look up in a manual.
- **Clean at high notes and high indexes.** Aliasing is the usual failure of a
  naive FM synth; Cirrus keeps it down by design (below) and is tested for it.
- **Cheap.** Sixteen voices at 48 kHz under 1.5% of a core (release), so a song can
  run several instances.
- **Plays like the other built-ins:** the same velocity, pitch bend, mod wheel,
  pressure and sustain pedal behaviour (`synth::Expression`), the same voice
  allocation and the same preset bar.

## Not in this plan

- **Six operators and the 32 DX7 algorithms.** Four operators cover the classic
  sounds with far fewer controls. Six is a later option, not a different design.
- **Importing DX7 or TX81Z patch files (SysEx).** Possible later for four-operator
  banks; it needs a parser, a mapping table and tests against real banks.
- **MPE and per-note expression.** None of the built-ins has it yet; when it lands
  it lands for all of them.
- **A full filter section.** Cirrus has one gentle low-pass for taming the top
  (High cut), not Aurora's multimode filter. FM shapes its tone with the modulators.
- **Wavetable operators or sample playback.** Sine plus a small set of waves only.

## Sound engine

### Operators

Each voice has four operators. An operator is a phase accumulator, a wave and an
envelope:

- **Frequency:** Ratio (0.5, then 1 to 32, coarse steps) plus Fine (0 to 0.99 of
  the next step) and Detune (+-7 cents, so stacked carriers beat), or **Fixed**
  (1 Hz to 9.75 kHz, ignoring the note: for drum and bell partials).
- **Wave:** Sine, and the TX81Z-style half and rectified sines (eight in all). Each
  wave is a 2048-point table with linear interpolation, built once at static
  initialisation, never on the audio thread.
- **Level:** 0 to 99 on a dB curve, as FM users expect: for a modulator this is the
  modulation index, for a carrier its loudness.
- **Envelope:** Attack, Decay, Sustain, Release. Reuses `synth::Envelope::step()`
  (linear attack, exponential decay and release), which multiplies the level:
  the exponential decay is a straight line in decibels, so it sounds natural.
- **Velocity** (0 to 1): how much velocity scales this operator's level. On a
  modulator that is how hard playing gets brighter, the core of FM expression.
- **Key scale** (-1 to 1): the level falls (or rises) above middle C. Keeps the
  top of the keyboard from going harsh.

Operator 4 has **Feedback** (0 to 1): it modulates itself with the average of its
last two samples (the DX approach, which stays stable), turning a sine towards a
saw or, at the top, noise.

Modulation is **phase** modulation: a modulator's output is added to the phase of
the operator it feeds. That is what the DX family did, and it keeps the pitch
centred at any index, where true frequency modulation drifts.

### Algorithms

Eight, the four-operator set (shown here carriers last, `>` meaning "modulates"):

| # | Routing | Typical use |
|---|---|---|
| 1 | 4 > 3 > 2 > 1 | Brass, leads, bass |
| 2 | (3 + 4) > 2 > 1 | Brass, strings |
| 3 | 3 > 2 > 1, 4 > 1 | Bass, plucks |
| 4 | 4 > 3 > 1, 2 > 1 | Plucks, keys |
| 5 | 2 > 1, 4 > 3 | Electric piano, layered keys |
| 6 | 4 > 1, 4 > 2, 4 > 3 | Organ-like, pads |
| 7 | 4 > 3, 1, 2 | Organ, layered tones |
| 8 | 1, 2, 3, 4 | Additive: drawbar organ |

An algorithm is a constant table (which operator feeds which, and which are
carriers), so the voice loop has no branching on it beyond a switch per block.
Carriers are summed and scaled by `1 / carrierCount` so switching algorithm does
not jump in level.

### Voice, pitch and the rest

- **Sixteen voices**, `synth::allocateVoice`, retrigger and stealing as the other
  synths. **Mono** with **Glide** (0 to 2 s, legato only) as a mode.
- **Pitch envelope:** Amount (+-24 semitones) and Time (decay to zero). Gives
  plucks and drums their snap.
- **LFO:** Sine, triangle, saw, square, random; Rate (0.05 to 20 Hz, or synced
  with `BeatSync`); to Pitch (cents) and to **Tone** (modulator depth). Mod wheel
  and pressure deepen the vibrato through `synth::Expression`, as everywhere else.
- **Macros:** **Tone** (-1 to 1) scales every modulator's level at once: the
  brightness knob. **Time** (-1 to 1) scales every envelope's times: shorter or
  longer notes without editing four envelopes.
- **Output:** High cut (a `dsp::Svf` low-pass, 1 to 20 kHz; not called Tone, which
  is the macro), Width (the carriers spread evenly from left to right in operator
  order, so algorithm 5's two carriers sit either side; one carrier stays
  centred), Level (dB).

### Aliasing

FM makes partials far above the note. Three measures keep it down:

1. **Oversampling.** The voices run at 2x and the summed stereo output goes
   through one `Oversampler` per channel back down: one down-sampler for the whole
   instrument, not one per voice. A **Quality** choice offers Eco (1x) and High
   (2x, the default).
2. **Key scale** defaults to a gentle negative value on modulators in the presets.
3. **An index limit near Nyquist:** a modulator's effective level is faded when its
   frequency times its index would put strong sidebands past the oversampled
   Nyquist. Cheap (one multiply per control interval) and inaudible below it.

Control-rate work (envelopes to dB, key scaling, LFO, pitch) runs every 16 samples
as Aurora's does, interpolated across the interval; the per-sample loop is four
table reads and four phase updates per voice.

### Real-time rules

Everything is fixed-size and allocated in `prepare()`: voices are a
`std::array`, tables are static constants, the oversampler is a member. The process
path is `noexcept`, takes no locks and does no I/O, and the debug allocation hook
covers it in the tests. Denormals are flushed (`dsp/Denormals.h`); a voice whose
carriers' envelopes are all off is inactive.

## Files

| Where | What |
|---|---|
| `src/plugins/Cirrus.h`, `.cpp` | Parameters, presets, the processor (on `MeteredBuiltIn`), and `Activity`, what the face reads live |
| `src/plugins/fm/Fm.h`, `.cpp` | Wave tables, the algorithm table, the level curve and `besselJ` (the sidebands' amplitudes). JUCE-free |
| `src/plugins/BuiltIns.cpp` | Registry entry: `{cirrus::type, "Cirrus", instrument, ..., pl::cirrusPresets, "FM synth"}` |
| `src/plugins/EffectPresetLists.h`, `EffectPresets.cpp` | `cirrusPresets()` |
| `src/ui/strip/CirrusEditor.h`, `.cpp` | The face |
| `tests/unit/plugins/CirrusTests.cpp` | DSP tests |
| `tests/unit_juce/` | Editor and capture tests alongside Aurora's |

No project schema change: built-in parameters are saved by key and a new type
needs no new model fields. The ParameterKeys golden list gains Cirrus's entry.

## The face (specified in screens/plugins.md, 7.4j)

The spec is the definition of done. The face is a flat, ruled instrument, about
1000 px wide and 378 px tall:

- **Top bar:** CIRRUS in the mono face, eight algorithm buttons with the routing
  as text beside them ("2 → 1 · 4 → 3"), the preset bar and sixteen voice lights.
- **ROUTING:** the chosen algorithm as a diagram, each operator a square in its
  hue (1 Thrift, 2 Gorse, 3 Sea grass, 4 Loch), op 4's feedback loop to drag, and
  the Feedback knob. Click an operator to select it.
- **The display:** the selected operator's pair as the carrier and five sidebands
  either side, as tall as their Bessel amplitudes at the note being played, or the
  operator's envelope to drag with its four times as cells.
- **PITCH · LFO:** Rate, Pitch, Bright, Shape; Bend, Bend time, Glide, Sync; Mono.
- **The operators' table:** a row per operator (ratio or fixed frequency, fine,
  detune, wave, level with its live level, envelope, velocity, key scale), every
  value edited where it shows.
- **TONE:** Tone and Time large, then High cut, Width and Level, and Quality.

The live parts read a `cirrus::Activity` (the newest voice's note, each
operator's level and the modulation depth), as Pulsar's does. The routing diagram
and the parameter table are patterns in design-system.md. Captured at 100% and
200% and checked by eye.

## Presets

Ten, each tuned by ear and measured (level within 1 dB of Aurora's presets, no
clipping across the keyboard at velocity 127):

| Name | Sound | Algorithm |
|---|---|---|
| Cirrus | The default: a clean, bright FM keys sound that shows off Tone | 5 |
| Ice Crystal Keys | Tine electric piano, bark on velocity | 5 |
| Evening Bells | Inharmonic bell (ratio 3.5 modulator), long release | 4 |
| Glass Mallets | Marimba / vibes, short pitch envelope | 3 |
| Low Sun Bass | Punchy FM bass, feedback for grit | 1 |
| Gold Brass | Brass stab, slow modulator attack | 2 |
| Drawbar Haze | Additive organ, feedback for key click | 8 |
| Mare's Tails | Detuned carriers, slow LFO on Tone | 6 |
| Jet Stream Lead | Mono lead with glide | 1 |
| Hail | Metallic percussion, fixed-frequency modulators | 4 |

## Tests

In `tests/unit/plugins/CirrusTests.cpp`, named for the behaviour:

- A carrier alone plays a sine at the note's pitch (measured with `Yin.h`), at its
  Ratio, and at its Fixed frequency regardless of the note.
- A modulator at level 0 changes nothing; at a known index the first sideband sits
  at the expected frequency and level (Bessel J1) within tolerance.
- Each algorithm routes as its table says: muting each operator in turn changes
  the output only where the table says it should.
- Feedback at maximum on operator 4 stays bounded and makes a saw-like spectrum.
- Velocity sensitivity on a modulator makes a loud note brighter (higher spectral
  centroid) than a soft one; on a carrier only louder.
- Key scale lowers a modulator's effect at C7 compared with C4.
- Aliasing: a high note with a high index at 2x has its strongest component below
  the note at least 50 dB under the fundamental; Eco is allowed to be worse.
- Same sound at 44.1 kHz and 96 kHz (fingerprint of the spectrum within tolerance).
- Voices end: after release every voice goes inactive and the output is exactly
  zero; denormals do not appear.
- Glide and Mono: legato notes glide, detached notes do not.
- No allocation while processing (the debug hook), including preset changes.
- Presets: each loads, plays a phrase without clipping, and round-trips.

Shared suites gain an entry: `SynthFingerprintTests` (once the sound is final),
`ParameterKeysTests`, `EffectPresetsTests`, `BuiltInTests`, `PluginMenuTests`, and
`uiperf::builtInTypes()` for the face's paint budget.

## Phases

1. **Operator and algorithms.** `fm/Fm.h`, wave tables, the algorithm table,
   with the routing, sideband and feedback tests. No registry entry yet.
2. **The instrument.** `Cirrus.cpp`: voices, envelopes, macros, pitch envelope,
   LFO, Expression, Mono and Glide, the output stage and the 2x oversampling.
   Registry entry, generic editor, the remaining DSP tests, a CPU measurement in
   release.
3. **Presets.** The ten above, tuned by ear on the release build (debug distorts on
   the Volt), measured, then the fingerprint test.
4. **Face spec.** Write 7.4j in `screens/plugins.md` and any new patterns in
   `design-system.md`; agree it before building.
5. **The face.** `CirrusEditor`, captures at 100% and 200%, the perf label run.
6. **Docs.** architecture.md (instrument list and the FM engine), decisions.md
   (four operators, phase modulation, voice-sum oversampling), user guide,
   CHANGELOG.md Unreleased, roadmap ("six bundled instruments").

Each phase builds clean with `-Preset ci`, passes `ctest` and is clang-formatted.

## Open questions

1. **Four or six operators?** This plan says four. Six means the DX7's 32
   algorithms and about 40% more controls, and a busier face.
2. **Patch import.** Worth planning TX81Z / DX100 SysEx import as a follow-up, or
   not at all?
3. **Default quality.** High (2x) costs about double the CPU. Fine as the default,
   or Eco by default with High for bounces?
