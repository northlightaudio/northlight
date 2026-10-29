# Plan: the amp modeller

A built-in tube amp modelled on a Fender Twin Reverb, with a second page for the
cabinet and microphones. It is the first of a guitar and bass suite; the pedals
come later and reuse its parts (oversampler, triode stage, convolver, tuner).

Working name: **Corona** (fits Aurora, Bloom, Patina; easy to change). Type
`northlight.amp`.

**Status: done.** Phases 1-7 shipped on `main` (commit 1a7e1c1, "Add Corona");
see CHANGELOG.md. Where the build differs from this plan: the tone stack is solved from
the circuit's nodal matrix (Yeh's published polynomial, as transcribed, was off by
a few percent); the page switch is a pair of keys on the strip and changes
instantly, with no slide; the master's pull boost is a lever. Of the follow-ups,
the tuner (Polaris) and the first five pedals have since shipped; low-latency
monitoring and more amp models are in the roadmap, not built.
The open questions below were answered with the plan's defaults: Corona, one
channel (Vibrato), silverface features, bass cabinets shipped now.

## Goals

- Sounds and behaves like a Twin Reverb: huge clean headroom, a scooped Fender
  tone stack, bright switch, spring reverb, tremolo, and an edge of breakup only
  when pushed.
- Usable for tracking: **zero reported latency**, low CPU at 64-sample buffers,
  and no zipper noise when knobs move.
- Two pages on one hardware face: the amp's front panel, and a cabinet page
  where mics are placed by dragging them on the speaker.
- Meets the design bar like the tape echo: rendered hardware face, every state
  designed, keyboard and tooltips on every control.

## Not in this plan

- Pedals (drive, fuzz, chorus, compressor, wah and so on). They follow, as their
  own built-ins, once the shared parts below exist.
- Other amp models (a bass amp, a British amp). The DSP is built as data plus
  stages so a second model is mostly new values, but only the Twin ships first.
- Neural / captured amp models.

---

## 1. Signal chain (DSP)

Mono core (a guitar is one channel; a stereo input is summed), stereo only at
the mics. Everything nonlinear runs oversampled.

