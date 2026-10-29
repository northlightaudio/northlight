# Plan: the modelled electric piano

A built-in electric piano, modelled rather than sampled: a tine piano first (the
Rhodes family), with a reed piano (the Wurlitzer family) as a second model on the
same parts. It fills the largest gap in the bundled instruments, which are all
synths. It is also the first step towards a modelled acoustic piano, which would
reuse its hammer, voices, noises and calibration tooling.

Name: **Lumen** (a lamp's glow; fits Aurora, Nebula, Corona). Type
`northlight.lumen`.

**Status: built; first calibration pass done, listening not yet.** Phases 0, 1,
3, 4, 5, 6 and 7 are built on the `lumen` branch: the tine voice, noises and
damper, the effects, presets, the editor and the docs, with the measurement
script and `--render-phrase` (phase 0). Phase 2 has had its first pass against
jRhodes3d (section 10 below); its listening rounds have not started. Phase 8
(the reed model) is not built.

Where the build differs from this plan:

- The hammer is not a simulated nonlinear spring. A simulated felt's force pulse
  has sharp edges, and its spectrum dips made a soft strike brighter than a hard
  one on some notes. The force is a Gaussian pulse as long as Hertz's law for the
  felt gives, so brightness always rises with velocity, and steeply enough that
  the bell grows 50 to 60 dB from the softest strike to the hardest, as the
  reference's does.
- The pickup is anti-aliased by ADAA on the flux (its antiderivative is an
  arctangent), not by 2x oversampling per voice, which would cost more than the
  voices. The aliasing test holds it 50 dB down at the top note, full bark and
  drive.
- The chorus is Lumen's own, in `plugins/dsp/Modulation.h` with the tremolo and
  phaser; Tide's is left as it is, so the pedal's sound cannot change.
- The phrase flag is `--render-phrase <type> <file.wav>`, for any built-in
  instrument, not `--lumen-phrase`.
- The editor is a rendered hardware face, the front of a suitcase piano (the
  user's choice, after the first editor of glass panels): the harp window's tines
  vibrate, the vibrato's lever and its two lamps show the tremolo, and every other
  parameter is a knob. The displays the plan described (the draggable tine, the
  velocity curve, the tone, the phaser and chorus) are not on it.
- There is no Model parameter yet: it is added (appended) with the reed model.

## Goals

- Sounds and plays like a good, well-voiced tine piano: a round, warm sound when
  played softly, the bell in the attack, and the bark when played hard. It
  sustains long in the low notes and short in the high ones, with a damper you
  can hear.
- Velocity changes the timbre, not just the level. This is the main reason to
  model it.
- Players can voice it like a technician would: hammer hardness, how close the
  tine sits to the pickup, and its offset (fundamental against bark).
- It has the effects people expect: the stereo tremolo (auto-pan) of the
  suitcase models, preamp drive, a phaser and a chorus.
- **No samples, no data files**: the whole instrument is code and a few tables
  of numbers. It ships in the binary like the other instruments.
- Light enough to play live: zero latency, and 32 voices with the sustain pedal
  down well inside one core at 64-sample buffers.
- Meets the design bar like Aurora: an editor that reads as its own product,
  every state designed.

## Not in this plan

- The acoustic piano. It is a separate, larger plan; this one builds the parts it
  would share.
- Clavinet, CP-70 style electric grand, toy piano. Each could be another model on
  the same parts later.
- Sympathetic string resonance. A tine piano has almost none; the acoustic piano
  needs it.
- MPE and poly aftertouch (the instruments' shared expression does not have them
  yet; when it does, Lumen gets them with the rest).

---

## 1. How a tine piano makes sound

What the model has to get right, in order of how much each part matters:

1. **Hammer.** A neoprene-tipped hammer strikes the tine. The tips get softer
   down the keyboard. A harder hit compresses the tip further, so contact is
   shorter and stiffer and excites more of the high modes: louder *and* brighter.
2. **Tine and tone bar.** The tine is a stiff steel rod clamped at one end, a
   cantilever. Its higher modes are inharmonic (about 6.3, 17.6 and 34.4 times
   the fundamental) and die within tens of milliseconds. That is the bell in
   the attack. The tone bar is a tuned brass resonator coupled to the tine,
   which carries the long, pure fundamental.
3. **Pickup.** The tine's tip vibrates in front of an electromagnetic pickup. The
   output follows how fast the magnetic flux changes, and the flux is a strongly
   nonlinear function of where the tip is. That nonlinearity is most of the
   character. A tip close to the pickup barks. A tip centred on the pole adds the
   second harmonic, and an offset tip gives more fundamental. Technicians voice
   the instrument by setting these two distances note by note.
4. **Damper and noises.** A felt damper stops the tine when the key comes up,
   with an audible thump. The action also makes a small mechanical knock that
   you hear even at low volume. Leaving these out is the usual sign of a
   synthetic electric piano.
5. **Preamp, tremolo, amp.** The suitcase models run through a preamp with bass
   and treble controls, a stereo tremolo that pans between the two speakers,
   and an amp that breaks up a little when pushed.

The reed piano (section 3) differs in parts 2 and 3: a clamped steel reed in
place of the tine and tone bar, and an electrostatic pickup in place of the
magnetic one. Part 1 (with a felt hammer) and everything after part 3 are shared.

Published work to start from (check these references in phase 0): Pfeifle on
real-time Rhodes and Wurlitzer models (DAFx 2017), Münster and Pfeifle on the
Rhodes pickup, and Gabrielli et al. on the Wurlitzer 200A's electronics.

## 2. Signal chain (DSP)

Each voice is mono up to the pickup. The effects run once, after the voices
are summed, and are stereo.

| Stage | What it models | How |
|---|---|---|
| Hammer | Neoprene tip against the tine; tip hardness graded down the keyboard | A mass on a nonlinear spring (force = k * compression^p, with hysteresis for the tip's losses), stepped per sample only while in contact (a few ms). Velocity sets the hammer's speed; hardness and p come from the note's register and the Hardness control. The contact force drives the modes below, so velocity changes the timbre by itself |
| Tine and tone bar | Fundamental plus three inharmonic beam modes | Modal synthesis: each mode is a complex one-pole resonator (a rotating phasor with a decay), exact in frequency and decay at any sample rate and cheap. The fundamental's decay is long (the tone bar); the beam modes' are short. The Bell control scales the beam modes |
| Pickup | Magnetic flux against the tip's displacement; the output is its rate of change | The tip's displacement (sum of the modes at the tip) goes through a flux curve built from a simple magnetic model, with a per-voice distance (Bark) and offset (Symmetry), then a first difference. Run at 2x through `plugins/Oversampler.h`, with antiderivative anti-aliasing on the curve as Corona's triode does |
| Damper | Felt on the tine at key-up | On note-off (or pedal-up) the modes' decays shorten over a few ms to the damper's rate; a small thump excites a low, heavily damped mode |
| Mechanical noise | Key, hammer and damper knocks | Short, filtered bursts of a few heavily damped modes, independent of the pickup, at a level the Noise control sets |
| Preamp | The suitcase's preamp, with bass and treble controls | A gentle triode stage (`TriodeStage` from `plugins/Triode.h`, Corona's) with Drive, and a two-band shelving tone control |
| Tremolo | The suitcase's stereo vibrato: an auto-pan between two speakers | Equal-power pan driven by an LFO with a shape control (sine to the original circuit's squarer wave), or mono amplitude tremolo; Rate in Hz or synced to the tempo |
| Phaser | The small four-stage phaser heard on many tine-piano records | Four (or six) first-order allpasses swept by an LFO, with feedback; Rate, Depth, Feedback, Mix |
| Chorus | A bucket-brigade chorus | Tide's chorus (`plugins/Tide.cpp`), moved into a shared struct both use, with Tide's sound unchanged |
| Output | Level, with a hard ceiling | Level and a soft safety clip, like the other instruments |

The tremolo and phaser are written as reusable structs in `src/plugins`. The
roadmap lists studio tremolo, auto-pan and phaser as missing effects, and those
effects can be built on the same parts later.

**Per-note data.** Each of the 73 notes (E0 to E6 in Northlight's naming,
MIDI 28 to 100) needs a fundamental, beam-mode ratios and levels, decay times for
the fundamental and each beam mode, hammer hardness, and pickup distance and
offset. These come from smooth curves across the keyboard with a few control
points each, not from 73 hand-tuned rows. The curves are fitted to measurements
in phase 2. Notes outside the 73 extrapolate the curves, so the full MIDI range
plays.

**Voices.** 32 by default. A note played again restarts its own voice,
because a real tine piano has one tine per key: the hammer strikes the tine
that is still ringing, which is the natural behaviour. When all voices are busy,
the quietest released voice is taken over, faded out over 5 ms. Pitch bend,
pressure (the shared vibrato) and the sustain pedal come from the instruments'
shared expression in `plugins/SynthVoices.h`. The mod wheel is Lumen's own: it
adds to the tremolo's Depth, up to full, so a player can ride the tremolo.

**Tuning.** A4 reference (default 440 Hz) and a small random detune per note
(Age, 0 to about 5 cents), fixed per note and not per strike, like a piano a
little out of tune.

### Controls

About 24 parameters, all automatable:

- **Hammer:** Hardness, Velocity (curve, from even to very dynamic), Noise
- **Tines:** Bell, Decay (scales every decay time), Damper (how fast notes stop), Age
- **Pickup:** Bark (distance), Symmetry (offset), Bass, Treble
- **Drive:** Drive
- **Tremolo:** Mode (pan, amplitude, off), Rate, Sync, Depth, Shape
- **Phaser:** Rate, Depth, Feedback, Mix (Mix 0 is off)
- **Chorus:** Rate, Depth, Mix
- **Output:** Model (Tine, or Reed once section 3 lands), Voices, Level, Tune (A4)

## 3. The reed model

The second model, chosen with Model. It reuses the hammer (with a felt tip),
voices, damper, noises and effects, and adds:

- **Reed:** a clamped steel reed tuned with a lump of solder. Same modal
  approach, with the reed's own mode ratios and much shorter decays; a reed
  piano is percussive.
- **Electrostatic pickup:** the reed is one plate of a capacitor against a
  charged comb. The output is nonlinear in the gap (roughly 1 / distance), which
  gives the reed piano's growl when played hard. It feeds a preamp with its own
  character.
- **Tremolo:** the reed piano's tremolo is amplitude only, so Mode defaults to it
  for this model.

It is its own phase because it needs its own calibration pass, and it ships
after the tine piano.

## 4. Calibration and listening

This is where most of the time goes, and the part Claude can't do alone: it can
measure, but it can't hear.

- **Measurement tool** (`scripts/analysis/ep-measure.py`, phase 0): reads a
  recording of single notes, finds each partial's frequency, level and decay,
  and writes them as rows. Run on reference recordings, it gives the targets.
  Run on Lumen's own output (rendered by a test or a launch flag), it gives the
  current values, so every change can be compared with the target in numbers
  before anyone listens.
- **References:** recordings of single notes at several velocities across the
  keyboard, from free multisampled electric pianos whose licence allows analysis.
  Phase 0 proposes the sets with their licences, and the user confirms them
  before any are measured. Only measured numbers enter the repository, never
  audio.
- **Listening checkpoints:** at the end of phases 1, 2, 3 and 4 the user plays it
  and gives notes. Listening uses the **release** build, since the debug build
  distorts on the user's interface whatever the instrument does. A launch flag
  renders a fixed test phrase (`--render-phrase <type> <file.wav>`: notes up the
  keyboard at three velocities, chords, a repeated note, pedal) so before and after can be compared by ear
  and by the tool.

## 5. Latency, CPU and real time

- `latencySamples()` is 0. The 2x oversampling is the shared IIR half-band, which
  is minimum phase.
- **CPU target:** 32 voices sounding, with effects on, under about 5% of one
  core at 48 kHz in the release build (to be confirmed by the first bench). A
  voice is about six resonators, a short contact model and a curve at 2x.
  Voices that have decayed below -100 dB stop.
- No allocation, locks or I/O on the audio thread. The per-note tables are
  computed in `prepare()` and when a parameter that shapes them changes, from a
  fixed-size array.
- Every parameter is smoothed. A Model change crossfades over 20 ms; voices
  sounding finish on the old model.

## 6. The editor (UI)

Like Aurora and Nebula, Lumen's editor reads as its own product while keeping to
the theme's palette and type. Its world is warm lamplight in a dark room, as
Aurora's is a night sky: a low amber glow from below, Gorse and Rowan the
lead hues, with Loch for the tremolo.

- **Header:** the wordmark LUMEN tracked wide in a Gorse-to-Rowan gradient,
  "ELECTRIC PIANO · NORTHLIGHT", the preset bar, a Tine / Reed switch, and voice
  lights.
- **KEYS panel** (the hero): a tine (or reed) in front of its pickup, drawn large.
  It vibrates with the last note played, at a visible slowed-down rate. Drag the
  tip towards or away from the pickup for Bark, and sideways for Symmetry.
  Hardness, Bell, Decay, Damper, Noise, Age.
- **HAMMER:** the velocity curve, with the last few notes' velocities marked on
  it as they are played. Velocity, Hardness.
- **TONE:** the preamp's shelving response; drag for Bass and Treble. Drive.
- **TREMOLO:** two speaker cones whose light swings between them at the rate (or
  pulses together in amplitude mode). Mode, Rate, Sync, Depth, Shape.
- **PHASER / CHORUS:** one panel with a chip to switch, each showing its sweep.
- **OUTPUT:** a scope, Level, Tune, Voices.
- Animated parts stop when silent or with reduce motion on, as on the other
  instruments.

**Before building:** the screen spec as `screens/plugins.md` 7.4i, any new pattern
(the draggable tine) in `docs/design/design-system.md` first, any new tokens in
`tokens.json` with contrast pairs. Captures at 100% and 200% with
`--midi-track-with northlight.lumen --instrument-editor --capture`. The card joins
`uiperf::builtInTypes()` and passes the work budgets and the release `perf` label.

## 7. Presets

Through `EffectPresets`, like Aurora's: Lumen (the default: well voiced,
gentle tremolo), Ballad (soft, dark, long), Bark (close pickup, driven), Bell
Keys (hard hammers, high Bell, the 1980s bright sound), Phase Funk (phaser,
some bark), Suitcase Pan (deep stereo tremolo), Lo-fi Keys (aged, dark,
chorus), and with the reed model Reed Grit and Reed Tremolo.

