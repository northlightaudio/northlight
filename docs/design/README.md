# Aural design specification

This folder is the design source for Phase 7 (design fidelity pass) of
[../PLAN.md](../PLAN.md). It replaces the Figma file the plan originally assumed: every
screen is specified here in words, numbers and tokens, precisely enough that the
implementation can be checked against it screenshot by screenshot.

Status: **draft for review**. Nothing here is implemented yet. Review the brief and the
design system first; the screen specs depend on them.

## Documents

| File | What it decides |
|---|---|
| [01-brief.md](01-brief.md) | Who Aural is for, what it should feel like, the principles every screen follows, and what is borrowed or avoided from other DAWs |
| [02-design-system.md](02-design-system.md) | Colour, type, spacing, radii, elevation, iconography, motion, and the component library with every state |
| [tokens.json](tokens.json) | The machine-readable token file `ui::Theme` loads. Section 2 is the human explanation of this file |
| [03-shell.md](03-shell.md) | The application window: zones, header, docks, panels, resizing, density |
| [04-arrange.md](04-arrange.md) | Arrange view: ruler, track headers, lanes, clips and waveforms, playhead, regions, markers, selection, drag, zoom |
| [05-mixer.md](05-mixer.md) | Mixer: strips, fader, pan, meters, sends, inserts, groups, master and loudness |
| [06-transport.md](06-transport.md) | Transport cluster, time display, tempo, record controls, status and diagnostics |
| [07-plugins.md](07-plugins.md) | Insert chain panel, plugin window, generic parameter editor, plugin browser and scanner states |
| [08-setup.md](08-setup.md) | Audio setup, preferences, first run |
| [09-piano-roll.md](09-piano-roll.md) | Piano roll and MIDI clips (Phase 8), specified now so the design pass covers it |
| [10-interaction.md](10-interaction.md) | State matrix, pointer and drag feedback, snapping, keyboard map, tooltips, notifications, errors |
| [11-accessibility-performance.md](11-accessibility-performance.md) | Contrast, focus, keyboard, screen reader names, DPI, frame budget and how it is measured |
| [12-channel-strip.md](12-channel-strip.md) | The channel strip: module cards with real-time displays in the Editor panel, the EQ analyser, tabs, the mixer's module row |
| [13-pitch-editing.md](13-pitch-editing.md) | Vocal pitch editing (Phase 11): the audio clip editor, pitch analysis, the note editing model, correction to the key, breaths |
| [14-comping.md](14-comping.md) | Take lanes and comping: take groups, the comp as crossfaded clips, recording into takes, swipe comping in the arrange view |
| [contrast.md](contrast.md) | Generated contrast table for every text and surface pair in the token file |
| [review-2026-10.md](review-2026-10.md) | Design review, October 2026: harmony, spacing and anchoring across the window, with what was fixed and what is open |

## Mockups

Static mockups rendered from this spec and `tokens.json` live on the "Aural UI
Mockups" canvas (claude.ai artifact, private to the project owner): the shell with
a populated arrange view, the mixer at full height, transport states, the plugin
chain, window bar and browser, Audio Setup, the piano roll, a component sheet and
the palette/type sheet. They are the visual reference for Phase 7 alongside the
acceptance sections below; where a mockup and the text disagree, the text wins and
the mockup gets fixed.

## How to read the screen specs

Each screen spec has the same shape:

1. **Purpose and anatomy**: what the screen is for and a labelled list of its parts.
2. **Layout**: sizes in device-independent pixels at 100% scale, tokens by name.
3. **Visual**: which tokens colour which part, in each state.
4. **Behaviour**: what happens on hover, press, drag, keyboard, and in empty, loading and
   error states.
5. **Acceptance**: the screenshot comparisons and checks that prove the implementation
   matches.

Sizes are written as `W×H` in px. Colours are token names, never hex, except in
section 2 where the tokens are defined. "Accent" always means the interaction colour,
never a track colour.

## Review checklist for the user

- Does the brief describe the product you want? Everything else follows from it.
- Palette: the dark theme values and the twelve track colours. Change them in
  `tokens.json`; the rest of the spec references them by name.
- Typography: Inter for UI, JetBrains Mono for time and numeric readouts. Both are
  open licensed and would be bundled in `assets/fonts`.
- The shell layout (section 3): header with transport, arrange in the centre, mixer and
  editors in a bottom dock, browser and inspector in a right panel.
- Anything marked **Decision** in the documents is a choice I made that you may want
  to overrule.

## Implementation notes for Phase 7

- `ui::Theme` becomes a loader for `tokens.json` (shipped in `assets/`, overridable
  from the user settings folder for custom themes). The struct shape grows to cover
  every token in the file; nothing in `src/ui` may carry a literal colour or size.
- Components are restyled through `AuralLookAndFeel` and dedicated components where
  JUCE's stock ones cannot reach the spec (fader, knob, meter, segmented control,
  clip, track header, ruler).
- Each screen spec's acceptance section is the definition of done for that screen.
- The order of implementation is the order of the files: system first, then shell,
  then arrange, mixer, transport, plugins, setup, piano roll, then the interaction and
  accessibility passes across everything.