| Stage | What it models | How |
|---|---|---|
| Input | Input 1 (high) / Input 2 (-6 dB), the guitar's level | Input trim in dB with a meter, calibrated so a typical DI strum lands where the real amp expects volts. A level reference constant, documented |
| Noise gate | Not on the Twin, but needed once the amp is pushed | Simple gate with threshold (off by default), hysteresis and hold. Before the amp so hum isn't amplified |
| V1 preamp triode (12AX7) | First gain stage | Triode stage (below) |
| Bright switch + volume pot | Bright cap across the volume pot; audio-taper pot | High shelf whose depth falls as volume rises (as on the real amp), pot taper curve |
| Tone stack | Fender treble / middle / bass | The exact 3rd-order passive network (Yeh's analysis), redesigned from the three pot values and discretised by bilinear transform. Recomputed per 32-sample sub-block while a knob moves |
| V2 triode | Recovery stage | Triode stage |
| Spring reverb | 12AT7 driver, 3-spring tank, 12AX7 recovery | Dispersive spring model (the tape echo already has a short one to build on): chirped all-pass chains, the tank's band limits, drip from the driver clipping when Reverb is high |
| Tremolo ("Vibrato" on the panel) | Opto cell shunting the signal | LFO into an opto model (the cell's slow, asymmetric response, so it isn't a sine), Speed and Intensity, plus a footswitch parameter |
| Phase inverter (12AT7, long-tail pair) | Symmetric clipping at high volume | Soft symmetric curve with its own headroom |
| Power amp (4 x 6L6, push-pull AB) | Clean headroom, then a firm squash | Push-pull curve with a slight crossover, plus negative feedback modelled as a loop around the stage |
| Sag | Supply droop under load (silicon rectifier: tight) | Envelope that lowers headroom; small on a Twin, a "Sag" amount exposed for taste |
| Output transformer + speaker load | Low-end saturation, treble rolloff, the speaker's impedance bump | Filters on the power amp's output, scaled by the feedback amount |
| Master + Boost | The 1970s silverface Twin's master volume with pull boost | Master after the preamp, Boost adds gain before V2. Gives drive at sane levels, true to the real amp |

**Triode stage**: coupling high-pass, grid current (a bias shift that follows
the signal, giving the "blooming" compression), an asymmetric curve read from a
table solved offline from the Koren triode equations on the stage's load line,
cathode bypass shelf and Miller low-pass. Antiderivative anti-aliasing (ADAA)
on the curve, as the tape echo and exciter already do. One reusable struct;
the pedals will use it too.

**Oversampling**: 4x by default through cascaded polyphase IIR half-bands
(the exciter already has a 2x one in `Exciter.cpp`: move it to a shared
`plugins/Oversampler.h`). IIR is minimum phase, so it adds only a few samples
of group delay and **no reported latency**. A quality parameter offers 2x
(lowest CPU for tracking) and 8x.

**Controls on the amp page** (matching the real panel, one channel):
Bright, Volume, Treble, Middle, Bass, Reverb, Speed, Intensity, Master, Boost,
plus the extras not on the real amp kept visually separate: Input trim, Gate,
Sag, Output. Normal / Vibrato channel choice is a later question; one channel
(Vibrato, which has the reverb and tremolo) is enough to start.

## 2. Cabinet and microphones

**Cabinets** (selector on the cab page):
- 2x12 open back (the Twin's own; default)
- 1x12 open back, 4x10 closed back
- Bass: 1x15 and 4x10 closed back, so the amp is usable on bass from day one
- Custom: load an impulse response (.wav) from disk

**Mics**: two mic slots, A and B, each with:
- Type: Dynamic, Ribbon, Condenser (generic names, no brands)
- Position on the cone: dragged on the speaker picture, centre (bright) to edge
  (dark)
- Distance: 1 cm to 1 m, with proximity effect for the directional mics
- Angle: on axis to 45 degrees off axis
- Level, pan, polarity, on/off
- The mic distance difference sets a real time-of-flight offset between A and B,
  so blending two mics gives the phase interplay engineers expect
- A Room amount: an early reflections / small room blend; and the open back's
  rear wave (delayed, inverted, low-cut) on open-back cabinets

**How**: we cannot ship commercial impulse responses, so the cab is a parametric
model that **builds** an IR: the speaker's response (low resonance, cone
breakup peaks, rolloff), the cabinet (open-back cancellation, closed-back
bump), and the mic (type curve, off-axis loss, proximity, position on the cone
shifting the breakup and treble). A background thread turns the magnitude
response into a minimum-phase IR (cepstrum), about 2048 taps, and publishes it
to the audio thread with the existing snapshot publisher; the audio thread
crossfades old to new over a few ms, never allocates, never frees.

**Convolver**: zero-latency partitioned convolution: the first 64-128 taps run
directly per sample, the rest as FFT blocks whose output is only needed later,
so the result is exact with no added latency. FFT from **signalsmith-linear**,
already fetched for the stretcher (MIT, no new dependency). One convolver per
mic. A user's IR goes through the same convolver (resampled to the project rate
when loaded, truncated to a fixed maximum length).

The user IR's file path lives in `core::Insert::state` (opaque state the
Sampler already uses for its files), so **no project schema change** is needed.

## 3. Latency, CPU and live use

- `latencySamples()` returns 0. A test holds it.
- **CPU budget**: measured with a bench test like `StripBenchTests`: target
  under about 3% of one core per instance at 48 kHz, 4x oversampling, two mics
  (to be confirmed by the first measurements). Mono core, SIMD-friendly inner
  loops.
- All parameters smoothed; tone stack and cabinet changes crossfade.
- **Recording records the DI** (inputs are captured before the inserts), so a
  player tracks through the amp and can change the amp afterwards: reamping
  for free. This deserves a line in the user guide.
