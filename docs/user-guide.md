# Using Northlight

A short tour of Northlight. The full manual will live on
[northlightaudio.org](https://northlightaudio.org).

## Audio setup

The first launch asks for an audio device. **Audio Setup** (the app menu, or the
device name in the status bar) chooses the driver (ASIO, or one of the Windows Audio
modes), the device, sample rate, buffer size and channels, and the MIDI inputs and
output.

To measure the real round-trip latency, connect an output to an input with a cable,
choose that pair under **Loopback** and click **Calibrate**. The result is stored for
that device, rate and buffer size, and recordings are placed with it.

## Projects

Projects are `.northlight` files: **New**, **Open**, **Save** and **Save As** in the
project menu (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S), with **Close project** and
**Revert to saved**. Anything that would replace or close a project with unsaved
changes asks first: Save, Don't save or Cancel. Unsaved changes are autosaved every
two minutes, and if Northlight closes without saving, the next launch offers to
restore them. Each save keeps the version it replaces beside the project as
`Song.northlight-bak`, which opens like any project. Open a project from Explorer
with **Open with**, or drop it on the window; if Northlight is already running, the
file opens there. **Recent** lists the last projects (a missing one shows "Not
found") and can be cleared, and Preferences can reopen the last project when
Northlight starts.

Imported audio is copied into the project's media folder (Preferences, General,
**Copy media**), so the project folder holds everything it plays. A project that has
not been saved yet copies its media, and its recordings, in when you first save it;
**Save As** to another folder asks whether to bring the media along. If a project
opens with files missing, a message says so; click **Locate…** there or on a missing
clip, find the file, and Northlight relinks it and offers to relink the others it
finds in the same folder. **Save as template** keeps the tracks, routing and
processing without the clips; **New from template** starts from one. **Gather
media** copies media used from elsewhere into the project's media folder; **Remove
unused media** moves files no clip uses to the Recycle Bin.

## Playing and editing

Import audio from the project menu (Ctrl+I), or drop files onto the arrange view.
Space plays and stops, Shift+Space pauses (Space or Play then carries on from
there), Enter or Home goes to the start and End to the end, and a click in the ruler
locates. When playback stops, the playhead goes back to where it started; turn
**Return on stop** off in Preferences > Recording to leave it where it stopped.
Ctrl+wheel zooms around the pointer, the wheel scrolls tracks and Shift+wheel
scrolls time.

Click a clip to select it (Shift adds, Ctrl toggles), drag on empty space to lasso,
drag a clip to move it (Ctrl+drag copies it), drag an edge to trim, drag a top
corner for a fade, and Ctrl+Shift+drag up or down for clip gain. Automation under a
clip moves with it (turn that off in the ruler's menu). Moves snap to the grid
(**N** turns snap on and off); the snap menu has triplet and dotted grids too. **Delete** removes (with
ripple on, **Shift+N**, later clips close the gap), **Ctrl+D** duplicates, **S** splits at
the playhead and **Alt+S** at the time selection, **J** joins, **X** crossfades overlapping clips, **Ctrl+Shift+N**
normalises, **Ctrl+M** mutes or unmutes the selected clips. **M** drops a marker,
**Alt+M** a region from the time selection (or the loop range); a new marker's
name is ready to type. Comma and full stop jump between markers. Right-click a clip
for the same edits in a menu, with Split here, Rename (**F2**) and Colour.

Click a track header to select it, Ctrl+click to add or remove one, Shift+click for
a run of tracks, and **Up**/**Down** to move (Shift extends). Arm, mute, solo,
colour, delete, duplicate, freeze, folder moves and drags apply to every selected
track. Drag a track by its icon to reorder it: dropped inside a folder's tracks it
joins the folder (how far right the pointer is picks the depth), and a folder takes
its tracks with it. Buses reorder the same way. Files dropped on an audio track land
on it at the snapped position; dropped below the last track, each makes a track.

The range tool selects a stretch of time across the tracks you drag over (over clips
too): **Delete** removes it (closing the gap with ripple on), **Ctrl+X**/**Ctrl+C**
cut and copy its parts, **Ctrl+V** pastes them at the playhead, and its right-click
menu also splits at it, loops it and makes a region of it. In an automation lane,
click a point to select it (Shift adds, Ctrl toggles, Shift-drag draws a marquee),
drag to move the selection, and use Delete, cut, copy and paste on it; the range
tool selects a stretch of a lane, the pencil draws freehand and the eraser sweeps
points away.

The tools sit at the top left of the track list, keys **1** to **7**: select, range
(drag a time range), pencil (draw MIDI clips), **erase** (click or sweep over clips
to delete them), **split** (click a clip to cut it at the line; Shift+click cuts
every track, Alt cuts off the grid), **glue** (click a clip to join it with the
next: audio that a split cut apart, or any later MIDI clip) and **mute** (click or
sweep to mute or unmute clips; a muted clip is hatched and stays where it is).

The tempo track holds tempo and time signature changes, including ramps. Audio clips
can be stretched and transposed, or stretched to the project tempo. The audio editor
corrects and shapes the pitch of a vocal and handles its breaths, on a stretched or
transposed clip too (the stretch plays the corrected voice). In its pitch mode
Left and Right step through the notes, a double-click splits a note, the Correct
sheet shows the key it aims at (the vocal's detected key when the project has none)
for you to confirm or change, and the Inspector shows the selected notes' pitch,
drift, vibrato, slides and gain to type in.

## Recording

Pick an input in a track's header, arm it with its **R** button (or **Shift+R** for
the selected track), choose input monitoring with **I** (off, automatic or on), and
press **Record** or the R key. Arming, monitoring and the input are not undo steps:
Ctrl+Z after arming undoes the edit before it, as in other DAWs. The project still
counts them as changes and saves them. **Click** turns on the
metronome and **Count-in** steps through Off, 1, 2 and 4 bars of click before a take. Drag in the ruler
to set a range: **Loop** repeats it and **Punch** records into it. With loop on, each
pass becomes a take; the take lanes show them, and swiping across a lane picks which
take plays where. MIDI tracks record takes the same way; the **Recording** card in a
MIDI track's Inspector switches it to **Merge** (overdub into the clip under the
pass) or **Replace** (replace what that clip had where you played).

Every enabled input can be picked on its own. For a stereo source (a keyboard, a
pair of overheads), add a **stereo input** under **Stereo inputs** in Audio Setup:
name it and choose its left and right inputs. Tracks then list it by name, record it
to one stereo file and monitor it in stereo. Stereo inputs are kept for each audio
interface, so every project can use them.

Recordings go to the project's media folder, as 32-bit float or 24-bit WAV
(Preferences, Recording), named by the take pattern ("Vox 3" from `{track} {take}`).
**Pre-roll** plays a few bars before the playhead without recording them. If
Northlight closes during a take, the next launch offers to recover the unfinished
files and put them back where they were recorded. If the audio interface is
unplugged mid-take, the recording stops and is kept up to that moment, and playback
stays stopped when Northlight switches to another device.

## Mixing

The **Mixer** (F2) has a strip for every track, bus and FX track: inserts, sends,
pan, mute, solo, phase, fader and meters, with the master's loudness meter. Strips
can join a fader group, so their faders, mutes and solos move together. Ctrl+click
a solo button to hear that channel alone; **Solo safe** (a strip's right-click
menu) keeps a channel heard whatever else is soloed. A strip's output can be the
master, a bus or another pair of your audio device's outputs. Sends have a pan and
a mute (right-click a send). Right-click a strip to hide it or switch to **wide
strips**; the mixer's "hidden" button brings hidden strips back. A strip shows how
much its plugins delay it, and the master the project's total delay compensation.

Every channel has a **channel strip** of six modules (gate, EQ, compressor, de-esser,
saturation, limiter) that switch on in place. In the **Editor** panel each module
is a card with a live display. The compressor has two models, switched in its card's
header: **Clean**, a modern compressor with every control, and **Opto**, a vintage
optical compressor with just Peak reduction and Gain whose release slows down the
longer it has been working. Factory and user **presets** set up a whole strip at
once.

**FX tracks** hold shared effects such as a reverb or a delay that several channels
send to. **Inserts** take the built-in processors and any VST3 or CLAP plugin found by
a scan; a plugin's window has bypass, a wet/dry mix, presets (named, and undoable)
and pin. Drag an insert by its handle to another strip to move it, with Ctrl to
copy it; an insert's menu also copies, pastes and replaces it. A plugin that is
not installed, failed to load or crashed says so on its slot, with the way out:
Scan for plugins, Retry, or Reload plugin. Ctrl+Shift+W closes every plugin
window. Compressors can be sidechained from another channel. Plugin latency is compensated everywhere,
including in recordings. The master has inserts too, on the whole mix before its
fader.

The **Glue Compressor** is a console-style bus compressor for holding a mix or a
group together: stepped Ratio, Attack and Release (with Auto) switches, a key
high-pass so the bass does not pump it, and Mix for parallel compression. Its
display shows the music's loudness and peaks going in (grey) and coming out
(colour): a fast attack pulls the peaks toward the body, a slow one keeps the
punch.

**Patina** makes a sound old. Its six stages each add one kind of age: Heat
drives it into a tube, tape, transformer or broken fuzz; Chamber puts it in a
small room, a spring or a plate; Drift gives it a tape or record's wow and
flutter; Grain adds vinyl crackle, tape hiss, mains hum or room air (Follow
ducks the noise under the music or lets it ride with it); Wear dulls the
highs and adds dropouts and skew; Crush lowers the bits and sample rate like
an early sampler. Switch a stage on with the slide switch over its window and
click the window for its controls; the window shows what that stage is doing
right now, and the lamps under it how much of the stage is in play. The arrows
beside the amber display step through the presets; click the display for the
list.
Age turns every stage up or down together: half way plays them as set, lower
cleans the sound up, higher wears it out. Breath lets a stage's amount drift
on its own, at the Pace beside Age, so the sound never sits still.

The **Exciter** adds harmonics to sounds that are flat or dull, which is something
an EQ cannot do: it only raises what is already there.

- **Harmonics.** Set **Frequency** to where the harmonics should start; only the
  sound above it drives them. Pick a **Character**: Tube is warm, Tape has an
  edge, Air adds a little sheen at the top, Crisp is bright. **Drive** makes the
  series denser, and **Amount** sets how much is added.
- **Low end.** **Amount** adds harmonics of the bass, so small speakers suggest
  notes they cannot play.
- **Dynamics.** **Dynamics** lets the excitation follow the music: Push gives
  more on loud moments, Fill more in quiet ones and tails. **Transients** adds
  snap to onsets.
- **Output.** **Width** spreads what is added without changing the mono sum.
  **Auto gain** keeps the level the same when the Exciter is switched on and off,
  so you hear the difference rather than a louder version.

The display shows the input in grey and what the Exciter adds in colour; drag the
handles to move the two frequencies.

**Bloom** is a reverb for effects rather than rooms. Its display draws the tail as
a flower: each note opens it and it fades back, the highs first, with now at the
top and higher pitches further out. Turn Decay up to a minute or press **Freeze**,
the seed at the flower's heart, to hold the tail for good (it keeps moving; the input stays out
until you let go). **Shimmer** pitch-shifts the tail inside itself, so it climbs an
octave or a fifth (or falls) again and again; **Swell** makes the tail build up
slowly after each note, like a reversed reverb. **Gate** cuts the tail a set time
after the sound stops, for gated drums, and **Duck** lowers the tail while the dry
sound plays so a huge tail does not bury it. The display shows the tail itself:
its spectrum over the last five seconds above, its level against the dry sound
below.