## 8. Tests

In `tests/unit/plugins/LumenTests.cpp` (and `tests/unit_juce` for the editor):

- **Pitch:** each note's fundamental within 1 cent of equal temperament at
  44.1, 48 and 96 kHz (Age 0).
- **Velocity changes the timbre:** the spectral centroid of the first 50 ms
  rises with velocity on every register, not just the level.
- **Voicing:** raising Bark raises the level of harmonics 2 to 5 against the
  fundamental; moving Symmetry to the centre raises the second harmonic.
- **Bell:** the beam modes are present in the first 20 ms and below -60 dB
  relative by 300 ms (at default Decay).
- **Register:** decay time falls monotonically up the keyboard.
- **Damper and pedal:** a released note falls 60 dB much faster than a held one;
  with the pedal down it does not, until the pedal comes up.
- **Repeated note:** restarts the same voice, with no click (no step larger than
  a threshold).
- **Aliasing:** a high note at maximum velocity, Bark and Drive leaves no energy
  at non-harmonic bins above a floor.
- **Real time:** no allocation on the audio thread (the debug hook), through
  note storms, Model changes and preset loads; no denormals after silence;
  voices stop when decayed.
- Same sound at 44.1, 48 and 96 kHz (spectra within tolerance); determinism;
  latency 0; state round trip; every preset loads; all parameters automate.