- **Engine follow-up: low-latency monitoring.** A monitored track is delay
  compensated like any other, so a latent plugin elsewhere (the master
  limiter's lookahead, a hosted linear-phase EQ) delays what the guitarist
  hears. A "Low-latency monitoring" option (per project, toggled from the
  transport) should leave monitored tracks uncompensated and bypass latent
  inserts on the monitored path while recording. This is separate engine work,
  but tracking guitar is miserable without it.
- **Tuner**: guitarists expect one beside the amp. A tuner built-in (pitch
  detection on the input, no audio change, mute switch) is the natural first
  pedal and should land straight after the amp.

## 4. The face (UI)

A hardware face like the tape echo's: Blender renders under one top light,
drawn at one size and scaled to fit (`ModuleCard::preferredWidthAt`).

**No Fender trade dress.** No logos, no "Twin", no script nameplate, no exact
silver panel. The UI says "American 2x12 combo" at most; the docs can say what
it is modelled on. Our own nameplate, our own panel finish.

**Amp page**: the front panel: a row of numbered skirted knobs (1-10, as on the
real amp, with the Bright and Boost as small toggles), a jewel pilot lamp that
glows with the output level, a small input meter next to the trim, and the
extras (Gate, Sag, Output, Quality) in a separated strip so they don't read as
part of the vintage panel.

**Cab page**: the speakers seen with the grille cloth off: the two mics drawn
over the cones, dragged to place them (horizontal drag = position on the cone,
a side handle or scroll = distance), with each mic's controls below. A small
side view shows the distance and angle. Each mic is selected by clicking it or
by tab; keyboard moves the selected mic.

**Switching pages**: a hardware-style toggle in the face (labelled "Amp" /
"Cabinet"), with a short slide transition from `Motion.h`. The page is UI state,
kept per editor while the app runs, not saved in the project.

**Before building**: add a "two-page hardware face" pattern to
`docs/design/design-system.md` (rule: patterns first), the screen spec to
`screens/plugins.md` 7.2a, new `hardware` tokens to `tokens.json` with contrast
pairs, renders and their scripts in `assets/amp/` and `scripts/render/amp/`.
Capture at 100% and 200%, both pages (needs a launch flag to open the cab page).

## 5. Presets

Factory presets through `EffectPresets`, a few that show the range: Clean
Sparkle, Surf (reverb high), Tremolo Swamp, Edge of Breakup, Boosted Crunch,
Bass DI-ish (4x10, condenser far), Funk Clean (bright, mids low).

## 6. Tests

In `tests/unit/plugins/AmpTests.cpp` (and `tests/unit_juce` for the face):

- Tone stack magnitude matches the analytic network at a grid of knob
  positions and frequencies.
- Headroom: at Volume 3 the output is clean (THD under a threshold); at
  Volume 8 it breaks up; Boost raises distortion.
- Aliasing: a high sine at full drive leaves no energy at non-harmonic bins
  above a floor, at 2x, 4x and 8x.
- Zero latency: an impulse's first output sample is at sample 0.
- Same sound at 44.1, 48, 96 kHz (spectra within tolerance).
- Cabinet IR builder: deterministic; edge position is darker than centre;
  distance lowers the bass for directional mics (proximity); minimum phase.
- Convolver equals direct convolution to float precision, at any block size,
  including blocks longer than `prepare()` announced.
- Real-time: no allocation on the audio thread (the debug hook), IR swaps and
  user IR loads included; no denormals on silence after a loud note.
- Bypass, mix, presets, automation of every parameter, state round trip of the
  custom IR path.
- Face arithmetic (mic drag mapping, knob scales) held like `TapeEchoFace.h`.
- Bench: CPU per instance at 2x / 4x / 8x.

## 7. Phases

Each phase ends buildable, tested and formatted, and is its own commit.

1. **Shared DSP parts**: move the half-band oversampler to
   `plugins/Oversampler.h` (exciter uses it, unchanged sound), the triode stage,
   a zero-latency convolver on signalsmith-linear's FFT. Tests for each.
2. **Amp core**: the chain from input to transformer, parameters, registration
   in `BuiltInFactory`, generic editor only. Tone stack, headroom and aliasing
   tests; bench.
3. **Reverb and tremolo**: spring tank and opto tremolo.
4. **Cabinet**: the parametric IR builder, background build and swap, two mics,
   cabinets, room; custom IR loading through `Insert::state`.
5. **Presets**.
6. **Design spec and face**: design-system pattern, plugins.md spec, tokens,
   renders, amp page, cab page, page switch, launch flag, captures at 100% and
   200%.
7. **Docs**: architecture.md (the amp and the convolver), decisions.md (zero
   latency via IIR oversampling and a split convolver; a built, not shipped, IR;
   no trade dress), user guide (tracking and reamping), CHANGELOG, roadmap
   (the "Amp and cabinet simulation" gap).

Follow-ups, each its own piece of work: low-latency monitoring in the engine,
the tuner, then the pedals.

## Open questions

1. The name (Corona is a placeholder).
2. One channel (Vibrato) or both channels with a channel switch? The real amp's
   jumpering trick (both inputs linked) is a nice touch but adds a second
   preamp's CPU.
3. Blackface (1960s, no master) or silverface (1970s, master with pull boost)
   look and feature set? The plan assumes silverface features with our own look,
   because Master + Boost makes the amp usable for drive.
4. Should the bass cabinets ship now, or wait for a dedicated bass amp?
