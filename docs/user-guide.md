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