- **Effects:** the tremolo's pan is equal power; the phaser's notches move with
  its LFO; the sync rate follows the tempo.
- **Bench:** CPU for 8, 16 and 32 voices with effects on and off.

No project schema change: Lumen is a new insert type with parameters, like the
other instruments.

## 9. Phases

Each phase ends buildable, tested and formatted, and is its own commit.

0. **Groundwork:** check the references; the measurement script; pick and
   measure reference recordings (numbers only); the phrase-render launch flag.
1. **Tine voice:** hammer, modes, pickup at 2x, damper; registration in
   `BuiltInFactory` with the generic editor; pitch, velocity, voicing and
   real-time tests; bench. *Listening checkpoint.*
2. **Calibration:** fit the per-note curves to the measurements, across the
   keyboard and at several velocities. *Listening checkpoint* (expect several
   rounds).
3. **Noises and release:** mechanical noise, damper thump, pedal behaviour,
   repeated notes. *Listening checkpoint.*
4. **Effects:** preamp and tone, tremolo, phaser, chorus, as reusable structs.
   *Listening checkpoint.*
5. **Presets.**
6. **Design spec and editor:** spec, patterns, tokens, the editor, captures at
   100% and 200%, UI performance budgets.
7. **Docs:** architecture.md (the modal voice, the pickup), decisions.md (modelled
   rather than sampled; the per-note curves), user guide, CHANGELOG, roadmap
   (six bundled instruments).
