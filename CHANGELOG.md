# Changelog

What changes in each release of Northlight, written for the people who use it. The
format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
follow the scheme in [CONTRIBUTING.md](CONTRIBUTING.md#versions-and-releases).

Add a line under **Unreleased** with every change a user would notice. When a
release is made, the Unreleased section becomes that release.

## Unreleased

Northlight has not had a release yet. The first release, 0.1.0, will include:

### Added

- **Lumen**, a modelled electric piano, joins the bundled instruments. Each note
  is a hammer striking a tine in front of a magnetic pickup, so a harder strike
  is brighter as well as louder, with the bell of the tine in the attack and the
  bark of the pickup when it is played hard. Voice it as a technician would
  (Bark, Symmetry, Hardness, Bell), age it, and play it through its stereo
  tremolo, phaser and chorus; the mod wheel rides the tremolo. Seven presets.
  `--render-phrase` renders a fixed test phrase through any built-in instrument
  to a WAV.

- Northlight comes as an installer and as a portable zip. The installer installs
  for you alone without administrator rights (or for everyone), adds a Start menu
  shortcut and opens `.northlight` projects with a double-click. Neither needs the
  Visual C++ Redistributable.

- Screen readers: clips, take lanes, markers, tempo changes, automation points,
  piano roll notes and the mixer's insert slots, sends and channel strip modules
  are now read out, with their position and state; pressing one selects it (or
  switches the module) and its menu is the right-click's. Nothing changes on
  screen, and none of it is made unless a screen reader is running.

- Keyboard: Tab now moves through the main window in order (header bar, the
  selected track's header, the arrange view, the dock, the side panel, the status
  bar) and wraps; Up and Down move between tracks with the focus. Ctrl+Left and
  Ctrl+Right select clips on the selected track (Shift extends). Shift+F10 or the
  Menu key opens the menu of what has the focus. In the piano roll, Insert adds a
  note after the selected one and Alt+Up / Alt+Down make notes louder or softer.

- Shortcuts can be changed: on Preferences > Shortcuts click an action's keys and
  press the new one. A key another action uses names that action and offers
  Replace or Cancel; a changed key has a reset button, and Reset all puts every key
  back. Changes apply at once, in tooltips and menus too. The page now also lists
  the piano roll's and the audio editor's keys.

- The App and Project menus show each item's shortcut.

- Pause: Shift+Space, or Play while playing, stops and stays there; the next Play
  carries on from that point. Stop (and Space) now takes the playhead back to where
  playback started; turn Return on stop off in Preferences > Recording to keep the
  old behaviour.

- The numeric keypad's 0, 3 and 1 play, record and go to the start.

- Accent and contrast changes in Preferences show at once, without a restart.

- Plugins in trouble say so: a mixer slot or Editor card whose plugin is not
  installed, failed to load or crashed shows it (with the reason on hover) and
  offers the way out: Scan for plugins, Retry or Blocklist, or Reload plugin,
  which restarts a crashed plugin from its last saved state without restarting
  Northlight. Opening a project with plugins missing or failed tells you once,
  naming them; a missing plugin keeps its name.

- Inserts move between mixer strips by their drag handle (Ctrl at the drop
  copies), a plugin dragged from the browser drops onto a strip, and every
  insert's menu has Copy, Copy chain, Paste after and Replace (effects too). The
  Editor panel's card menu is now the same menu as the mixer's, with presets,
  sidechain and the latency, and the card's key chip opens the key menu.

- A hosted plugin's window names its current preset, a preset load is one undo
  step, its presets are in its card's menu too, a pin keeps that plugin's windows
  on top, and a toggle shows the generic editor for plugins that have both.
  "Close all plugin windows" (Ctrl+Shift+W) is in the app menu, and windows open
  when a project was saved reopen with it.

- Mixing: solo safe (a strip's right-click menu) and exclusive solo (Ctrl+click
  S); sends have a pan and a mute; a track or bus can play straight to another
  pair of the audio device's outputs; "+ Send" offers New FX track right there;
  strips can be hidden and shown again; wide strips add a dB scale and a send pan
  column; strips show their plugins' delay and the master the project's total
  delay compensation; a short strip's "3 inserts" and "2 sends" open a list.

- The plugin browser counts the built-ins, describes each one in a line (also in
  the effects menu), filters by kind of effect, says when nothing matches, keeps
  16 recent plugins, and a slot's format badge opens it on that plugin.

- The arrange view selects several tracks (Ctrl+click, Shift+click, Up/Down with
  Shift): arm, mute, solo, colour, delete, duplicate, freeze, folder moves and drags
  act on all of them. Tracks can be duplicated with their clips, inserts, sends and
  automation; new tracks go after the selected one, in its folder. Dragging a track
  into or out of a folder's tracks changes its folder, a folder moves with its
  tracks, and buses can be reordered.
- Ctrl+drag copies clips (clip gain moved to Ctrl+Shift+drag), and automation moves
  with clips moved along their track. Clips can be renamed (F2 or the clip menu) and
  given a colour of their own.
- The range tool works over clips and across tracks; a time range can be deleted
  (with ripple), cut, copied, pasted, looped or made a region.
- Automation lanes: select points (click, Shift, Ctrl, marquee), move, delete, cut,
  copy and paste them, delete a stretch of time, draw freehand with the pencil and
  sweep points away with the eraser.
- Triplet and dotted snap grids, in submenus of the snap menu and in Preferences.
- A new marker is named as it is added, and Alt+M makes a region from the time
  selection.

- Northlight asks before anything replaces or closes a project with unsaved changes:
  New (Ctrl+N), Open, a recent file, New from template, restoring an autosave, Quit
  in the app menu and Windows shutting down, as the close button already did. The
  autosave is only deleted once you save or choose Don't save, and a second
  Northlight window no longer offers (or deletes) the first one's autosave.
- Close project and Revert to saved in the Project menu; Recent shows projects that
  are no longer there as "Not found" and has Clear recent; Preferences can reopen
  the last project at startup.
- Each save keeps the previous version beside the project as a `.northlight-bak`
  file (Preferences, General, Keep backup).
- Projects open from Explorer's Open with and by dropping them on the window; a
  second launch with a file opens it in the window already running.
- Imported audio is copied into the project folder (Preferences, General, Copy
  media), and an unsaved project's media and recordings come into its folder when it
  is first saved; Save As offers to bring the media along. Remove unused media works
  for an unsaved project's own recordings, and those left in the shared Recordings
  folder go to the Recycle Bin once nothing can use them.
- Missing media: a message when a project opens with files missing, and Locate… on
  it and on each missing clip relinks the file (and the others in that folder), as
  one undo step.
- Recording: WAV 24-bit, take names from a pattern ("{track} {take}") and pre-roll
  in Preferences now work.
- When the audio device is unplugged mid-take, the message says how much was saved
  ("the recording was saved up to 3:12"); when Northlight switches to another device
  it stays stopped and says which one plays.

- Pitch edits now play on stretched and transposed clips (and "Follow tempo
  changes" clips), in playback, export and freeze: the stretch plays the corrected
  voice. The audio editor no longer refuses to edit such a clip, and shows a
  transposed clip's notes at the pitch they play.

- MIDI loop recording makes takes, comped in the take lanes as audio is. A MIDI
  track's Inspector has a Recording card: Takes, Merge (overdub into the clip under
  the pass) or Replace. Notes coming in draw in the record colour in the piano roll.

- Piano roll: cut, copy, paste and duplicate notes; a right-click menu of note edits
  (quantise, humanise, velocity set, scale, compress and randomise, length and
  legato, transpose, mute, split, join, select all of a pitch); the arrange view's
  split, glue and mute tools (5, 6, 7) and muted notes; triplet and dotted grids,
  Bar and Adaptive; swing and humanise in the Quantise menu; up to four stacked,
  resizable lanes; vertical zoom (Ctrl+Shift+wheel) and Z to fit the notes; follow
  the playhead; the other tracks' notes as ghosts; a key button that sets the
  project key; keys to step through notes (Ctrl+Left/Right), change their length
  (Alt+Left/Right), mute (Ctrl+M), split at the playhead (S) and join (J).

- Dragging a velocity bar moves the selected notes together, keeping their
  dynamics (Alt sets them all to one value), and new notes take the last velocity
  edited, shown and set in the piano roll's toolbar.

- A drum kit's piano roll shows its pad names (Kick, Snare, Hats...) on the keys,
  and a button hides the rows the kit does not play.

- The Inspector shows the notes selected in the piano roll (pitch, start, length,
  velocity, mute) and the audio editor (pitch, drift, vibrato, slides in and out,
  gain, mute), each typeable.

- Audio editor: the Correct sheet shows the key it aims at (the vocal's detected
  key when the project has none) to confirm or change, and a Keep vibrato switch;
  Left and Right step through the notes; a fade handle's vertical drag shapes its
  curve as in the arrange view; follow the playhead; Ctrl+Shift+wheel zooms the
  pitches; audition is on by default, as in the piano roll.

- A plugin's card in the Editor panel shows a faceplate: its kind, its full name,
  its maker, the format and its latency, with an Open editor button (or
  double-click it). A plugin that is not installed says so in the same place.

- Every shortcut comes from one table, so the Shortcuts page, tooltips and menus
  always show the keys that work: the page now lists F8 and F9 (Instruments, Audio
  Editor), cut, copy and paste; Redo shows Ctrl+Y (Ctrl+Shift+Z still works);
  Return is called Enter; and a region from the loop and a split at the selection
  are Alt+M and Alt+S everywhere.

- The Audio Setup window, the Preferences Audio page and the first-run sheet can
  be open together and every input and output meter shows the full signal; they
  used to take the peaks from one another. The first-run sheet offers the buffer
  sizes the device itself offers, and picks a Windows Audio device's default input.

- Buses, FX tracks and the master have one colour in every view: the mixer's
  uncoloured group buses are Overcast and its master Slate, as in the arrange
  view, and automation lanes always take their channel's colour.

- The Colour submenus in the track menu and the mixer show each colour's swatch
  and tick the current one, as the header stripe's popup does.

- A clip turned up draws a taller waveform in the arrange view, up to full
  scale, as it already did in the audio editor; takes in the take lanes show
  their fades.

- The piano roll and the audio editor draw the arrange view's grid and ruler:
  bars, beats and sub-beats by the same zoom thresholds, the bar numbers thinned
  and timed when zoomed out. The piano roll snaps and quantises to the beat of the
  bar each note is in, as the arrange view does after a time signature change,
  and the tempo track's changes snap to the arrange grid (G turns it off).

- The piano roll and the audio editor zoom smoothly with Ctrl+wheel around the
  pointer, as the arrange view does, instead of in fixed steps.

- The Limiter insert's header shows its gain reduction while it limits, as the
  channel strip's limiter does.
- Aurora, Nebula and Starbit's preset bars list your own presets too: save the
  sound as it is, delete one of yours or show the presets folder, as for the
  effects. A preset, on an effect or an instrument, is one undo step.
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

### Changed

- Better screen reader support (Narrator and others): the transport and tool
  buttons say whether they are on, the position counters read out, track header
  and mixer controls name their track ("Mute Bass" rather than "M"), the browsers
  read file and plugin names instead of row numbers, and Audio Setup's controls
  are named. Nothing looks different.
- Keyboard focus is easier to follow: the focus ring shows in every window once
  you press Tab or an arrow key, lists and the piano roll, audio editor and tempo
  lane show a ring when they have the focus, Tab stays inside a dialog, and closing
  a dialog or window returns the focus to where it was. Esc closes plugin and
  insert editor windows.
- High contrast goes further: disabled text and the small print on effect faces
  are brighter too, a clip light that has lit gets a bright outline, and a pitch
  note flagged by Correct gets a dashed outline. Contrast can now follow Windows: on
  System (the new default) a Windows contrast theme turns High on. Nothing changes
  unless you or Windows ask for it.
- Preferences > General has a Reduce motion setting: On makes panels, sheets and
  menus appear without sliding and stops animated glows. System follows Windows'
  Show animations, and now notices when it changes while Northlight runs.
- In the audio editor's pitch mode a double-click splits a note (on a join, merges
  the two), as the spec says; snapping to the semitone is in the note's menu. In
  wave mode a double-click resets the gain line or removes a fade instead of
  switching to pitch mode.

