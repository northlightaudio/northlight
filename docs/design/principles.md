# 1. Design brief

## What Northlight is

A Windows-native digital audio workstation for recording and mixing real performances:
multitrack audio first, instruments and MIDI second. It runs at low latency on
ASIO, hosts VST3 and CLAP plugins, and is built to be lived in for hours at a time.

## Who it is for

- **The recording musician**: tracks a band or themselves, needs arm, monitor, record,
  punch and comp to be fast and obvious, and needs to trust that nothing was lost.
- **The mix engineer**: dozens of tracks, buses, sends and plugins; needs the mixer and
  the arrangement to stay legible at scale and the meters to be honest.
- **The producer**: moves between arrangement, instruments and mixing constantly;
  needs the shell to switch context without losing place.

Not the primary audience for v1: live performers, scoring, podcast editing. The design
should not get in their way but does not optimise for them.

## How it should feel

**An instrument, not a spreadsheet.** The screen is quiet until you touch it. Audio
is the colour; the chrome is not. Precision is visible in the details (tabular
numerals, exact alignment, honest meters) rather than in decoration.

Three words to test any screen against:

1. **Calm.** Dark, low-contrast chrome. Few borders. Colour only where it carries
   meaning: track colours, meters, record, selection.
2. **Direct.** The thing you want to change is the thing you grab. Faders are faders,
   clips are clips, no hidden modes. Every control shows its value.
3. **Trustworthy.** Status is always visible: device, latency, dropouts, what is armed,
   what is recording, what was saved. Errors say what happened and what to do.

## Principles

1. **Hierarchy by contrast, not by borders.** Surfaces step up in lightness; dividers
   are rare and faint. A screen should read as three or four planes, not twenty boxes.
   Panels are flat; only controls have depth (section 2.4).
2. **Track colour is the organising colour.** Clips, headers, mixer strip caps, meters'
   identity and the piano roll all carry the track's colour. The accent colour is
   reserved for interaction: selection, focus, active toggles.
3. **Numbers are instruments.** Every readout uses tabular monospaced figures so values
   do not jitter; units are always shown; precision is fixed per quantity.
4. **One density, scaled.** The layout is designed at 100% and must survive 125%,
   150% and 200% without reflowing into something else. Panels resize, controls do
   not change shape.
5. **Motion explains, never decorates.** Transitions exist to show where something
   went or came from (a panel opening, a clip snapping). Meters and playhead are never
   eased.
6. **Keyboard first, pointer complete.** Everything on the arrange and mixer has a key;
   every drag has a visible snap target and a modifier to override it.
7. **Nothing blocks the audio thread, and nothing blocks the user.** No modal loops;
   long operations (scan, bounce, recovery) run with progress and a cancel.
8. **Nothing is default.** Every element on screen was designed on purpose: its
   shape, size, spacing, type, colour and every state. Working and using the tokens
   is not enough; see the design bar below.

## The design bar

All UI must be well designed, not "default". A screen can pass every automated check
(tokens only, contrast, no literals) and still look like a stock JUCE application;
that screen is not done. Each element has to be considered as a piece of design, and
the reviewer judges it against this list.

- **No stock appearance.** Nothing JUCE draws by default reaches the screen. Every
  control, popup menu, tooltip, scrollbar, alert, combo arrow, text caret and
  selection highlight is drawn by `NorthlightLookAndFeel` or a dedicated component,
  to the design system. A control that still looks like JUCE's default is a bug, even
  if it works.
- **Every state is designed.** Default, hover, pressed, focused, disabled and
  selected or active, and, where they apply, empty, loading, error and overflow. The
  spec names each state and its tokens; none is left to whatever the toolkit does.
- **Alignment and rhythm.** Everything sits on the spacing grid (section 2.3). Edges
  and baselines line up across neighbouring controls, padding is the same inside
  controls of the same kind, and text is centred optically, not just mathematically.
  No stray one-pixel offsets.
- **Typography.** Each piece of text uses a role from the type scale (section 2.2)
  for size, weight and case. Numbers use tabular figures with units. Text that does
  not fit is ellipsised or abbreviated by design, never clipped.
- **Detail.** Icons come from the set and sit on its grid; radii, borders, shadows
  and glows come from the tokens and follow the material rules (section 2.4). Lines
  appear only where principle 1 allows them. Everything is crisp at 100%, 125%, 150%
  and 200%.
- **The edges are designed too.** The minimum window size, 200% scale, no items,
  hundreds of items, a 60-character name, the largest and smallest values: each one
  still looks intended.
- **It belongs.** A screen matches its neighbours and would not look out of place
  beside the references below. A pattern the design system lacks is added to
  [design-system.md](design-system.md) first, then built, so it is designed once and
  reused.
- **No placeholders.** No "TODO" text, sample text, debug outlines, temporary
  colours or unstyled fallbacks in a screen that is called finished.

How it is checked: a change to anything visible is captured with `--capture` at 100%
and 200% (`--capture-scale`), with each state it touches set up through launch
flags, and the captures are compared against the spec and this list before the
change is called done. A screen with no spec gets one, written before or with the
code. Where the result falls short of the bar, the change is not finished, however
well it works.

## References: what to borrow, what to avoid

| DAW | Borrow | Avoid |
|---|---|---|
| Bitwig Studio | Layered dark panels, clip and track colour saturation, the way the inspector and browser dock beside the arrange view, the device chain as a horizontal strip | Visual noise from too many simultaneous panels |
| Ableton Live 12 | Flat, honest controls; everything has a value; the arrangement overview; info text for the hovered control | Dense tiny type; a UI that looks the same whether something is selected or not |
| Logic Pro | Typography and spacing discipline; the mixer's proportions; region names and colours | Skeuomorphic knobs and channel strip imagery |
| Studio One | The clean transport bar and the way the inspector sits with the arrange | Over-rounded controls, gradient buttons |
| Reaper | Sheer efficiency: every action has a key, nothing is more than a click away | Default theme density and inconsistency |
| Pro Tools | The edit window's clarity about what is selected and what the edit mode is; counters | Modal workflow and legacy chrome |

## Visual identity

- **Brand: Machair dusk** ([brand.md](brand.md)). A Hebridean island in early
  evening: peat-green surfaces, an overcast-blue accent, moon-cream text, track
  colours from the land.
- **Name**: Northlight. Wordmark in Newsreader, set on the mark's horizon line; the
  window title uses the project name.
- **Mark**: Moonrise, a half moon risen over the horizon with its reflection on the
  water. Used for the app icon, the empty-project state, the about screen and the
  website. Files in `assets/brand/`.
- **Palette direction**: a dark peat-green base, neutral surfaces stepping up by
  lightness, one accent (Overcast) for interaction, a rowan record red, and twelve
  muted track colours. Defined in [design-system.md](design-system.md).
- **Material direction**: tactile controls on flat panels. Buttons, keys, caps and
  knobs are lit from above with a short shadow; faders run in grooves; meters are
  LED-segmented in a well; readouts sit in wells. Only active toggles glow.
- **Type direction**: one humanist sans for the UI (Inter) and one monospace for
  numerics (JetBrains Mono); Newsreader as the display face for the wordmark, the
  about screen and the website's headings, never for controls.

## What success looks like

- Every screen matches its spec's acceptance checks and meets the design bar: no
  element looks like a toolkit default.
- 60 fps with 64 tracks visible on a 4K display at 150% scale on the reference
  machine.
- Every control reachable by keyboard; every colour pair passes the contrast rules in
  section 11.
- No literal colour or pixel size remains in `src/ui`.