8. **Reed model:** reed modes, electrostatic pickup, its calibration, its
   presets, the Tine / Reed switch going live. Its own listening checkpoints.

Phases 1 to 3 are the risky part. If after phase 2 it does not sound good to the
user, stop there and rethink before building the editor.

## Decisions

The open questions, answered on 2026-10-29:

1. **Name:** Lumen.
2. **Reed model:** ships after the tine piano. Lumen's first release is phases 0
   to 7; phase 8 follows, and the Model switch shows only Tine until it lands.
3. **Mod wheel:** rides the tremolo's depth, as a player does on a suitcase.
   Channel pressure keeps the shared vibrato.
4. **References:** free multisampled electric pianos whose licences allow
   analysis. Phase 0 proposes the sets and the user confirms them before any are
   measured.
5. **Key range:** 73 keys voiced (E0 to E6); notes outside extrapolate the
   curves.

### Reference sets (jRhodes3d chosen on 2026-10-29)

- **jRhodes3d** (Jeff Learman; [sfzinstruments listing](https://sfzinstruments.github.io/pianos/jrhodes3d)):
  a 1977 Rhodes Mark I Stage 73, 67 full-length unlooped samples, up to five
  velocity layers, 44.1 kHz 16-bit FLAC with an SFZ map. The best fit: a tine
  piano of exactly the kind Lumen models, every note decaying naturally. Its
  samples are CC BY-NC 4.0 (the rest CC0). Measuring them and keeping only the
  numbers redistributes nothing, but the non-commercial term should be read
  before a commercial release relies on the calibration.
- **jRhodes3c**, the older looped version: CC BY-NC-SA 4.0, and the loops cut the
  decays short, so it is only a fallback for attack measurements.
- Not usable: ETI Roads (a Kontakt library; no licence found), VCSL (CC0, but no
  electric piano).

## 10. Calibration record

### First pass, 2026-10-29: against jRhodes3d

**Reference.** jRhodes3d's mono WAV version (`jlearman/jRhodes3d-wav`, the
`jRhodes3d-mono-wav` folder: 65 samples, 15 notes from F1 to C7, up to five
layers), downloaded to `H:\Tools\jRhodes3d-wav` and never copied into the
repository. The shipped SFZ maps layer `_5` to the softest band and `_1` to the
hardest (its `layers.txt` says the reverse; the SFZ is what plays); each layer is
measured at its band's centre velocity (24, 60, 84, 104, 120). The README says
the set was recorded through EQ (a treble boost and a low-mid scoop, to bring
out the bell), each layer levelled across the keyboard, the layers about 3 dB
apart. So absolute harmonic levels carry that EQ and loudness carries nothing:
the pass matches harmonic levels against the fundamental, the bell, and ring
times, and holds the level even across the keyboard.

**Loop.** `scripts/analysis/lumen-calibrate.ps1 -Listing <notes.csv>` renders
Lumen at the reference's notes and velocities (the hidden `[.calibrate]` unit
test of the Release build), measures both with `ep-measure.py` and prints the
difference by register and strike. `-Set "index=value,..."` tries parameter
values without a build.

