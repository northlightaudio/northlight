# Roadmap

Where Northlight is going, what it will not try to be, and what stands between it and
a first release. Day-to-day work is tracked in GitHub issues; this page keeps the
direction.

## Goals

| Goal | What it means in practice |
|---|---|
| First-class ASIO | Native ASIO with the buffer size, sample rate and channels the user chooses; Windows Audio (WASAPI) as the fallback, so it runs on any machine. |
| Plugin hosting | VST3 and CLAP instruments and effects: scanning that cannot crash the application, insert chains, editor windows, state saved with the project, delay compensation. |
| Performance | Glitch-free playback and recording at 64-sample buffers on a mid-range machine, rendering on every core, a UI that never stalls the audio, and a project open in under a second. |
| Design | A real design system (tokens, type, spacing, motion), 60 fps meters and waveforms, high-DPI throughout. See [design/](design/README.md). |
| Multitrack | Any number of audio, MIDI and instrument tracks, buses, FX tracks, sends, groups and insert chains. |
| Recording | Any number of inputs at once, with input monitoring, exact latency compensation, loop and punch recording into takes, and writing that survives a crash. |

## Not goals for the first release

- macOS and Linux. The platform-specific parts sit behind a seam so a port stays
  possible, but the first release is Windows only.
- Video, notation, cloud collaboration.
- VST2, AAX and AU hosting.

## Where it stands

Northlight is in active development and has not had a release. The core of a working
DAW is in place:

- **Audio:** ASIO and Windows Audio devices, with loopback latency calibration.
- **Recording:** multitrack recording from mono and stereo inputs with crash
  recovery, and loop and punch recording into take lanes with comping.
- **Mixing:** a mixer with buses, FX tracks, sends, fader groups and delay
  compensation, plus a six-module channel strip on every channel.
- **Plugins:** VST3 and CLAP hosting with out-of-process scanning, sidechains and
  automation.
- **MIDI and instruments:** a piano roll with controller lanes, MIDI file import and
  export, external MIDI output, and five bundled instruments.
- **Time and pitch:** a tempo track, clip time-stretch, and vocal pitch editing.
- **Editing and output:** clip editing and markers, offline export with loudness
  normalisation and dither, track freezing and folders.
- **Projects:** autosave and templates.

## Before a first release

- **Hardening.** Long soak tests (a day of playback, scans of very large plugin
  collections, projects with hundreds of tracks), a matrix of ASIO drivers (Focusrite,
  RME, Universal Audio, Steinberg, ASIO4ALL, FlexASIO), and a performance regression
  suite in CI.
- **A minimum hardware specification**, to set the performance budget against.
- **An installer** that ships the licence and third-party notices with the
  application.
- **Vocal pitch editing checked by ear** on real vocal recordings; so far it is
  verified with synthetic voices.
- **Design polish.** The October 2026 review left smaller alignment and grouping
  issues in the track headers, mixer strips, Editor panel cards, piano roll toolbar
  and browser footer. These are to become issues of their own.
- **Known gaps:**
  - Moving a folder in the track order doesn't yet take its tracks with it.
  - An instrument whose extra outputs feed aux tracks can't be frozen.
  - Buses and the master aren't shown as arrange rows.
  - The master has no inserts.

## Feature gaps

A review in October 2026 compared Northlight with what people expect from a DAW they
record and mix in every day. The engine and the recording path hold up; the gaps are
in editing, effects and controller work. Each item is to become an issue of its own.

### In order

1. **The master and buses in full.** Master inserts, and buses and the master as
   arrange rows with their own automation (both in the known gaps above).
2. **Clip mute and fade curves.** The arrange spec draws muted clips, but clips have
   no mute. Fades and crossfades are linear only and dip in level on music; add
   equal-power, logarithmic and S-curve shapes.
3. **A modulation effect and a stereo utility** (see the effects below).
4. **MIDI learn.** Map controller knobs, faders and buttons to any parameter and the
   transport, then Mackie Control and HUI for control surfaces.
5. **Richer automation.** Lanes for send levels and mute, curve shapes between
   points, and range edits on a lane (copy, scale, thin).

### Editing and workflow

| Gap | What it means |
|---|---|
| Undo history | A panel listing the undo steps, to jump back several at once |
| Keyboard shortcuts | An editor for the key map, which is fixed in code today |
| Clip operations | Reverse, rename, bounce in place and glue (render several clips into one) |
| Recording | Pre-roll and post-roll, MIDI overdub and merge, retrospective record, and cue mixes for performers' headphones |
| Track presets | Save a whole track (instrument, inserts, sends) and add it to another project; strip presets cover the channel strip only |
| Markers | A marker and region list to see, rename and reorder them |
| Overview strip | The arrange's mini-map, deferred in the spec, for long projects |
| Folders | Moving a folder should take its tracks (in the known gaps above) |

### Effects

Northlight ships gain, EQ, compressor, opto compressor, limiter, reverb and delay,
and the channel strip adds a gate, de-esser and saturation. Missing:

| Area | Effects |
|---|---|
| Modulation | Chorus, flanger, phaser, tremolo, auto-pan. None exist yet; this is the largest hole |
| Utility | Stereo width and mid/side, mono, channel swap, tuner |
| Metering | Spectrum analyser, phase correlation meter and vectorscope as inserts |
| Dynamics | Multiband compressor, transient shaper, clipper, bus compressor |
| Filter | Resonant filter driven by an LFO or an envelope follower |
| Pitch | Real-time pitch shifter and harmoniser |
| Reverb | Convolution reverb that loads impulse responses |
| Colour | Amp and cabinet simulation, bitcrusher, tape echo |

The EQ insert has a high-pass and three bands. A fully parametric EQ (six to eight
bands with shelves and notches, over a spectrum) would let it serve as the main EQ.

### MIDI, time and output

| Gap | What it means |
|---|---|
| MIDI editing | Quantise exists; add humanise, swing and groove quantise, legato, velocity scale and randomise, and chord and scale tools |
| Note entry | Step input from a MIDI keyboard |
| Audio to MIDI | Convert a monophonic audio clip, or drum hits, to notes |
| Warping | Clip stretch is whole-clip; add warp markers inside a clip, transient detection and slicing a loop into the Sampler |
| MP3 export | MP3 files import but cannot be exported |
| VCA faders | Fader groups exist; add VCA faders that control a group's levels without summing audio |
| Freeze | Instruments whose extra outputs feed aux tracks (in the known gaps above) |

## Open questions

- **Plugin sandboxing.** Hosted plugins run in-process, with their crashes contained
  where possible. Whether to run them out of process is decided after seeing crash
  reports from real use.
- **Other platforms.** When to start a macOS port.

## Risks

| Risk | Mitigation |
|---|---|
| ASIO driver quirks (sample-rate switching, control-panel reentrancy, channel limits) | Defensive device reopening and retry; testing across a driver matrix, with ASIO4ALL and FlexASIO as the worst cases |
| Third-party plugins crash or misbehave | Out-of-process scanning and a blocklist; crash containment around processing calls; sandboxing as the long-term answer |
| SDK licensing | The ASIO and VST3 SDKs are never committed; CLAP (MIT) is supported as the open alternative |
| A single developer | The documentation in this folder is kept current enough for someone else to pick up any part |