### Fixed

- Changing the accent colour while High contrast was on made the dimmest text as
  bright as the main text. High contrast now always raises text by one step.
- A narrow track header (down to its 160 px minimum) keeps the track's name and a
  whole input select: R, monitor and A move to the second row, next to the input,
  instead of pushing the name out.
- A tall track header's output menu is as wide as its name needs, so it reads
  "Master" rather than "..." in a narrow header; the sends count beside it shows only
  when it fits.

- Arming a track, changing its input monitoring or its input are no longer undo
  steps, so Ctrl+Z after arming undoes the edit before it. They still count as
  unsaved changes.

- The header's count-in steps through Off, 1, 2 and 4 bars, as Preferences offers;
  it stopped at 2.

- The Play button's tooltip says Pause while playing (it said Stop).

- The user guide's shortcuts match the app: the Mixer is F2 (Ctrl+M mutes clips),
  snap and ripple are N and Shift+N, and Shift+R arms the selected track. G and E no
  longer duplicate snap and ripple in the track list.

- A mixer strip reads top to bottom as its spec draws it: inserts, sends, pan,
  the M/S/R/Ø toggles, then the fader, gain and output.

- The send menu no longer sends you to the track list when there is nothing to
  send to: it offers a new FX track there.

- Audio files dropped on an audio track's lane land on that track at the snapped
  position shown; only a drop below the last lane makes new tracks.
