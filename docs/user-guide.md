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
project menu (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S). Unsaved changes are autosaved
every two minutes, and if Northlight closes without saving, the next launch offers to
restore them. **Save as template** keeps the tracks, routing and processing without
the clips; **New from template** starts from one. **Gather media** copies media used
from elsewhere into the project's media folder; **Remove unused media** moves files no
clip uses to the Recycle Bin.

## Playing and editing

Import audio from the project menu, or drop files onto the arrange view. Space plays
and stops, Return goes back to the start, and a click in the ruler locates.
Ctrl+wheel zooms around the pointer, the wheel scrolls tracks and Shift+wheel
scrolls time.

Click a clip to select it (Shift adds, Ctrl toggles), drag on empty space to lasso,
drag a clip to move it, drag an edge to trim, drag a top corner for a fade, and
Ctrl+drag for clip gain. Moves snap to the grid (**G**). **Delete** removes (with
ripple on, **E**, later clips close the gap), **Ctrl+D** duplicates, **S** splits at
the playhead, **J** joins, **X** crossfades overlapping clips, **Ctrl+Shift+N**
normalises. **M** drops a marker, **Shift+M** a region from the loop range, and
comma and full stop jump between markers.

The tempo track holds tempo and time signature changes, including ramps. Audio clips
can be stretched and transposed, or stretched to the project tempo. The audio editor
corrects and shapes the pitch of a vocal and handles its breaths.

## Recording

Pick an input in a track's header, arm it with **R**, choose input monitoring with
**I** (off, automatic or on), and press **Record** or Ctrl+R. **Click** turns on the
metronome and **Count-in** sets the bars of click before a take. Drag in the ruler
to set a range: **Loop** repeats it and **Punch** records into it. With loop on, each
pass becomes a take; the take lanes show them, and swiping across a lane picks which
take plays where.

Every enabled input can be picked on its own. For a stereo source (a keyboard, a
pair of overheads), add a **stereo input** under **Stereo inputs** in Audio Setup:
name it and choose its left and right inputs. Tracks then list it by name, record it
to one stereo file and monitor it in stereo. Stereo inputs are kept for each audio
interface, so every project can use them.

Recordings go to the project's media folder. If Northlight closes during a take, the
next launch offers to recover the unfinished files and put them back where they were
recorded.

## Mixing

The **Mixer** (Ctrl+M) has a strip for every track, bus and FX track: inserts, sends,
pan, mute, solo, phase, fader and meters, with the master's loudness meter. Strips
can join a fader group, so their faders, mutes and solos move together.

Every channel has a **channel strip** of six modules (gate, EQ, compressor, de-esser,
saturation, limiter) that switch on in place. In the **Editor** panel each module
is a card with a live display. The compressor has two models, switched in its card's
header: **Clean**, a modern compressor with every control, and **Opto**, a vintage
optical compressor with just Peak reduction and Gain whose release slows down the
longer it has been working. Factory and user **presets** set up a whole strip at
once.

**FX tracks** hold shared effects such as a reverb or a delay that several channels
send to. **Inserts** take the built-in processors and any VST3 or CLAP plugin found by
a scan; a plugin's window has bypass, a wet/dry mix, presets and pin. Compressors can
be sidechained from another channel. Plugin latency is compensated everywhere,
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

**Tape Echo** is a tape loop machine. Time sets the motor speed (head 1's echo; the
other heads are 2, 3 and 4 times it), and Sync sets it to a note value instead. Turn
Time while something plays and the echoes bend in pitch as the motor settles. The
Heads selector chooses which playback heads sound, from single slaps to the gallop
of 1+3+4. Feedback sends the heads back to the tape. Each repeat passes through the
tape again, so Bass, Treble, Age and Drive shape it a little more each time. Watch
LOOP: past 1.0 (and "Self-oscillating" in the header) the echoes build into the
tape on their own. Wow and Flutter add the wobble, and Spring adds a short spring
reverb to the echoes. On an FX track, switch on Kill dry and set Mix to 100%.

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

The **A** button on a track header shows its automation. Choose the parameter and the
mode (Off, Read, Touch, Latch, Write) at the top of the lane; click to add points,
drag them, double-click to remove. In Touch, Latch and Write, moving a control while
playing records it, including moves made in a plugin's own window.

## Instruments and MIDI

A MIDI track plays an instrument channel, so several MIDI tracks can play one
instrument. Northlight comes with five instruments: **Aurora**, a polysynth; **Pulsar**,
a drum synth; **Nebula**, a pad synth; **Starbit**, a chiptune synth; and the
**Sampler**. VST3 and CLAP instruments work the same way. The **Instruments** panel
lists every instrument, who plays it and its outputs; an extra output of a
multi-output instrument becomes a track of its own in one click. A MIDI track can also
play an external instrument through a MIDI output.

The **piano roll** edits notes and velocities, with controller lanes for pitch bend,
the mod wheel and any control change. Standard MIDI Files import and export.

To hear everything together, write and open the demo song, "Northern Drift":

```powershell
Northlight.exe --demo-song "$env:USERPROFILE\Music\Northern Drift.northlight"
```

## Export

**Export** (Ctrl+E) renders the mix or every track as stems, over the whole song or a
range, to WAV, AIFF, FLAC or Ogg Vorbis, at the project rate or another, with loudness
normalisation and dither. **Freeze** renders a track in place to save processing.
