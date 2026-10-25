# Changelog

What changes in each release of Northlight, written for the people who use it. The
format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
follow the scheme in [CONTRIBUTING.md](CONTRIBUTING.md#versions-and-releases).

Add a line under **Unreleased** with every change a user would notice. When a
release is made, the Unreleased section becomes that release.

## Unreleased

Northlight has not had a release yet. The first release, 0.1.0, will include:

### Added

- Every bus and the master have a row in the arrange view, so they can be
  automated like tracks: the master's volume and its effects' parameters, a bus's
  volume, pan and effects. The master fader in the mixer follows its lane and can
  write automation in Touch, Latch and Write.
- The window opens at the size and position it had when Northlight last closed,
  maximised if it was, or centred when that place is no longer on a screen.
- Fade curves: drag a fade's handle up or down while setting its length to make it
  fast (full level sooner) or slow; the clip shows the curve as it plays, and the
  waveform under a fade shrinks with it. Linear stays the default.
- Four more tools in the arrange view, keys 4 to 7 after select, range and pencil:
  erase (click or sweep over clips to delete them), split (click a clip to cut it
  where the line shows; Shift+click cuts every track there, Alt cuts off the grid),
  glue (click a clip to join it with the next, including MIDI clips across a gap)
  and mute. Clips can now be muted on their own, with the mute tool or Ctrl+M: a
  muted clip stays in place, hatched, and plays nothing. Right-clicking a clip
  offers the edits too (split here, join, crossfade, mute, duplicate, normalise,
  cut, copy, delete), for MIDI clips as well as audio ones.

- Vesper, a reverb for vocals, in the effects menu's Reverb & Delay group. It
  listens to the singing: consonants, "s" sounds and breaths are kept out of the
  reverb (Clarity, Breaths), the tail ducks while the singer sings and comes back in
  the gaps (Duck, Release in ms or a note value), and it lifts and lengthens at the
  end of each line (Ends, Gap). Choose a plate, a chamber or a hall; the pre-delay
  can follow how fast the singing is (Auto); Follow keeps rumble out of the tail
  under the sung note, Mud cuts a muddy build-up, and Centre clears the middle of
  the tail for the voice. Lookahead (off by default, so it is safe to track
  through) lets it listen ahead of the sound: Short has the reverb's send down
  before an "s" starts, and Long throws the last word of each line into the reverb
  and lets Swell build the tail into a line before it is sung. Catch holds the
  tail as a pad under what comes next. Its face is a dusk sky over the machair:
  in its display the voice stands as land in front of the reverb's afterglow, so
  you can see the reverb get out of the way of the words and bloom between them,
  with a star at each line end; three lights show what it hears, and the evening
  star in its corner is Catch.
- Orbit, a studio tape machine for glue and colour on a bus or the master, in the
  effects menu's Drive & Colour group. Its tape is modelled from the way magnetic
  tape really responds, so pushing INPUT compresses softly, adds mostly odd
  harmonics and rounds off the highs first, the louder the more. Choose the speed
  (7½, 15 or 30 ips, which moves the low-end bump and the top end), the EQ standard,
  the operating level, the bias and one of three tapes; add hiss and flutter if you
  want them; LINK keeps the loudness steady while you drive it. Its face is a
  reel-to-reel under a spotlight, whose reels turn while the song plays, whose
  counter shows the song's position, and whose VU meters can read the input, the
  tape or the output.
- Add an effect between two others in the Editor panel: point at the gap between two
  insert cards and click the round + that appears. The effect goes in right there,
  so there is no need to add it at the end and drag it back.
- Five guitar pedals in a new Guitar group of the effects menu, beside Corona, each a
  stompbox with a few knobs and a footswitch, voiced for guitar and bass: Gravity, a
  compressor and sustainer; Flare, a distortion with silicon, LED and open clipping;
  Comet, an analog-style delay whose repeats darken and smear and can run away into
  oscillation; Tide, a chorus that keeps the bass's low end solid; and Polaris, a
  chromatic tuner from a five-string bass's low B up, whose footswitch mutes the
  output so you can tune silently.
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
- Presets for every built-in effect, chosen from a bar in the effect's header in
  the Editor panel and in its window: a factory list for each (compressor, EQ,
  reverb, delay and the rest), and your own, saved from the same menu and kept in
  Documents/Northlight/Effect Presets.
- Patina, a vintage colour effect: Heat, Chamber, Drift, Grain, Wear and Crush
  stages age a sound in their own ways, each with its own Breath, under one Age
  control, with a live readout of every stage. Its editor is a rendered 1960s
  broadcast processor with every control in view: a module per stage with its
  lit button, window, type selector and knobs, and a master plate with a large
  Age dial. Twelve factory presets.
- An Exciter that adds harmonics to flat sounds: four characters (Tube, Tape, Air,
  Crisp) generated from the band above a crossover, anti-aliased with no latency,
  following the music, plus psychoacoustic bass, mono-safe width and auto gain.
  Its display shows the harmonics it adds over the input's spectrum.
- Bloom, an effect reverb for huge and strange tails: decays up to a minute,
  Freeze, Shimmer that climbs by an interval, Swell that builds the tail like a
  reversed reverb, a gate and ducking. Its display draws the tail as a blossom that
  opens with each note and fades, pitch outward and time round to now at the top,
  with Freeze as the seed at its heart.
- Tape Echo, a tape loop with four playback heads in eight combinations, a motor
  speed that glides and bends the pitch (or syncs to the tempo), wow, flutter and
  tape age, repeats that darken and saturate each pass and self-oscillate past
  100%, a spring reverb and Kill dry. Its card is a 1970s tape echo console: the
  tape transport behind a smoked lid, a neon on each head glowing with the echoes
  as they pass, rollers turning with the tape, a VU meter for the record level, a
  loop gain lamp that turns red when the loop sings, and knobs and levers on cream
  and sage enamel. It scales to fit the Editor dock.
- Corona, a tube guitar amp modelled on a 1960s-70s American 2x12 combo with reverb
  and tremolo, for guitar and bass: Bright, Volume, Treble, Middle, Bass, a spring
  reverb, an opto tremolo, Master with a pull Boost, supply sag, a noise gate and
  an input trim with a meter. It stays clean to about half way and breaks up as it
  is turned up, adds no latency so it can be played through while recording, and
  runs at 2x, 4x or 8x oversampling. A second page shows the cabinet with the
  grille off: five cabinets (or your own impulse response) and two mics you drag
  across the speaker and move nearer or further, each a dynamic, ribbon or
  condenser with its angle, level, pan and polarity, and a room. Recordings keep
  the dry guitar, so the amp can be changed afterwards. Its card is an amp's front
  in oxblood and champagne, and it has 14 presets.
- `--capture` also saves any other open window, such as an insert's editor.
- Effects with a rendered face (Corona, the tape echo) switch to a compact form in
  the Editor dock: larger names without the small print, and a knob's value shown
  in place of its name while you hover or turn it. Their text is sharper.
- The tape echo's two-way levers flip on a click anywhere on them, and its
  console draws its knobs, lamps, neons and rollers without sparkle and its
  legends sharper.
- The Stereo Widener: width in three bands with phase-coherent crossovers, Mono
  below, a mono-safe Spread, rotation and balance, a correlation guard and level
  compensation, with a live vectorscope and the width the output measures.
- An opto compressor, modelled on vintage optical compressors, that can be swapped in
  for the channel strip's compressor or added as an insert.
- VST3 and CLAP hosting, with plugin scanning in a separate process, a plugin browser,
  presets and automation of plugin parameters.
- Automation in Read, Touch, Latch and Write modes, on lanes of their own under each
  track: one per automated parameter, all visible at once, each resizable, and
  collapsible or hideable without losing what it plays. The track's clips stay
  editable while its lanes are open, and a point at the very start of the song can
  be edited.
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

- The audio editor's pitch mode says when a clip is stretched or transposed, where
  pitch edits do not play, instead of taking edits that were never heard.
- With zoom linked, zooming or scrolling in the piano roll or the audio editor moves
  the arrange view too; before, the arrange view put it straight back.
- Undoing back to the last save leaves the project unmodified, so closing no longer
  asks to save; a change made in a plugin's own window (a preset, a knob) now marks
  the project modified.
- The inspector and the piano roll follow Select all, Esc, and clips that are
  deleted or moved to another track; before, they kept showing the old selection.
- Opening a project whose id counter is behind its pitch edits no longer lets new
  ids repeat a vocal note's or a breath's.
- Choosing an accent or a contrast in Preferences no longer rewrites, or deletes, a
  hand-edited `tokens.json`; those choices have their own file.
- Cancelling the Save dialog after choosing Save on close no longer makes the next
  save quit the app.
- The audio editor follows a stretched clip: its playhead, the clip's end, the fades
  and a zoom linked to the arrange view all sat in the wrong place before.
- Notes have one name everywhere: middle C is C3 in the Sampler and on the Polaris
  tuner too, as in the piano roll and the pitch editor (they said C4 before).
- Dragging a clip's fade handle in the arrange view sets the fade; before, the fade
  sprang back on release.
- Tooltips over the built-in effects' faces appear by the pointer at their usual
  size, instead of small and well above it.
- Selecting a MIDI clip shows it in the piano roll, and clicking a clip always
  updates the inspector; a plain click on a clip used to go unnoticed.
- The snap button's arrow opens the grid menu (Adaptive, Bar, Beat, 1/8, 1/16,
  1/32, Off); the magnet still turns snap on and off, keeping the grid.
