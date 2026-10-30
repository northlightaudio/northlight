---
title: "Glossary"
description: "The words the manual uses, explained."
sidebar:
  order: 4
---

The words this manual uses, in alphabetical order, with a short explanation of each.
Where a page covers the subject properly, the entry links to it.

## A

**ASIO**: A way for audio software to talk straight to an audio interface, bypassing
most of Windows' own audio system, for less delay. Most interfaces made for music come
with an ASIO driver. See [Setting up audio](../getting-started/audio-setup.md).

**Audio interface**: The box (or card) that turns microphone and instrument signals
into digital audio for the computer, and plays the computer's audio to speakers and
headphones.

**Audition**: To listen to something on its own before using it: a file in the
browser, a note you click in the piano roll or the audio editor.

**Automation**: Changes to a setting (a fader, a pan, a plugin's control) recorded or
drawn over time, so the mix moves by itself as the song plays. See
[Automation lanes](../automation/lanes.md).

**Automation modes**: How a lane behaves as the song plays: **Read** plays it back,
**Touch** records while you hold a control, **Latch** keeps recording after you let
go, and **Write** records over everything it passes. See
[Recording automation](../automation/modes.md).

## B

**Bar** and **beat**: A beat is the pulse you tap your foot to; a bar is a group of
beats (four, in most songs). Northlight measures positions in bars and beats as well
as in minutes and seconds.

**Bit depth**: How finely each measurement of a sound is stored. 24-bit is plenty for
recording; 32-bit float cannot clip inside the computer; 16-bit is used for CDs.

**Bounce**: To render tracks or a whole mix to a new audio file. See
[Exporting](../exporting/index.md).

**Breath**: In pitch editing, the sound of the singer breathing between phrases, which
Northlight finds and lets you turn down. See [Breaths](../pitch/breaths.md).

**Buffer**: The slice of audio the computer prepares at a time, set in Audio Setup as
a number of samples. A small buffer means little delay but more work for the computer;
a large one is safer from dropouts but slower to respond.

**Bus**: A channel that other tracks' audio is sent to, so they can be processed and
controlled together: all the drums through one bus, for example. See
[Routing](../mixing/routing.md).

## C

**CC**: Short for MIDI continuous controller: a MIDI message for a control that moves,
such as the mod wheel (CC 1) or the sustain pedal (CC 64), each with a value from 0
to 127.

**Cents**: Hundredths of a semitone, used to measure small differences in pitch. A
note 50 cents flat is halfway to the semitone below. See
[Vocal pitch editing](../pitch/index.md#cents).

**Channel**: One path of audio through the mixer: a track, a bus, an FX track or the
master, each with its own strip.

**Channel strip**: The built-in processing on every channel: EQ, compression, a gate,
a de-esser, saturation and a limiter. See
[The channel strip](../mixing/channel-strip.md).

**CLAP**: A plugin format, newer than VST3, for effects and instruments from other
makers. Northlight loads both. See [VST3 and CLAP plugins](../mixing/plugins.md).

**Click**: The metronome that plays the beat while you record.

**Clip**: A piece of audio or MIDI on a track, which you can move, trim, split and
fade. See [Clips](../editing/clips.md).

**Clip gain**: A clip's own volume, set before the track's fader.

**Clipping**: Distortion when a signal goes louder than the maximum a file or a device
can hold (0 dBFS). The meters show it in red.

**Comping**: Building one best performance from several takes, by choosing which take
plays at each moment. See [Loop, punch and takes](../recording/takes.md).

**Compressor**: An effect that turns loud moments down, to make a sound more even.
See [Compressor](../effects/compressor.md).

**Count-in**: Bars of click before recording starts, so you can come in on time.

**Crossfade**: A fade out of one clip overlapping a fade in of the next, to join them
without a click.

## D

**dB (decibel)**: The unit for loudness and level. +6 dB is roughly twice the signal
level; 0 dB on a fader means no change.

**dBFS**: Decibels relative to the loudest level digital audio can hold, which is
0 dBFS. Everything below it is a negative number.

**De-esser**: A processor that tames harsh "s" and "t" sounds in a vocal.

**Delay compensation**: Some plugins delay the audio they process, to look ahead.
Northlight delays every other path by the same amount, so the whole mix stays in
time. See [Delay compensation](../mixing/delay-compensation.md).

**Dither**: A very quiet noise added when reducing bit depth (to 16-bit, for example),
which hides the distortion that cutting off the extra detail would otherwise cause.

**Dock**: The area along the bottom of the window where panels such as the mixer, the
piano roll and the audio editor open.

**Drift**: In pitch editing, the slow wander of a held note's pitch. See
[Drift, vibrato and slides](../pitch/shaping.md).

**Driver**: The software that lets Windows and Northlight use an audio interface.

**Dropout**: A gap in the sound, heard as a click or crackle, when the computer could
not prepare the audio in time. See [Troubleshooting](troubleshooting.md).

**Dry** and **wet**: The sound without an effect (dry) and the effect's own output
(wet). A mix control blends the two.

## E

**EQ (equaliser)**: An effect that turns ranges of frequencies up or down, to shape
the tone of a sound. See [EQ](../effects/eq.md).

**Export**: Rendering the mix, or every track, to audio files. See
[Exporting](../exporting/index.md).

## F

**Fade**: A gradual rise from silence at the start of a clip, or fall to silence at its
end.

**Folder track**: A track that holds other tracks, so they can be collapsed, moved
and muted together.

**Freeze**: Rendering a track, with its instrument and plugins, to audio in place, to
save processing. A frozen track can be unfrozen to edit it again. See
[Freezing tracks](../mixing/freezing.md).

**FX track**: A track that holds a shared effect, usually a reverb or delay, which
other tracks reach through sends. See [Routing](../mixing/routing.md).

## G

**Gain**: A change in level, in decibels.

**Gate**: A processor that silences a signal below a set level, such as the hiss and
spill between drum hits.

## H

**Headroom**: The space between the loudest part of a signal and the point where it
would clip.

## I

**Input monitoring**: Hearing what a track's input is receiving through Northlight,
with its effects, while you play or sing. See
[Recording audio](../recording/recording-audio.md#input-monitoring).

**Insert**: An effect placed in a channel's path, so all of its audio goes through
it. See [Inserts](../mixing/inserts.md).

## L

**Latency**: The delay between a sound going in (or a key pressed) and coming out of
the speakers. The buffer size sets most of it. See
[Latency and monitoring](../recording/latency.md).

**Limiter**: A compressor that stops the signal going above a set ceiling, used last
on the master. See [Limiter](../effects/limiter.md).

**Loop**: Playing a range of the song round and round. Recording with loop on makes a
new take on every pass.

**LUFS**: Loudness units relative to full scale: a measure of how loud a song sounds
over time, which streaming services use to match levels. See
[Metering and loudness](../mixing/metering.md).

## M

**Marker**: A named point on the timeline, for finding your way around a song.

**Master**: The channel that every track and bus finally plays into, and that you
hear and export.

**Media folder**: The folder beside a project, named after it, that holds its
recordings and imported audio. See [Media and missing files](../projects/media.md).

**MIDI**: Musical Instrument Digital Interface: messages that describe a
performance (which notes, how hard, which controls moved) rather than the sound
itself. An instrument turns them into sound. See
[MIDI tracks and instruments](../midi/midi-tracks.md).

**MIDI effect**: A processor that changes MIDI notes before they reach the
instrument, such as an arpeggiator. See [MIDI effects](../midi/midi-effects.md).

**Mono** and **stereo**: One channel of audio, or two (left and right).

**Mute** and **solo**: Mute silences a track; solo silences every track except the
soloed ones.

## N

**Normalise**: Raising (or lowering) a clip's level so its loudest peak reaches a set
level.

## P

**Pan**: Where a sound sits between the left and right speakers.

**Panel**: One of the views that open in the dock or at the side, such as the mixer,
the Inspector or the browser. Each can also float in its own window.

**Piano roll**: The editor for MIDI notes, laid out as a keyboard against time. See
[The piano roll](../midi/piano-roll.md).

**Plugin**: An effect or instrument that runs inside Northlight. Northlight has its
own built in and loads VST3 and CLAP plugins from other makers.

**Pre-roll**: Bars played before the recording point, without recording, so you can
get into the music.

**Preset**: A saved set of settings for an effect or instrument. See
[Presets](../effects/presets.md).

**Punch**: Recording only within a set range, to replace part of a take while the rest
plays back. See [Loop, punch and takes](../recording/takes.md).

## Q

**Quantise**: Moving notes onto the nearest grid line, to tighten the timing.

## R

**Region**: A named range of the timeline, such as a verse or chorus.

**Ripple**: An editing mode in which deleting something moves everything after it
earlier, to close the gap.

**Round trip**: The total delay from a sound entering the interface to it coming back
out, input and output added together.

## S

**Sample**: One measurement of a sound wave. Also a short recording played by an
instrument such as the [Sampler](../instruments/sampler.md).

**Sample rate**: How many measurements of the sound are taken each second. 44.1 kHz
and 48 kHz are the usual rates.

**Send**: A copy of a channel's audio sent to an FX track or bus, at its own level,
while the channel carries on to the master. See [Routing](../mixing/routing.md).

**Sidechain**: Feeding one sound into an effect on another, so the first controls the
second: a kick drum making a compressor duck the bass, for example. See
[Compressor](../effects/compressor.md#sidechain).

**Slide**: In pitch editing, the glide between two notes that run into each other.
See [Drift, vibrato and slides](../pitch/shaping.md#slides).

**Snap**: Moves and edits landing on the nearest grid line, so they line up with the
beat.

**Stem**: An audio file of one track, or one group of tracks, from a mix, so it can be
mixed or used elsewhere. Export can write every track as a stem.

## T

**Take**: One recorded performance. Recording the same part several times gives
several takes to choose from. See [Loop, punch and takes](../recording/takes.md).

**Take lane**: A row under a track showing one of its takes, used for comping.

**Tempo**: The speed of the music, in beats per minute (BPM). The tempo track holds
changes to it. See [Tempo and time](../editing/tempo.md).

**Time signature**: How many beats are in a bar, and of what length: 4/4, 3/4, 6/8.

**Transport**: The play, stop, record, loop and position controls, in the header bar.

**Transpose**: Moving notes or audio up or down in pitch.

## V

**Velocity**: How hard a MIDI note is played, from 1 to 127. Instruments usually play
harder notes louder and brighter.

**Vibrato**: A regular wavering of pitch on a held note. See
[Drift, vibrato and slides](../pitch/shaping.md#vibrato).

**VST3**: The most widely used plugin format for effects and instruments. See
[VST3 and CLAP plugins](../mixing/plugins.md).

## W

**Windows Audio**: Windows' own audio system (WASAPI), which works with any sound
device. Use it when a device has no ASIO driver.