**What changed in the model.**

- The fundamental's ring time is a table measured from the reference (24 s in
  the bass to 1.2 s at the top), not a log line: the reference holds 15 to 25 s
  up to about D4 and then falls fast.
- The bell's ratio is 6.4, not a clamped rod's 6.27 (the reference measures 6.3
  to 6.5 through the middle), and its share of the tip's motion 0.5.
- The strike is a Gaussian pulse with the felt exponent p = 7 (contact time as
  v^-0.75); the contact at a full strike is a table: 2 ms in the bass, about
  0.8 ms from D3 to A4, short again at the top. That is what keeps the bell
  strong from D3 to D4 and faint from F4 to A5, as the reference's is.
- A tine's swing past the pickup is a table: 3.2 in the bass (which barks most
  when hit hard), 1.7 in the middle, 0.3 at the top (nearly pure). Each strike's
  level is evened out (the swing and the pulse's roll-off of the fundamental,
  at most 20 dB), so the keyboard plays at one level and only the timbre
  follows the register; a test holds it within 12 dB.
- The defaults: Velocity 30% (the soft strikes' harmonics matched the
  reference's only with a gentler velocity curve) and Symmetry 45% (more second
  harmonic, as the reference has).

**Where it stands** (Lumen minus the reference; dB, and Lumen's ring time over
the reference's):

| Register | Strike | Harmonic 2 | Harmonic 3 | Harmonic 4 | Bell | Ring |
|---|---|---|---|---|---|---|
| Low, F1-A2 | hard | +1.8 | -2.0 | -8.6 | -15.9 | 0.93 |
| Low, F1-A2 | soft | +5.9 | +10.1 | -8.1 | +31.3 | 0.85 |
| Middle, D3-F4 | hard | -0.3 | -1.2 | -18.3 | +3.5 | 1.07 |
| Middle, D3-F4 | soft | +3.0 | +7.3 | +2.3 | -0.5 | 0.95 |
| High, B4-A5 | hard | -1.9 | +4.0 | +4.1 | -5.8 | 1.01 |
| High, B4-A5 | soft | -2.1 | +9.0 | -4.7 | -24.1 | 0.91 |
| Top, D6-C7 | hard | +4.9 | +24.5 | +2.7 | -23.2 | 0.97 |
| Top, D6-C7 | soft | +3.5 | +18.0 | -11.3 | -47.6 | 0.92 |

**Still open, for the listening rounds.**

- The bass's bell: with the bass's large swing the pickup smears the bell into
  sidebands, so it measures 16 dB weak on a hard strike and 31 dB strong on a
  soft one. It may need the bell to bypass part of the pickup's curve, or a
  smaller swing with a closer pickup; listening decides which sounds right.
- The third harmonic is too strong on the top keys (+18 to +25 dB), though at
  -45 dB or so under the fundamental; soft strikes have a little too much third
  everywhere (+7 to +10).
- The fourth harmonic is weak on hard bass and middle strikes (-9 to -18 dB).
- The bell is weak on soft high and top strikes.
- The damper: the reference has no releases, so the damper times are still the
  first guesses (the SFZ's release times, 0.5 s in the bass to 0.2 s at the top,
  are a sampler's envelope, not a measurement).
- All of it against one instrument, voiced to one player's taste through EQ:
  the user's ear is the judge from here.