- The count of selected clips no longer covers the arrange view's tools: it is at
  the right end of the status bar. The piano roll's tools and the audio editor's
  pitch tools are icon buttons like the arrange view's, with the same cursors.

- The plain parameter editor shows a setting with a few fixed choices as a row of
  buttons and one with many as a menu, instead of a knob reading a number; in the
  Editor panel a long list of parameters widens the card instead of running past
  its edge, and the knobs' values are no longer cut short.
- Text across the app is drawn at its intended size: every label, caption and
  readout was about a sixth smaller than designed. Layouts that were packed around
  the smaller text now leave room for it, and track headers (260 px) and mixer
  strips (92 px) are a little wider so names fit.
- The effects menu groups effects by kind (Dynamics, EQ & Filter, Reverb & Delay,
  Modulation, Drive & Colour, Stereo & Utility), each with an icon, built-in and
  scanned plugins together, with recent effects first and a By maker submenu last.
- An effect open in its own window is no longer drawn in the Editor panel too: its
  card shows that it is popped out, with a button that brings its window forward.
- The Editor panel can no longer be dragged so short that effect and instrument
  controls overlap: the dock stops at the height the tallest card needs, and
  scrolls when the window itself is shorter.
- Large knobs on the effect cards share one label line with the small knobs
  beside them, and their values sit clear of the ring.
- The tape echo, Corona and Patina no longer drop frames while the mouse moves
  over them, a knob is turned or they play: each repaints only what changed.
- Level readouts on the effect cards show whole numbers ("-23") instead of being
  cut off ("-23…").
- Automated faders in the mixer hold their automated value steadily while you move
  another fader, instead of flickering back to their static level.