**Vesper** is a reverb for vocals. It listens to the singer and shapes the reverb
around the words: **Clarity** keeps consonants and sibilance out of it,
**Breaths** keeps breaths out, **Duck** lowers the tail while the singer sings
and **Ends** lifts and lengthens it where a line ends. **Lookahead** lets it hear
a little ahead (Short) or half a second ahead (Long, which also throws a line's
last word and makes **Swell** possible: the tail rises into each line before it
is sung); it adds that much latency, which Northlight makes up for. The evening
star at the top right is **Catch**: press it to hold the tail as a pad. The
display, Evensong, shows the last six seconds with now at the right. The coloured
sky is the reverb's tail and the dark land in front of it is the voice, so the
sky shows between the words. Under the horizon, coloured bars mark where the
singer sang, small ticks hanging below them where Clarity kept a consonant or an
s out of the reverb (the longer the tick, the more it took out), and dots mark
breaths. A star on the horizon marks each line end. The dotted line through the
sky follows the sung note (Follow's cutoff). The three lights by the name show
what Vesper hears now: SINGING, KEPT OUT and LINE END.

**Tape Echo** is a tape loop machine. Time sets the motor speed (head 1's echo; the
other heads are 2, 3 and 4 times it), and Sync sets it to a note value instead. Turn
Time while something plays and the echoes bend in pitch as the motor settles. The
Heads selector chooses which playback heads sound, from single slaps to the gallop
of 1+3+4. Feedback sends the heads back to the tape. Each repeat passes through the
tape again, so Bass, Treble, Age and Drive shape it a little more each time. Watch
LOOP: past 1.0 (and "Self-oscillating" in the header) the echoes build into the
tape on their own. Wow and Flutter add the wobble, and Spring adds a short spring
reverb to the echoes. On an FX track, switch on Kill dry and set Mix to 100%.

**Corona** is a tube guitar amp. Insert it on a guitar or bass track and play
through it: it adds no latency, so with a small buffer (64 or 128 samples) it can
be monitored while you record. The knobs read 1 to 10 as an amp's do. It stays
clean to about half way and breaks up as you turn **Volume** past 6; **Boost**
breaks it up sooner, and turning **Master** down keeps the drive at a lower level.
**Bright** adds sparkle at low volumes. **Reverb** and the tremolo's **Speed** and
**Intensity** have footswitch buttons on the strip at the foot. Set **Input** so
the LEDs reach about the middle on your hardest playing: the amp responds to how
hard it is hit, as a real one does. **Gate** quietens hum between notes, and
**Quality** trades CPU for smoothness (2x is enough for tracking). Press
**Cabinet** to see the speakers: choose a cabinet (the bass ones are 1x15 and
4x10), drag each mic across the speaker (the centre is bright, the edge dark),
scroll to move it nearer or further, and blend two mics; turn **Speaker** to
**Line** to use your own cabinet simulation, or **Load IR** to load a response
file. What you record is the dry guitar, so you can change the amp, or swap it for
another, after recording.

**Stereo Widener.** Insert it on a stereo track or bus to change how wide it
sounds, band by band. Drag the Low, Mid and High blocks up to widen and down to
narrow (0 is mono); drag the lines between them to move the crossovers, and turn up
Mono below to keep the bass centred. To widen a mono sound, turn up Spread: Diffuse
spreads it without echo, Haas uses a short delay you set with Time; either way it
disappears cleanly when the mix is played in mono. The scope shows the image (a
vertical line is mono, a cloud is wide) and the meter under it the correlation: keep
it out of the red. Switch on Guard and the widener narrows itself whenever the image
would go out of phase; drag the coloured line on the meter to say where.

**Presets.** Every built-in effect has presets in its header, in the Editor panel
and in its window. The arrows step through them; click the name for the list. The
name says Custom once you change anything. To keep a setting, choose **Save
preset...** from the list and name it: it appears under Yours for that effect in
every project, and **Delete** removes it again. Undo takes back a whole preset at
once.

## Automation

The **A** button on a track header (or the **A** key) opens the track's automation
lanes under it, one lane per automated parameter; the number on the button counts
them. Right-click **A** to choose the mode (Off, Read, Touch, Latch, Write) or add a
lane; **+ Add lane** under the lanes does the same. In a lane, click to add points,
drag them, double-click to remove. Click a lane's name to change its parameter or to
clear, collapse, hide or delete it. Drag a lane's bottom edge to size it; the
arrow collapses it to a thin strip and the eye hides it. A hidden or collapsed lane
still plays, and **n hidden** under the lanes brings hidden ones back. In Touch,
Latch and Write, moving a control while playing records it, including moves made in
a plugin's own window, and adds a lane for it if it has none.

## Instruments and MIDI

A MIDI track plays an instrument channel, so several MIDI tracks can play one
instrument. Northlight comes with five instruments: **Aurora**, a polysynth; **Pulsar**,
a drum synth; **Nebula**, a pad synth; **Starbit**, a chiptune synth; and the
**Sampler**. VST3 and CLAP instruments work the same way. The **Instruments** panel
lists every instrument, who plays it and its outputs; an extra output of a
multi-output instrument becomes a track of its own in one click. A MIDI track can also
play an external instrument through a MIDI output.

The **piano roll** edits notes and velocities, with controller lanes for pitch bend,
the mod wheel and any control change; the lane menu stacks up to four lanes and a
lane's top edge drags its height. Its tools match the arrange view's (**1**, **3** to
**7**: select, pencil, erase, split, glue, mute; a muted note is hollow and silent).
**Ctrl+C**, **Ctrl+X** and **Ctrl+V** copy, cut and paste notes (at the playhead, or
where the pointer was), **Ctrl+D** duplicates, and right-click opens a menu of note
edits: quantise, humanise, velocity (set, scale, compress, randomise), length and
legato, transpose, mute, split, join and select all of a pitch. The grid menu has
triplet and dotted values, and the Quantise menu swing and humanise. Dragging a
velocity bar moves the selected notes together, keeping their dynamics (Alt sets them
all). **Ctrl+Left/Right** step through the notes, **Alt+Left/Right** shorten and
lengthen them, **Z** zooms to the notes and **Ctrl+Shift+wheel** zooms the rows. The
selected notes show in the Inspector to type in. A drum kit names its rows (Kick,
Snare...), and the filter button hides the rest; the ghost button shows the other
MIDI tracks' notes; the key button at the right sets the project's key. Standard MIDI
Files import and export.

**MIDI effects** change a MIDI track's notes before its instrument plays them (and
before its MIDI output). Select the MIDI track and use **+ MIDI effect** in the MIDI
FX section of the Editor panel; they run left to right, and their menu moves,
replaces, copies and removes them. **Arpeggiator** plays held notes one at a time, in
time with the song (with the transport stopped, at the project tempo from the first
key), up, down, as played, at random or as chords, over up to four octaves, with
gate, swing and latch. **Chord** turns each key into a chord, by shape or from the
key's scale, inverted, voiced and strummed. **Chord Pads** plays the chords of the
project's key on the white keys (C is I, D is ii...) and borrowed chords on the black
ones, each led smoothly from the last, with an optional bass note. **Note Repeat**
retriggers held notes in time, for rolls; **Note Echo** repeats each note, fading
and stepping in pitch. **Scale** keeps notes in a key, or maps the white keys onto
it; **Transpose** moves notes by steps of the key, semitones or octaves;
**Velocity** reshapes how hard notes play; **Note Length** sets fixed lengths,
legato, or holds notes with the sustain pedal; **Humanise** loosens timing and
velocity and can drop notes by chance (the same seed plays the same way). **Key
Range** passes only the notes in a range (or folds them in), and **MIDI Filter**
blocks kinds of events and moves one controller to another. Effects that follow the
key use the project's key unless set to their own. Recording keeps what you played,
not what the effects made.