- The mouse wheel scrolls the arrange view's tracks and Shift+wheel scrolls time, as
  the other views do.
- Ctrl+click on an unselected clip adds it to the selection (it was taken out again
  on release).

- The status bar no longer shows a dropout on a fresh launch with nothing played
  (the device starting up), and its round trip reads "20.0 ms round trip", with the
  buffer's one-way length in its tooltip, so it is not mistaken for Audio Setup's
  buffer length.

- Opening a plugin's window no longer switches the dock to the mixer: the Editor
  panel stays up.

- A project with one damaged note, automation point, controller event or comp
  segment opens, without that entry, instead of failing to open at all.
- Moving a clip to a track that has just been removed leaves it where it was; before,
  it could vanish from its own track.
- The undo history keeps the last 1000 steps (and at most about 256 MB of them), so
  a long session no longer grows its memory without end; a fader drag is one step
  that holds one change, not one per pixel.
- The master's row in the arrange view shows its meter; the arrange view's meters
  no longer depend on the mixer's own timer.

- Values read the same on every card: one way of writing Hz, kHz, seconds, percent and
  dB, with a true minus sign (some cards wrote "-3.2 dB", others "−3.2 dB";
  2.4 kHz read "2.40 kHz" on one card and "2.4 kHz" on the next).
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
