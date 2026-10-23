# Changelog

What changes in each release of Northlight, written for the people who use it. The
format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
follow the scheme in [CONTRIBUTING.md](CONTRIBUTING.md#versions-and-releases).

Add a line under **Unreleased** with every change a user would notice. When a
release is made, the Unreleased section becomes that release.

## Unreleased

Northlight has not had a release yet. The first release, 0.1.0, will include:

### Added

- Pulsar drums modelled on the TR-808: a booming kick that drops gently onto its
  note, a snare with a tuned shell under its wires, a clap of retriggered bursts and
  a tail, metallic hats from six square oscillators, and a clipped rim shot. Harder
  hits drive the kick, toms and snare harder, like the 808's accent, and every pad
  sounds the same at any sample rate.
- An evenly spaced transport readout: bars, time, tempo and signature sit the same
  distance apart on one baseline, with the signature's chevron right after it.
- Waveforms in the arrange view, take lanes and audio editor show every sample
  when zoomed in, instead of widening the overview.
- Preferences in cards like Audio Setup's: an icon page list, controls sized to
  what they hold, accent presets from the track palette, scan folders as a list
  with Add folder and Remove, a grouped shortcut table with key caps, and a
  Coming soon badge on settings that are kept but not used yet.
- Stereo inputs, set up in Audio Setup from any two inputs of an interface and
  chosen by name in a track's input menu; a stereo track records one stereo file and
  monitors in stereo.
- Recording from any number of inputs with input monitoring, latency calibration
  through a loopback cable, loop and punch recording into take lanes with swipe
  comping, and recovery of recordings interrupted by a crash.
- A mixer with buses, FX tracks, sends, fader groups, sidechains and delay
  compensation, and a six-module channel strip on every channel with factory and user
  presets.
- Inserts on the master, on the whole mix before the master fader.
- The Glue Compressor, a console-style bus compressor with stepped Ratio, Attack and
  Release (with Auto), a key high-pass and parallel Mix, whose display shows the
  mix's loudness and peaks going in and coming out and how far the peaks stand
  above the body (the crest factor).
- An opto compressor, modelled on vintage optical compressors, that can be swapped in
  for the channel strip's compressor or added as an insert.
- VST3 and CLAP hosting, with plugin scanning in a separate process, a plugin browser,
  presets and automation of plugin parameters.
- Automation in Read, Touch, Latch and Write modes.
- MIDI and instruments: a piano roll with controller lanes, Standard MIDI File import
  and export, external MIDI output, and five bundled instruments (Aurora, Pulsar,
  Nebula, Starbit and the Sampler).
- A tempo track, clip time-stretch and transposition, and vocal pitch editing.
- Clip editing, markers and regions, folder tracks and track freezing.
- Export to WAV, AIFF, FLAC and Ogg Vorbis with loudness normalisation and dither.
- Autosave, templates, media management and local crash reports.
- Progress while a project opens: a sheet shows what is loading (the project, each
  audio file, each plugin by name) with a progress bar, and the window keeps drawing.
  Opening can be cancelled until the plugins start loading.
- The Machair dusk design, with a splash screen and About box that show the version.
- Steadier playback while the computer is busy (compiling, for example): the render
  threads and the disk thread now run at higher priority than ordinary programs.
- The dropout counter also counts gaps where the disk could not keep up with a
  streamed clip, and its warning says when the disk is the cause. Diagnostics shows
  these disk gaps on their own.
- Diagnostics shows late callbacks: times the audio driver called Northlight late,
  which is heard as a click even when processing kept up. These usually come from
  another driver or power saving holding up the computer.
- Diagnostics says "not reported" for driver dropouts when the audio driver cannot
  count them (most ASIO drivers), instead of showing 0.
- An output tap for tracking down clicks: start Northlight with `--output-tap
  <folder>` and it records exactly what it sends to the audio driver, with a list of
  any clicks found in it and of late callbacks, so you can tell whether a click comes
  from Northlight or from the driver and system.
- ASIO drivers work in every build of Northlight, including ones built from source
  without Steinberg's ASIO SDK.

### Fixed

- Automated faders in the mixer hold their automated value steadily while you move
  another fader, instead of flickering back to their static level.