## Export

**Export** (Ctrl+E) renders the mix or every track as stems, over the whole song or a
range, to WAV, AIFF, FLAC or Ogg Vorbis, at the project rate or another, with loudness
normalisation and dither. **Freeze** renders a track in place to save processing.

## Keyboard shortcuts

**Preferences > Shortcuts** lists every key, grouped as Transport, View, Tracks,
Editing, Files, App, Piano Roll and Audio Editor, and searchable by action or key.
Click an action's keys and press the new one to rebind it; if another action already
uses that key, the page says which and offers **Replace** (the other action loses the
key) or **Cancel**. A rebound action has a reset button, and **Reset all** puts every
key back. Changes apply at once, in tooltips and menus too. The tools' number keys
and the arrow keys work as sets and cannot be rebound.

The defaults most used: Space play/stop, Shift+Space pause, R record, Enter go to
start, L loop, P punch, C click, N snap, Shift+N ripple, Z zoom to fit, Ctrl+Z undo,
Ctrl+Y redo, Shift+M / Shift+S / Shift+R mute, solo and arm the selected track, F2 to
F9 the panels (Mixer, Editor, Browser, Inspector, Piano Roll, Diagnostics,
Instruments, Audio Editor; Shift with the key detaches the panel), and F2 renames the
selected track while the track list has the focus. The piano roll's keys (G snap, Q
quantise, [ and ] the grid, = and - transpose) work while it has the focus.
