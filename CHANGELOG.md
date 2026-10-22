# Changelog

What changes in each release of Northlight, written for the people who use it. The
format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
follow the scheme in [CONTRIBUTING.md](CONTRIBUTING.md#versions-and-releases).

Add a line under **Unreleased** with every change a user would notice. When a
release is made, the Unreleased section becomes that release.

## Unreleased

Northlight has not had a release yet. The first release, 0.1.0, will include:

### Added

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
