# 7. Inserts, plugin windows, browser

## 7.1 Insert chain panel (Editor tab, dock)

Selecting a track or bus and opening the Editor tab shows its chain as a horizontal
strip of device cards, so the whole signal path reads left to right.

```
| 01 Kick ▸  [ EQ ─────────── ] [ DC1A3 ──── VST3 ] [ Limiter ───── ] [ + ]
|             ┌─────────────┐   ┌──────────────┐   ┌──────────────┐
|             │ ⏻  EQ     ⋮ │   │ ⏻ DC1A3   ⋮  │   │ ⏻ Limiter  ⋮ │
|             │ [ knobs … ] │   │ [ open GUI ] │   │ [ knobs … ]  │
|             │ mix ──o──   │   │ mix ──o──    │   │ mix ──o──    │
|             └─────────────┘   └──────────────┘   └──────────────┘
```

- Card: `surface1`, radius `md`, 1 px `lineSubtle`, min width 160, height fills the
  dock; header 28 px with power (bypass) toggle, name in `label`, format badge,
  and a "⋮" menu (replace, remove, presets, sidechain, latency "32 smp").
- Body: for built-ins, the built-in's own editor (7.2a); for hosted plugins with a
  GUI, a thumbnail placeholder with an "Open" primary button (opens the plugin
  window) and the three most important parameters as knobs if the plugin exposes
  them; for hosted plugins without a GUI, the generic editor.
- Popped out (its editor open in its own window), a card is edited there, not in
  two places: its body gives way to a note centred in it, the `detach` icon
  (`iconLarge`, `textTertiary`), "Open in its own window" in `small`
  `textSecondary`, "Edit it there" in `small` `textTertiary`, and a "Show window"
  button that brings the window to the front. The card narrows to 224 px
  (`controlLarge` × 7); its header and Mix stay. Closing the window brings the
  controls back. A popped-out card's displays stop reading the processor.
- Footer 24 px: wet/dry mini slider with "Mix 100%".
- Bypassed card: body at 50% alpha, power icon `textTertiary`, header name struck.
- Drag the header to reorder; a 2 px `accent` insertion line between cards. Drag a
  plugin from the browser onto the strip to insert at the line.
- Between two cards, a round "+" (design-system.md "Gap add button") shows while
  the pointer is in the gap, centred in it at the cards' middle. It opens the same
  effects menu as the "+" card, and the chosen effect goes in at that place in the
  chain, one undo step ("Add insert"). Its tooltip names the two cards. It is put
  away while a card or a plugin is dragged, so it never sits on the insertion line.
- The "+" card is a ghost card that opens the plugin menu. Every insert slot's
  effects menu is laid out alike: RECENT (the last effects used), then EFFECTS, a
  submenu per kind with its icon: Dynamics (fx-dynamics), EQ & Filter (fx-eq),
  Reverb & Delay (fx-space), Modulation (fx-modulation), Drive & Colour (fx-drive),
  Guitar (fx-guitar: Corona and the pedals, each with what it is, "Amp",
  "Compressor", "Tuner", as its hint), Stereo & Utility (fx-stereo), Other (plugin);
  a kind with nothing in it is left out. In a kind the built-ins come first, then, past a separator, the scanned
  plugins that report that kind (VST3 subcategories, CLAP features), each with
  its maker and format as a hint at the right in `small` `textTertiary`. Every
  effect row carries its kind's icon (16 px, `textSecondary`). FAVOURITES follow,
  then, past a separator, By maker: the scanned plugins by manufacturer. The
  instruments menu keeps its built-ins, then the scanned instruments by maker.
- Sidechain: a small "key: 02 Hats" chip under the header when set, `accentSubtle`
  fill, click to change.

## 7.2 Plugin window

Native window with a 32 px Northlight bar above the plugin's own editor:

```
| ⏻  DC1A3 · 01 Kick     VST3   mix ──o── 100%   presets ▾   ⟲   📌   ⤢ |
```

- Bar: `surface0`; power toggle, "Plugin · Track" title in `label` (track name in
  `textSecondary`), format badge, wet/dry slider (80 px) with value, presets combo
  (factory programs and user presets, with Save… at the bottom), an "A/B" toggle
  (**Decision**: deferred, keep the slot), pin (always on top) toggle, and a
  generic-editor toggle for plugins that have both.
- The plugin's editor sits below at its native size; the window follows plugin
  resize requests; DPI scaling is passed through.
- Position and size remembered per plugin type; pin state remembered.
- Closing the window never unloads the plugin; the card in the chain shows "open" as
  an `accent` dot on its header while the window is open.

## 7.2a Built-in editors

Every built-in has an editor in the channel strip's language
([channel-strip.md](channel-strip.md) 12.1-12.2): a live, draggable display in a
well, then knobs in the module's hue. The same editor is the card's body in the Editor panel
and the body of its window (under the title, bypass and mix). The hues follow the
strip: EQ `Overcast`, compressor `Loch`, limiter `Rowan`; gain `Machair`, reverb `Lichen`,
delay `Gorse`, glue compressor `Thrift`, Patina `Kelp`, exciter `Machair`, Bloom `Heather`, tape echo `Rowan`, Orbit `Slate`, stereo widener `Sea grass`, Corona `Straw`; the pedals take their family's: Gravity `Loch`, Flare `Rowan`, Comet `Gorse`, Tide `Sea grass`, Polaris `Overcast`. The instruments (7.4b-7.4e) have faces of their own, and so do Patina, the tape echo, Orbit and Corona, hardware faces (design-system.md "Hardware face").

- **Gain**: the input and output history with IN and OUT meters; Gain.
- **EQ**: the strip's EQ graph with the built-in's four bands (a high-pass,
  off at 0 Hz, sitting at the left edge; low shelf; mid with Q on the wheel; high
  shelf) and the analyser. The selected band's knobs below under its name.
- **Compressor**: the channel strip compressor's card (they are one compressor):
  the transfer curve, the history with the threshold
  line, the IN threshold meter, GR and OUT; Ratio, Attack, Release, Knee, Makeup,
  Mix; Auto make-up and Peak / RMS. The insert also takes a sidechain key, tapped
  before or after the source's fader: a Sidechain button beside Peak / RMS picks
  it (the same items as the slot menu) and, while a key is set, lights in the
  module's hue as "Key: Kick (post)"; the threshold meter's caption reads KEY
  and it, the history's grey trace and the threshold all follow the key.
- **Limiter**: the history with the ceiling, the ceiling on the IN meter, GR and
  OUT; Release, Lookahead.
- **Glue Compressor** (a console-style VCA bus compressor, for a mix or a group):
  feed-forward and stereo linked with a 4 dB soft knee; Ratio 1.5, 2, 4 or 10:1;
  Attack 0.1, 0.3, 1, 3, 10 or 30 ms; Release 0.1, 0.3, 0.6, 1.2 s or Auto (a fast
  stage releasing over 0.1 s and a slow one that charges from it over 0.4 s while the
  compression is held and
  releases over 1.2 s: quick back from a hit, slow back from held compression);
  Key HP on the detector (off at 20 Hz, up to 400 Hz) so the low end does not pump
  it; Makeup 0 to 20 dB; Mix for parallel compression. No latency. Its editor shows
  what glue does rather than a transfer curve:
  - **Dynamics ribbon** (the display, flexible width): the last three seconds,
    each column a band from the 50 ms RMS (the body) to the peak, the input
    `textTertiary` at 32% under the output in the hue at 42%, with the output's
    RMS as a 1.5 px line in the hue; -48..0 dB with the dB grid every 12 dB; peaks
    fall 30 dB a second across the columns, as the other histories. The two bands
    show what the settings do to the distance between body and peaks: a fast
    attack holds the peaks and the coloured band sits thinner; a slow attack lets
    the transients through over a lowered body, and it sits wider. The threshold is the
    dashed line with its value (drag anywhere in the ribbon, double-click resets).
    At the top left, **CREST** in `captionCaps` and the crest factor (peak over
    RMS) of the last second going in and coming out, "14.2 → 9.6 dB", the input
    figure `textSecondary`, the arrow `textTertiary`, the output in the hue; "–"
    in silence.
  - **GR arc** (to its right, up to 120 px, centred in its well with room for its
    labels): ten LED segments over 240°, 7 px thick, for 0.5, 1,
    2, 3, 4, 6, 8, 10, 14 and 20 dB, lit in the hue (`meterHot` past 10 dB),
    `surface2` when dark; the deepest reduction of the last `motion.peakHoldMs`
    holds as an outlined segment, then falls at 20 dB a second; 1, 4, 10 and 20
    labelled outside in `caption`; the reading in `value` in the middle (`meterHot`
    while reducing) over GR in `captionCaps`.
  - Controls, in three groups split by `lineSubtle` rules: Threshold and Makeup as
    46 px knobs with 0-10 scale dots; Ratio, Attack (ms) and Release (s) as rotary
    selectors (design-system.md); Key HP ("Off" at the bottom) and Mix as knobs.
    Every control's label sits on one baseline along the bottom of the row.
- **Reverb**: the decay: the dry sound, the pre-delay, early reflections (more and
  longer for a bigger room), the tail falling 60 dB over the decay and the highs'
  faster fall (dashed) for the damping. Drag the tail's start for the pre-delay, its
  end for the decay. Size, Damping, Low cut, Width, Mix.
- **Delay**: the echoes over time as bars as loud as the feedback leaves them,
  left up and right down when ping-pong alternates them, darker as the tone falls.
  Drag the first echo for the time, the second for the feedback. Time, Feedback,
  Tone, Mix, a Ping-pong pill and a Sync chip: off (the time in ms) or a note
  value from 1/64 to 1/1, straight, triplet or dotted, at the project tempo.
  Synced, the Time knob dims and shows the note value, and dragging the first
  echo snaps to the nearest one.
- **Patina** (a vintage colour processor: six stages that age a sound in
  different ways, under one Age control): in signal order **Heat** (drive into
  a Tube, Tape, Iron or Fuzz curve, anti-aliased by ADAA and level-held, with a
  tilt), **Chamber** (a Room, Spring or Plate, mixed over the sound), **Drift**
  (wow and flutter of a Reel, a cassette Deck or a Disc, with a rate and a
  stereo spread), **Grain** (Vinyl crackle, Tape hiss, 50 or 60 Hz Hum, or room
  Air, which can duck under the music or ride with it), **Wear** (lost highs,
  dropouts, azimuth skew) and **Crush** (bits and rate, Soft, Hard, Mu-law or
  Jitter). Each stage has a switch, a main amount and a Breath that lets the
  amount wander at the Pace beside Age. **Age** scales every stage's amount:
  half way plays them as set, down to nothing makes the sound new, up to full
  takes each toward ruin in proportion to how much of it is used. An output
  section follows: Tone (a tilt about 900 Hz), Low cut and High cut ("Off" at
  their ends), Width and Output. Every switch and choice fades over 10 ms. No
  latency. Twelve factory presets (Gentle Age, the defaults, first). Its editor is
  a hardware face built from rendered parts (design-system.md "Rendered parts";
  `assets/patina`, rendered by `scripts/render/patina`), a 1960s broadcast colour
  processor drawn at 1160 x 680 and scaled to fit the card's body, the card
  narrowing with it (the tape echo's and Corona's rules; compact below 65%). Every
  control shows at once; nothing on the face is a tab. Text is Barlow legends
  (`legendFont`) set crisply by the face; the inks are `patinaInk` on the enamel
  (`patinaInkSoft` for values, captions and scales) and `patinaPanelInkSoft` on
  the hammertone:
  - **The panel**: slate-green hammertone enamel (`panel.jpg`, flat
    `patinaPanel` without it) under everything.
  - **The top row**, on the hammertone. At the left the **nameplate**
    (`nameplate.png`, 300 x 56: a flat, polished brushed stainless plate, square
    corners and a hairline edge, catching a sheen across its left; after decades
    on a rack its middle is still bright and the grime, stains and pits gather
    toward its edges and round a screw in each corner; `patinaNameplate`),
    etched: the Northlight mark in a 34 px square (a 1.6 px rule), PATINA at 31 px
    bold with 0.34 tracking and under it NORTHLIGHT · STUDIO EQUIPMENT at 11 px
    with 0.26 tracking, left aligned beside the square. Etched means the lettering
    in `patinaInk` at 90% over a `highlight` edge at 35% 0.8 px below it, where the
    cut's lower wall catches the light. Beside it the model line, "VINTAGE COLOUR PROCESSOR ·
    MODEL PT-6", 13 px with 0.24 tracking in `patinaPanelInkSoft` (hidden in
    compact). At the right the **preset selector**: two 38 x 32 bakelite keys
    (the hardware key, an arrow alone on each) either side of the amber display
    in its black bezel (`display.png`, 300 x 44; the display fills its 288 x 32
    opening): the preset's number ("07", "--" for Custom) in `value` at 95% size
    in `displayDim` at 75%, then its name in capitals in `value` at 120% size,
    0.08 tracking, `displayText` over a `displayGlow` halo at 22% (brighter on
    hover), the line centred on the capitals' height; scan lines every 3 px and a
    5 px `shadow` shade at 45% under the bezel's lip; the glass follows the
    opening's 3 px corners. The keys step through the
    same list as the header's preset bar (7.2b), the user's presets too
    (wrapping; from Custom, Next goes to the first); a click on the display shows
    the preset menu. A preset is one undo step ("Preset Worn Cassette"); any
    change of a value makes it Custom. Tooltips: "Previous preset", "Next
    preset", "Presets". Right of them the **pilot lamp** (Corona's red jewel, lit
    while the insert is on).
  - **Six stage modules** in signal order, 142 x 568 each, 8 px apart, on grey
    enamel plates (`module-1.png` to `module-6.png`, four screws; `patinaPlate`),
    each worn its own way. Every painted part of the face is worn as decades-old
    rack gear: paint chipped back to steel along the edges and corners, flakes
    lifted from the face, grime and streaks, the enamel yellowed, the hammertone
    dusty. Down a module:
    - its name (18 px bold, 0.14 tracking; 21 px in compact) at the left and its
      **push-button** at the right: a 36 x 24 nickel bezel round an amber lens,
      dark when the stage is off, pressed in and lit when on (`button-off.png`,
      `button-on.png`, drawn 60 x 45 with their shadow and glow). A click, Space
      or Enter switches the stage (one undo step, "Heat on"); hover lightens the
      lens; focus draws the focus ring round the bezel. Tooltip: "Heat: on".
    - its **window**: a black crinkle bezel (`window.png`, 126 x 96) round the
      lit glass (112 x 82, cut to the opening's 3 px corners, which follow the
      bezel's outer curve), the stage's live readout; under
      the bezel a row of eight 10 x 4 px lamps, 3 px apart: lit `led` (the last
      two `ledHot`) with a 35% halo up to the amount Age and Breath leave right
      now, dim (the colour at 40%) on to where Age alone puts it, the rest
      `ledOff`. Left and Right move the focus between windows; Space or Enter
      switches the stage; focus draws the focus ring round the glass.
    - its **type selector**: a 30 px rendered knob that clicks between its
      positions, fanned 52 degrees apart across its top; a dot at each position
      (the chosen one `dialRed`) and its legend beyond it (12.5 px, the chosen
      one in `patinaInk`, the rest `patinaInkSoft`), each legend's near edge
      9 px past the knob's rim whatever its width; the caption under it (11.5 px
      in `patinaInkSoft`). Compact, the legends give way to the setting, printed
      where the caption is. Wear has no type: its Dropouts and Skew knobs take
      the selector's place.
    - its **main knob** (52 px) over eleven printed ticks, its name under it
      (16 px bold) and its value under that (13 px, `patinaInkSoft`).
    - its **small knobs** (30 px), two to a row in up to two rows (a row's last
      knob alone is centred), each with its name (14.5 px bold) and value
      (13.5 px), the name's capitals a clear 6 px below the scale's ticks.
  - **The master plate** (216 x 568, `master.png`): **Age**, a 96 px knob over
    eleven ticks with NEW and RUINED printed at its two ends (16 px bold,
    `patinaInk`), its
    name at 22 px and value at 15 px; **Pace** under it ("0.16 Hz"); then the
    output section under an OUTPUT caption between two rules: Tone, Low cut,
    High cut in a row, Width and Output centred under them.
  - Every knob is the rendered knob (`knob-body.png`, `knob-pointer.png`: a flared
    black bakelite skirt, a fluted body, a spun aluminium cap with a red pointer
    line) drawn over its printed ticks; it turns by drag, wheel and keys, a
    double click resets it. Compact (the Editor dock), values and the selectors'
    legends drop, names print larger (small knobs' 1.5 times) and the knob in
    hand shows its value where its name is.
  - The readouts in the windows. Lit (the stage on), the glass is a radial
    `glassCentre` through `glassMid` to `glassEdge` and the readout draws in
    `glassInk`. Switched off, the window goes dark: smoked glass, a radial
    `glassOffCentre` to `glassOffEdge`, with no readout, grid or figure, and its
    lamps all `ledOff`. Where the readouts below say the hue, read the ink;
    `textSecondary` and `textTertiary` are the ink at 55% and 50%, the grid the
    ink at 16%, and the ring of Heat's dot the glass's centre colour. The figure
    sits at the top right. Over the glass, a 6 px `shadow` shade at 45% under the
    bezel's lip and a `highlight` reflection (22% lit, 10% unlit) across its top
    left.
    - Heat: the static curve at the live amount (input across -1..1, output
      up, in a square), the unity diagonal dashed, the loudest input of the
      last 50 ms as a dot riding the curve (a 7 px `textPrimary` disc round a
      4 px hue one); the figure: how much the curve took off the peaks over the
      last 200 ms ("−2.7 dB", the hue while above 0.1 dB, else `textTertiary`).
    - Chamber: the wet level over three seconds (-60..0 dB, a grid line every
      20 dB) as a hue area at 32% under a 1.5 px hue line; the figure: the decay
      time ("2.1 s").
    - Drift: the speed error over three seconds in cents, the left side a
      1.5 px hue line and (with Stereo above zero) the right a 1 px
      `textSecondary` line at 70%, about a centre line with dashed limits; the
      scale grows to fit what plays (±5, 10, 20, 40 cents, its label at the top
      left in `caption` `textTertiary`); the figure: the widest swing of the
      last second ("±8.7 c").
    - Grain: the noise's RMS as a hue area at 36% with a 1 px hue edge and the
      music's peak as a 1.5 px `textSecondary` line, -84..0 dB, grid every
      24 dB, so ducking and riding show; the figure: the noise level ("−38 dB",
      "–" when silent).
    - Wear: the surviving bandwidth (the high cut on a log axis, 1 to 20 kHz,
      grid at 2, 5 and 10 kHz, "20k" at the top left) as a hue area at 26% under
      a hue line; each column keeps its lowest value so dropouts show as
      notches; the figure: the cutoff ("11 kHz"), or the depth of a dropout
      while one plays ("−14.2 dB", in the hue).
    - Crush: a scope of about six of the converter's samples from a rising zero
      crossing: what goes in as a 1 px `textSecondary` line at 60%, what comes
      out as 1.5 px hue steps; the figure: the bits and rate in effect
      ("6 bit · 6.0 kHz").
- **Exciter** (adds harmonics to flat sources; psychoacoustic bass):
  - **How it works.** The band above Frequency (700 Hz-16 kHz, 24 dB/oct) is
    normalised to a fixed level and driven into one of four curves:
    - Tube: the octave and the twelfth, warm.
    - Tape: odd orders, an edge.
    - Air: mostly the octave, placed an octave above the crossover.
    - Crisp: a dense even series, bright.

    Only what the curve adds is mixed back: its linear part is taken out, at a
    level calibrated per curve and drive. So Drive sets the character alone,
    and Amount sets the harmonics against the band: 50% is 6 dB under it, 100%
    is 6 dB over it.
  - **Anti-aliasing.** The curves run at twice the rate (polyphase IIR) with
    ADAA, on the harmonics only. No latency.
  - **Gate.** Below -84 dBFS of band level nothing is excited.
  - **Focus** tilts what is added: Dark to Edge, ±4 dB about three times the
    crossover.
  - **Dynamics** runs from Fill (more in the quiet moments and tails) through
    Static to Push (more on the loud moments), against the band's 600 ms
    average. **Transients** adds up to 15 dB more on onsets.
  - **Low end.** Harmonics of the mono band under Bass Freq (30-250 Hz), kept
    between it and eight times it. Amount 100% matches the low band.
  - **Output stage.**
    - Width spreads the harmonics into a decorrelated side copy; the mono sum
      never changes.
    - Mix scales everything added.
    - Output.
    - Auto gain (on by default) turns the output down by the K-weighted
      loudness the exciter adds, never up.

  The editor shows what is added, where and when:

  - **Harmonic spectrum** (the display, flexible width; -96..0 dBFS tilted
    +3 dB/octave about 1 kHz; 20 Hz-20 kHz on the EQ's grid and labels;
    dB lines every 24 dB in `gridBeat`):
    - **Traces.** An 8192-point Hann FFT of the processor's two taps, 30 times a
      second, rising quickly and falling at 40 dB/s:
      - the input as a `textTertiary` fill at 16% under a 1 px `textSecondary`
        line at 45%;
      - what is added as a gradient fill in the hue (45% to 8%) under a 1.5 px
        hue line.

      Where the colour rises out of the grey, the exciter has made energy that
      was not there.
    - **Regions.** The region above the crossover and the region below the bass
      frequency are tinted with the hue at 7% while their section adds anything.
    - **Handles.** Each frequency is a vertical handle:
      - Active: a 1 px hue line at 70%. While its section is at 0%: dashed
        `textTertiary` at 60%.
      - Hover or drag: `textPrimary` at 85%, 1.5 px.
      - The shared 9 px handle disc sits 12 px above the plot's bottom
        (`textPrimary`, or `textSecondary` while inactive) with a 1.5 px hue
        ring (2 px on hover).
      - Its frequency is in `value` beside it, on the side with room, on a
        `meterWell` plate at 80% (radius `sm`) so traces never cross the figures.
    - **Mouse.** A press moves the nearer handle to the pointer and drags it (one
      gesture), and double-click resets the nearer one. The cursor is a
      left-right resize within 6 px of a handle.
    - **Legend.** Top left: 8 px rounded swatches with "Input" and "Added" in
      `caption`.
    - **Empty state.** When the analyser has nothing: "Play something to see
      what is added" in `caption` `textTertiary`, centred.
  - **Excitation history** (to its right, 208 px or a third of the display):
    - The last three seconds:
      - the input's 50 ms RMS as a `textTertiary` area at 32%;
      - the added RMS as a hue area at 42% with a 1.5 px hue line.
    - Range -72..0 dB, the grid every 12 dB with labels at the right as in the
      Glue's ribbon.
    - Static dynamics keep the two a set distance apart. Push widens the gap on
      loud moments, Fill on quiet ones, and Transients on each onset.
    - Top left: **ADDED** in `captionCaps`, then the added level against the
      input over the last 300 ms in `value` in the hue ("-26.8 dB"); "–" in
      `textTertiary` in silence.
    - The card header's live readout is the same figure in the hue.
  - **Controls,** in four groups. Each group has a `captionCaps` caption in
    `textTertiary` on a 16 px row above it (HARMONICS, LOW END, DYNAMICS,
    OUTPUT), and `lineSubtle` rules between the groups run from the caption row
    to the controls' foot:
    - HARMONICS: Character, a rotary selector (Tube, Tape, Air, Crisp); Amount, a
      46 px knob with 0-10 scale dots; Drive; Frequency; Focus ("Dark 40",
      "Flat", "Edge 40").
    - LOW END: Amount, a 46 px knob with scale dots; Frequency.
    - DYNAMICS: Dynamics ("Fill 35", "Static", "Push 60"); Transients.
    - OUTPUT: Width; Mix; Output; Auto gain. Auto gain is a "Match" pill where
      a knob's ring sits; under it, where a knob shows its value, the live trim
      ("-1.8 dB", or "Off" in `textTertiary`), then its label.

    Every label sits on one baseline along the bottom of the row.
  - **Size.** The card is 939 px wide at 100% and its body is 300 px tall.
- **Bloom** (an extreme reverb for use as an effect: huge, frozen, shimmering,
  swelling, gated or ducked tails): a feedback delay network of eight modulated
  lines with an allpass in each, mixed by a Householder matrix. Decay 0.4 s to 60 s
  (RT60); Size from a large room to an absurd space (5 to 88 m), gliding like tape;
  Freeze holds the tail for good, the input kept out, the shimmer and damping out of
  the loop and a little modulation always on so it keeps moving. Shimmer pitch-shifts
  part of each pass inside the loop (Pitch -12, +5, +7, +12, +19 or ±12 semitones),
  so the tail climbs over seconds; Swell feeds the input in through a rising cloud
  of taps so the tail builds like a reversed reverb (off, or 10 ms to 4 s); Gate cuts
  the tail a set time after the input stops (off, or 40 ms to 2 s; never while
  frozen); Duck lowers it under the dry input (up to 24 dB, Release 50 ms to 3 s).
  Low cut and Damping act inside the loop as shelves: lows and highs past them decay
  four times faster than the decay at any decay time. Tilt ±6 dB about 900 Hz after
  the loop, Pre-delay 0-500 ms or a note value (Sync), Diffusion, Mod depth and Rate,
  Width, Mix. A governor holds the network's energy under a ceiling, so no setting
  runs away. No latency; bypassed, the network keeps running so the tail is current
  when it comes back. Its editor shows the tail as a blossom:
  - **The face**: the card's body filled `surface0` (radius `md`); the controls'
    tiles `surface1`, radius `lg`.
  - **Blossom** (the display, at the left, as tall as the body; its field 1.32 times
    as wide as it is tall so the corner readouts clear the flower). The tail alone
    (tap 1, before the mix) over the last five seconds as a flower: time runs
    clockwise over 330 degrees to now at the top (a 15 degree gap either side, a
    1.5 px `textPrimary` line and a 6 px dot at 12 o'clock), frequency rises
    outward, 50 Hz to 16 kHz on a log scale, from a quarter of the radius to 0.885
    of it. 56 slices by 14 bands of petals, each separated by a hairline (0.7
    degrees, narrowing outward; 1.2 px between bands), each band the mean power of
    its bins (2048-point FFT, Hann), -96 to -24 dB, coloured as the hue fading in
    (alpha `v^1.4 x 0.95`, at most 92%) and warming toward `textPrimary` at the top
    20%. A slice heard while frozen is coloured in ice (`accent` 40% toward
    `textPrimary`), so a held tail turns to ice slice by slice. The petals are drawn
    through a per-pixel lookup into an image held at twice the flower's size, redrawn
    only when a slice arrives. Ground: a radial gradient from `canvas` 13% toward the
    hue (ice while frozen) at the heart through 4% at 70% to `canvas` at the rim.
    Dotted rings (`textPrimary` at 7%) at 100 Hz, 1 kHz and 10 kHz, labelled in the
    gap at the top in `caption - 1` mono `textTertiary`. **Rim**: the tail's RMS per
    slice (-60..0 dB) as a band in the hue at 85% on a 0.906 radius track
    (`textPrimary` at 6%), up to 0.047 of the radius deep; the dry input's RMS as a
    1.5 px `textSecondary` tick outside it, for slices above a fifth of the scale.
    **Shimmer** (above 2%, not while frozen): three dotted spirals (1 px dots every
    5 px, 1.2 px) in the hue at 20% + 45% of the amount, climbing outward (falling
    inward for -12; one of three inward for ±12). **Corners**: top left DECAY
    (`captionCaps` `textTertiary`), the RT60 in mono `title + 1` `textPrimary` and the
    highs' ("2.7 s at 8 kHz", `value` `textSecondary`); frozen, "∞" over "held". Top
    right SPACE, the size in metres and the pre-delay ("20 ms before", or the note
    value). Bottom left the legend: a 9 px hue dot TAIL, a 9 x 2 px `textSecondary`
    bar DRY; bottom right "5 S · NOW AT THE TOP"; captions `captionCaps`
    `textTertiary`.
  - **The seed** (Freeze), at the flower's heart, 84% of the inner radius's
    diameter plus 16 px: a round button. Off: a `canvas` moat at 90% with a hue glow
    (22% fading out) round it, a body from `canvas` 14% toward the hue to 5%, a
    1 px hue rim at 40% (70% on hover), a 1.5 px snowflake and FREEZE (`captionCaps`)
    in `textPrimary` at 85% (full on hover). Held: an ice glow at 45%, an ice body
    (brighter at the top), a 1 px rim of ice brightened, the snowflake and FROZEN in
    `canvas`. Pressed darkens the body. Click, Enter or Space toggles it (one undo
    step); focus draws the focus ring round it; its hit area is the circle.
  - **Large controls** (right, 644 px wide in all): Decay ("6.0 s"), Size ("27 m"),
    Shimmer (%) and Swell ("Off" at the bottom) as 52 px bud knobs (design-system.md
    "Bud knob") in four equal tiles, their names in `label` `textSecondary`. Frozen,
    Shimmer and the interval pills fade to 50% (the shimmer has left the loop).
  - **Shimmer interval** (a tile, the row's width less the next tile): SHIMMER
    INTERVAL in `captionCaps` `textTertiary`, then six 26 px pills 6 px apart: -12,
    +5, +7, +12, +19, ±12 in `value`. The chosen pill is filled in the hue with its
    text in `canvas`; the others have a 1 px `textPrimary` outline at 15% and
    `textSecondary` text, filled `surface2` with `textPrimary` text on hover. Click a
    pill, or Left, Right, Home and End; focus rings the chosen pill. Tooltip
    "Shimmer interval".
  - **One hit** (a 196 px tile beside it): ONE HIT, then the envelope: what one hit
    becomes, -60..0 dB: the dry hit (a 2 px `textTertiary` bar at 0), the pre-delay,
    the swell rising, the decay falling 60 dB over the RT60 (a 1.5 px hue line over a
    hue gradient from 32% to 4%), the highs' faster fall dashed (hue brightened,
    80%), the gate's vertical cut; frozen, a flat line to the edge with ∞ in the
    hue. Grid at -20 and -40 dB (`gridBeat`). In a well under 56 px tall it is
    compact: the curve fills the well (5 x 4 px in) and only the span shows, at the
    top right, hidden while frozen; taller, 0 and the span sit under it.
  - **Secondary controls** (28 px bud knobs in 50 px cells) in one row of four
    groups 14 px apart, each under a `captionCaps` `textTertiary` caption and a 1 px
    `textPrimary` hairline at 8% running to the group's end: SPACE (Pre-delay,
    Diffusion, Motion, Rate; the Sync chip, a CornerChoice, at the caption's right,
    the hairline stopping 8 px short of it: "Sync off" or the note value, and synced
    the Pre-delay knob dims and shows the note), TONE (Low cut, Damping "6.0 kHz",
    Tilt), DYNAMICS (Duck, Release, Gate, "Off" at the bottom), OUTPUT (Width, Mix).
  - Header readout: "Frozen" in ice while frozen, else the ducking ("Duck -4.2 dB",
    `textSecondary`) while it ducks.
  - Body 316 px; width the flower's field (1.32 x the body's height less 8 px), a
    20 px gap and the 644 px of controls.
- **Tape Echo** (a tape loop machine, the Delay's characterful sibling):
  - A record head and four playback heads at 1, 2, 3 and 4 times head 1's time. Eight
    head combinations: 1, 2, 3, 4, 1+3, 2+4, 1+3+4, all.
  - Feedback is the active heads' mean, so 100% is the edge of oscillation; it goes
    to 120%.
  - Time is the motor speed (head 1, 20-1000 ms). Synced, it is head 1's note value.
    A change glides like a motor with a flywheel: the echoes' pitch bends while it
    settles, with no crossfade.
  - Wow and Flutter wobble the tape speed, with randomness. Flutter's rate follows
    the motor.
  - Every pass round the loop goes through the record path again: the Bass and
    Treble shelves, the tape's saturation with pre- and de-emphasis (anti-aliased),
    and the tape's high loss (darker with Age and at slow speeds). Hiss breathes
    under the echoes and dropouts travel round the loop, both growing with Age. Each
    repeat therefore darkens and wears.
  - Past the edge, the loop sustains into the saturation, bounded.
  - Drive sets the record level, and the echoes are compensated by half.
  - Stereo is Pan (the active heads alternate left and right; a lone head is
    centred), Ping (ping-pong) or L/R. Spread sets the width.
  - Spring is a send to a short dispersive spring (its chirp and drip) on the echoes
    only.
  - Mix, Output, and Kill dry for use on a send. No latency.
  - **The face:** a 1970s tape echo console, a hardware face built from rendered
    parts (design-system.md "Hardware face", "Rendered parts"; assets/tapeecho). It is
    drawn at 1160 × 600 and scaled to fit the card's body, never above 100%: the plugin
    window shows it 1:1, and in the Editor dock it scales to the dock's height and the
    card narrows with it. Legends are Barlow Condensed (`font.legend`) in the
    `hardware` console inks, printed without a shade on the light enamel.
    - **Panels.** Cream enamel above (`creamPanel`, inks `creamInk`/`creamInkSoft`),
      sage below (`sagePanel`, `sageInk`/`sageInkSoft`), one rendered steel plate in
      two colours with a brushed aluminium trim between them. Worn where hands go:
      polish round the controls, grime at the edges and the seam, a few chips.
    - **Name column** (left of the window): TAPE ECHO in 46 px bold, FOUR-HEAD ·
      SPRING (16 px) under it; at the foot, level with the window's, a 28 px rule,
      MODEL TE-4 (18 px bold) and NORTHLIGHT (14 px). The heads' names under the
      window and LOOP GAIN are 16 and 15 px. Legends are sized to stay legible with
      the face scaled to about half in the dock.
    - **The transport window** (642 × 212, a slim aluminium bezel, the transport
      624 × 194 inside it): the tape path under a smoked perspex lid, lit warm by
      lamps under the window's front edge. Left to right: two flanged guides, the
      erase and record heads and playback heads 1-4 on brackets, the tension arm, the
      capstan and pinch roller, and the tape chamber under its printed cover. The
      heads are named under the bezel (ERASE, REC, 1-4), each centred on its head.
      - Each record and playback head has a **neon** on its bracket, as bright as
        the level passing it (the frames' `extra` at the head's distance round the
        loop): it strikes 42 dB down, is full at full scale, lights at once and dies
        away over about 120 ms. A head that does not play shows the tape passing it
        at 35%. The neons are their light alone over the transport, so their spill
        on the brackets and deck is part of them.
      - The pinch and tension rollers turn with the tape at the motor's speed (the
        tension roller, half the size, twice as fast), slowed together so the
        fastest makes at most three turns a second. Only their screw slots and
        drillings turn; the chrome and its highlights stay put. Stopped while the
        card is off and with reduced motion.
    - **The meter** (a VU, right of the window, its top on the window's): the tape's
      level at the record head, each frame's peak, 0 VU at -10 dBFS (the tape saturates
      towards -6, so a healthy level reads about 0 and only a hot tape reaches the
      red), on a scale linear in voltage (-20 to +3, 0 VU about
      71% across; red from 0, `dialRed`), its needle moving with a VU's 300 ms
      ballistics under the pivot's hood and the glass. Tooltip-free; its accessible
      description is the reading in VU.
    - **Loop gain lamp** (a jewel lamp under the meter, LOOP GAIN beside it): dark
      while the loop is steady (a loop gain below 0.85) and while the card is off;
      amber from 0.85, as the loop nears the edge; red and flickering at 1 and above
      (steady with reduced motion). Tooltip "Loop gain 0.45", ", singing" past
      the edge.
    - **Big knobs** (72 px skirts, a printed 11-tick scale, 0 5 10 in 18 px): HEADS (a stepped
      knob: eight positions 1, 2, 3, 4, 1·3, 2·4, 1·3·4, ALL printed round it over
      270°, the chosen one bold; value "1 + 3"), REPEAT RATE, INTENSITY (feedback) and
      ECHO VOLUME (mix). Name (21 px bold) and value (19 px) under each on one
      baseline; HEADS's positions in 17 px.
    - **REPEAT RATE** is the motor's time; with SYNC on it steps through the note
      values instead, its scale reading 1/64, 1/8 and 1/1 and its value the note.
    - **Levers** (cream-tipped bat handles at 85% of their render, over the last three
      small knobs; names 19 px bold, positions 18 px): STEREO
      three-way (PAN up, PING centre, L/R down), SYNC (ON: tempo sync at the last
      note value chosen, an eighth the first time; OFF: free), DRY (ON, KILL). A click
      anywhere on SYNC or DRY flips it; on STEREO, a click above its middle moves it
      up a position, below it down. Up and Down move a lever, Space flips or steps
      it. Tooltip "Stereo: Pan".
    - **Small knobs** (44 px; names 20 px bold, values 17 px): BASS, TREBLE, DRIVE, WOW, FLUTTER, AGE, SPREAD,
      SPRING, OUTPUT, spread across the panel's width.
    - **Rendered controls** keep the system's behaviour: drag, wheel, arrows,
      double-click to reset, focus ring round the skirt or nut, a 7% highlight on
      hover (10% while dragged), 45-50% when disabled. Every part has a flat token
      fallback (panels, a dark well, plain knobs and levers, a lit dial with a hood),
      so the face works without its images.
  - **Compact** (below 65%, the Editor dock): no values, scale marks, HEADS
    positions, lever positions, dial numbers or model line; names 1.45x; HEADS
    shows its heads ("1 + 3"); the knob in hand shows its value in its name's
    place.
  - **Header status:** "Self-oscillating" in `meterHot` while the loop gain is at
    1 or more.
- **Orbit** (a studio tape machine, for glue and colour on a bus or the master; in
  the menu's Drive & Colour):
  - **The tape** is a Jiles-Atherton hysteresis model: the magnetisation lags the
    field, so loud passages compress softly and the harmonics are mostly odd, run at
    2x to 4x the session rate (about 192 kHz). At +6 a tone at 0 VU (-18 dBFS) is
    about 0.2% distortion; full scale about 8%.
  - **The machine round it.** The record EQ boosts the highs before the tape and
    playback takes them back out, so the highs saturate first, the louder the more.
    SPEED (7½, 15, 30 ips) moves the head bump (about 26, 52 and 105 Hz), the low
    roll-off under it and the gap loss (13, 19 and 24 kHz). EQ is NAB (50 µs, and
    3180 µs in the lows) or CCIR (70 µs at 7½, 35 µs at 15); 30 ips is AES (17.5 µs)
    whichever is chosen. LEVEL (+3, +6, +9 dB over 185 nWb/m) is how hard 0 VU hits
    the tape. TAPE is Classic (saturates early), Hot or Modern (the most headroom,
    the cleanest).
  - **BIAS** (-100 to +100, Cal at 0): under-biased tape shows its hysteresis even
    quietly and keeps a little more top; over-biased is clean and dark.
  - **INPUT** drives the tape (-12 to +18 dB); with **LINK** on, OUTPUT follows it the
    other way, so the loudness holds while the tape works harder. **HISS** (0 is
    off; more at a slower speed, each track its own) and **FLUTTER** (wow and
    flutter, weighted rms, to 0.3%: a slow wander and the capstan's flutter, faster
    at a higher speed) wear it. **MIX** blends the dry signal, delayed to match.
  - **Latency** 1.5 ms (it holds the flutter's wander), reported, and kept while
    bypassed.
  - **The face:** a studio reel-to-reel in a dim mastering room, a hardware face of
    rendered layers (design-system.md "Hardware face", "Rendered parts";
    assets/orbit), drawn at 1160 × 600 and scaled to fit the card's body as the tape
    echo's is. A warm spot pools on the left reel, the right falls into shadow with a
    cool blue edge, a low warm fill runs along the panel. Every word is printed over
    the renders in Barlow (`font.legend`) in the `orbit` inks.
    - **The transport plate** (brushed aluminium, the top 330 px): the two 10½-inch
      NAB metal reels (centres 190 and 970 px, 165 down), the tape running through
      the guides, over the erase, record and play heads (named under them, 13 px,
      `orbitHeadInk`) and past the capstan and pinch roller. ORBIT (50 px bold,
      tracked) and STUDIO TAPE MACHINE · ½-INCH TWO-TRACK (16 px) at the top centre,
      NORTHLIGHT · MODEL OR-2 (12.5 px) above the heads, in `orbitPlateInk`.
    - **The reels** turn anticlockwise while the song plays and the insert is on,
      each at the tape's speed over its pack's radius (the full supply reel slower
      than the take-up), and when the song stops they coast on to rest. They are
      still with reduced motion.
    - **The counter** (a black window behind glass, 108 × 26) shows the song's
      position as hh:mm:ss in 19 px `font.mono` (so the digits hold still as they
      count), in `orbitCounter`, TAPE TIME beside it; the
      **pilot lamp** (a red jewel left of it) is lit while the insert is on.
    - **The meters** (two VUs, L and R, 178 × 106 dials), modelled as the tape echo's
      is: a black bakelite bezel chamfered into its window, the cream dial sunk in it,
      the needle's pivot hood standing off the dial's foot with two warm bulbs under
      it whose light pools up the dial. They read the point METER chooses (INPUT,
      after the input knob; TAPE, the tape's playback; OUTPUT) as a VU does: each
      channel's rectified signal through a VU's movement (two equal lags, 99% of a
      step in 300 ms, in the processor), calibrated so a sine reads its rms, 0 VU at
      -18 dBFS, so music's peaks do not pin it. The scale is linear in voltage (-20 to
      +3, 0 about 71% across, red from 0); the needle passes under the hood and the
      glass; the card shows the newest reading, and the needles fall back the same way when
      the insert is switched off. Under them 0 VU = +6 dB
      OVER 185 nWb/m (the LEVEL chosen). Accessible description: the reading in VU.
    - **The panel** (hammertone, from 337 px): captions RECORD, MACHINE, WEAR and REPRO
      (14 px bold, `orbitPanelInk`) with a rule either side to the column's width.
      RECORD: INPUT (56 px skirt) over BIAS (38 px). MACHINE: rows of keys, SPEED,
      EQ, LEVEL and TAPE, named at the left (14 px, `orbitPanelInkSoft`). WEAR: HISS
      over FLUTTER. REPRO: OUTPUT over MIX, LINK beside MIX (OUT FOLLOWS IN under it,
      11 px). Knob names 15 px, values 13.5 px under them.
    - **Keys** (64 × 30, rendered keys: design-system.md "Hardware key"): a row is a
      radio group, the chosen key lit amber, the others ivory with a faint standby
      glow; LINK latches. Tooltips "Speed: 15 ips",
      "Meters read the tape", "Link: output follows input".
    - **Rendered controls** keep the system's behaviour, as on the tape echo, and
      every layer has a flat token fallback.
  - **Compact** (below 65%): no values, dial numbers, subtitle, model line or small
    print; names 1.45x and keys' labels 19 px; the knob in hand shows its value in
    its name's place.
  - **Header status:** "Tape 1.2 dB" while the tape compresses by 0.3 dB or more.
- **Corona** (a tube guitar amp and its cabinet, for guitar and bass): modelled on the
  1960s-70s American 2x12 combo with reverb and tremolo, with no trade dress of the
  original (our own name, plate, cloth and badge; "two-twelve reverb" at most).
  - The circuit's stages in order, each in volts: V1 (12AX7, 100k plate, bypassed
    1.5k cathode, 68k grid stopper), the treble / middle / bass stack (the exact
    passive network, 250k / 10k / 250k pots, 100k slope, 250 pF, 100 nF, 47 nF), the
    1M audio volume pot with the 120 pF bright cap across its top (BRIGHT: a treble
    lift that fades as the volume rises), the silverface's pull BOOST (+10 dB in
    front of V2), V2, the spring tank on a send from V2 (two springs, 33 and 41 ms,
    about 2.8 s, its driver clipping when hit hard), the mixer stage V3 (unbypassed),
    the opto tremolo (a lamp's half-wave pulse through a light cell's quick attack and
    slow release: SPEED 1.5-8 Hz, INTENSITY to a near chop), MASTER, the phase
    inverter, the push-pull power amp with negative feedback and SAG, the output
    transformer and the speaker's load (a bump at 95 Hz, a rise above 3 kHz). The
    triodes are curves solved from Koren's equations on each stage's load line, with
    grid conduction and the bias shift a loud note leaves.
  - Clean to about 5 with a strummed chord (a quarter volt at the jack, -12 dBFS at
    0 dB INPUT), breaking up from 6, crunchy at 10; BOOST breaks it up earlier; MASTER
    trades loudness for drive.
  - Everything nonlinear runs oversampled (QUALITY 2x, 4x default, 8x) through
    minimum-phase half-bands with antiderivative anti-aliasing. **No latency.**
  - INPUT trims the jack (-24 to +24 dB), GATE is a noise gate in front (Off at the
    bottom, to -20 dB, 6 dB of hysteresis and a 50 ms hold), OUTPUT is the level out.
  - The cabinet (plugins/Cabinet.h) is a model that builds a response: five
    cabinets (2x12 open, 1x12 open, 4x10 closed, and for bass 1x15 and 4x10), or a
    response file of the user's (IR, cut at 0.5 s), and two mics, each with a type
    (dynamic, ribbon, condenser), a place on the cone (cap bright, edge dark), a
    distance 1-100 cm (proximity effect up close, the other speakers and the floor
    coming in as it backs off), an angle to 45° (duller), level, pan and polarity.
    The mics' distances set a real time-of-flight offset, so blending them combs.
    ROOM adds early reflections. SPEAKER's LINE position takes the cabinet out.
  - **The face:** a hardware face built from rendered parts (assets/corona), drawn
    at 1160 × 600 and scaled to fit, with two pages (design-system.md "Two-page
    face"). Legends are Barlow Condensed in the `hardware` corona inks.
    - **Both pages:** the cabinet's oxblood tolex (`coronaTolex`) behind everything,
      and the utility strip along the foot (graphite, `coronaStrip`, inks
      `coronaStripInk`/`coronaStripInkSoft`): INPUT with a ten-LED meter of the
      trimmed input (lit from -48 dBFS in 6 dB steps, the top two from -3 dBFS
      warning in `ledHot`), GATE with a LED lit while it holds the signal back, SAG,
      OUTPUT (38 px knobs, names 14 px, values 13 px); the QUALITY keys (2×, 4×, 8×);
      the footswitch's REVERB and TREMOLO push-buttons, a LED beside each (the
      tremolo's pulses with the light cell); and the page keys, AMP and CABINET:
      two square keys (60 × 68, 8 apart, centred between the last hairline and
      the strip's screws), each an icon (a combo; a cabinet with two speakers) at
      half the space over its name, the current page's LED lit in its top left
      corner.
      Hairlines part the amp's controls, the footswitch and the pages.
    - **Keys** are bakelite (`keyTop` to `keyBottom`, the label `keyGlyph` 14 px
      bold) in a dark slot, a LED at the left lit while chosen or on, pressed in a
      pixel while down; hover 7%, pressed 10%, disabled 50%; a focus ring round the
      slot.
    - **The amp page:** the champagne plate (`coronaPanel`, inks `coronaInk` and
      `coronaInkSoft`) with captions over hairlines (PREAMP, REVERB, TREMOLO, POWER,
      14 px tracked), BRIGHT and BOOST as chrome levers (ON up, OFF down; a click
      anywhere on the lever flips it), eight
      62 px knobs (VOLUME, TREBLE, MIDDLE, BASS, REVERB, SPEED, INTENSITY, MASTER),
      each with ten ticks and 1-10 printed round it, its name (17 px bold) and value
      (15 px, "4.0") under it, and the pilot jewel (DRIVE) glowing with the power
      amp's drive: 30% at idle, full once it clips, dark while the card is off. The
      model line along the plate's foot (13 px). Below, the grille cloth framed in
      brass piping, with the brass badge, CORONA in 46 px tracked 0.24, centred on the brass by its letters' outlines, about twice as far from each rivet as the letters are apart.
    - **The cabinet page:** the baffle (left, 680 × 464) with the chosen cabinet's
      speakers laid out to scale (at most 1000 px a metre) and the two mics over the
      first speaker: each a marker, a 2 px ring in its colour (A `coronaMarkerA`,
      B `coronaMarkerB`) over a 16% fill and a dark halo, as large as the mic would
      look from the speaker (smaller the further it is, to 45% at a metre) and
      narrower the more it is angled, with its type's glyph inside (a dynamic's
      slim capsule on its clip, a ribbon's slotted box, a condenser's round head)
      and a label beside it ("A · 3.0 cm", a dot in its colour). A works up and to the right of the cap, B down and to the
      right, so they never cover each other. Drag a mic along its ray to place it;
      the wheel or Up and Down move it nearer and further, Left and Right along the
      cone (Shift: finer), double-click returns it. A mic that is off is a dashed
      ring at 35%. With a response file or the line out, the speakers dim, the mics go and a
      band says USING THE LOADED RESPONSE or CABINET OFF · LINE OUT.
    - The plate beside it: CABINET (a stepped knob, its six positions printed round
      it, the chosen one bold), SPEAKER (a lever: ON, LINE), ROOM; LOAD IR (a key,
      opening a file chooser; loading a file switches CABINET to IR) with the file's
      name or "No response file loaded" (and why, when it cannot be read); then MIC A
      and MIC B side by side, each with ON and Ø keys, the type keys (DYN, RIB, CON)
      and DISTANCE, ANGLE, LEVEL and PAN (40 px knobs). A mic's controls dim while
      it is off, and all of them with a response file or the line out.
    - **Compact** (below 65%, the Editor dock; design-system.md "Rendered parts"):
      no values, scale numbers, lever positions or model line; names 1.45x;
      CABINET shows the chosen cabinet; the knob in hand shows its value in its
      name's place; the mics' labels larger.
    - **The page** is not a parameter (nothing to undo). Each card remembers it per
      insert while the app runs; `--corona-page cabinet` opens new cards there.
    - Every rendered part has a flat fallback in the `hardware` tokens.
  - **Header status:** "Line out" while SPEAKER is at LINE.
- **The guitar pedals** (Gravity, Flare, Comet, Tide, Polaris; plugins/Pedals.h): five
  stompboxes voiced for guitar and bass, each with the few controls a pedal has.
  Every knob reads 0..10 but Level (dB), Time (ms) and the tuner's REF (Hz). None
  adds latency.
  - **Gravity** (compressor / sustainer): LEVEL (-18..+18 dB), SUSTAIN, ATTACK
    (0.5 to 40 ms: slow lets the pick through), BLEND (dry to all compressed). A
    feed-forward compressor whose threshold falls (-10 to -42 dBFS) and ratio rises
    (2 to 20:1) with SUSTAIN, 10 dB soft knee, make-up that leaves a firmly played
    note (-14 dBFS) where it was so everything quieter comes up toward it; a
    detector with a 90 Hz bass cut and a two-stage release (70 ms from a pick, 900
    ms from held compression); a 1:2 expander below -64 dBFS so the make-up does not
    lift the hiss; the OTA's soft saturation and a 7 kHz roll-off on the compressed
    signal.
  - **Flare** (distortion): LEVEL, DRIVE (6 to 66 dB, the op-amp stage's gain), TONE
    (a one-pole low-pass, 450 Hz to 14 kHz), and the CLIP toggle: SI (silicon
    diodes, tight), LED (more headroom, louder, more open), OPEN (no diodes: the
    op-amp on its rails, gritty, closest to a fuzz). The stage's gain is 15 dB lower
    below 160 Hz (tight lows under heavy drive) and its gain-bandwidth product
    rounds the highs off more the higher the gain; the clippers run at 4x. Level is
    normalised per clipper so switching CLIP does not jump in loudness.
  - **Comet** (analog delay): MIX, TIME (40 to 800 ms), REPEATS, and the MOD toggle
    (ON / OFF: the clock's slow wobble). A bucket-brigade line of 4096 stages: the
    repeats' bandwidth falls as the delay grows (9 kHz down to about 1 kHz), each
    pass is darkened, thinned below 90 Hz and softly limited, so repeats fade into
    a warm smear; at the top of REPEATS the loop runs away into a self-oscillation
    the limit holds. Turning TIME glides the clock (120 ms), bending the repeats'
    pitch. The dry signal passes untouched.
  - **Tide** (chorus): RATE (0.1 to 8 Hz), DEPTH (to 5.5 ms either side of a 7 ms
    delay), MIX, and the MODE toggle (CHO / VIB). A triangle-swept bucket-brigade
    chorus (7 kHz band limit, soft saturation) whose wet signal is high-passed at
    160 Hz, so on bass the low end stays solid; stereo out, the right side swept in
    the opposite phase (it sums to mono without cancelling). VIB crossfades the dry
    highs out with MIX, leaving pitch vibrato over the dry lows.
  - **Polaris** (chromatic tuner): REF (A4 at 430 to 450 Hz) and the footswitch,
    which mutes the output (the lamp, labelled MUTE, lit while it does). From a
    five-string bass's low B (31 Hz) to the top of a guitar's neck: the mono input
    decimated to about 8 kHz, YIN over the last 128 ms every 32 ms, its work spread
    over the blocks that follow; the period measured over as many cycles as fit, so
    a high note is as exact as a low one (within 3 cents in the tests). The display
    shows a chord or noise as no note (a clarity under 0.8).
  - **Face** (a hardware face built from rendered parts, assets/pedals): each pedal
    a die-cast enclosure seen from above on a pedalboard's dark carpet, drawn at 360
    x 600 and scaled to fit; its own finish (Gravity cobalt metal-flake, Flare
    signal orange sand-texture, Comet deep green wrinkle, Tide sea-glass gloss,
    Polaris graphite hammertone), worn as a pedal is (paint chipped along the
    rounded edge, boot scuffs round the footswitch). From the top: Corona's red
    jewel as the lamp (lit while the pedal is on; Polaris's while it mutes), the
    knobs (black, knurled, a white line; a scale of 11 ticks; the name under each
    and, at full size, its value under that), the toggle between the top two
    knobs with its positions printed beside it and its name under it, the pedal's
    name in large letters with what it is under it, Corona's chrome footswitch
    button, and NORTHLIGHT at the foot. Three-knob pedals set their knobs in a
    triangle (two at the top, the third below between them); Gravity's four are a
    square; Polaris has its display where the knobs would be, and REF under it.
  - **Footswitch:** a click, Space or Enter switches the pedal on or off (the
    insert's bypass, as the header's power does; one undo step). Polaris's mutes
    instead. The button shows pressed while held.
  - **Polaris's display:** black glass in a black bezel with a shade under its lip
    and a reflection; 21 LEDs from flat to sharp (one per 5 cents, the middle one
    green and lit within 3 cents, the reading's LED `tunerOff` with a soft halo,
    unlit ones `tunerSegmentOff`), the note under them in 76 px legend type with its
    sharp and octave beside it (green in tune), the cents at the bottom left and the
    reference at the right. A new note jumps; the same note's cents settle; after
    the note stops the reading holds 0.6 s, then fades over 0.4 s. Off (the pedal
    switched off, or no note yet) the LEDs are dark and the note a dash.
  - **Compact** (below 65%): values and the toggle's positions go, names grow; the
    knob being turned shows its value in its name's place.
  - Repaints: a turned or hovered knob repaints only its ticks, name and value
    (`RenderedCard::printArea`); the tuner repaints its display while it reads.
- **Stereo Widener** (width per band, a mono-safe spread, and the image on screen):
  the side (L-R) is split into Low, Mid and High by Linkwitz-Riley crossovers (Low
  split 40 Hz-1 kHz, default 250 Hz; High split 1-16 kHz, default 4 kHz) and each band
  scaled from 0 (mono) through 100% (as it came) to 200%; the mid goes through the
  matching allpasses, so at 100% the sound is unchanged and at any width the mono
  sum is the mid. Mono below (Off at 20 Hz, up to 500 Hz) takes the side out of the
  bass with an LR4 high-pass. Spread (0-100%) widens a mono source by adding a copy
  of the mid to the side only, so it cancels in mono: Diffuse through an allpass
  network (each frequency leans a different way: no echo, no regular comb), or Haas
  through a 1-30 ms delay (Time); both high-passed at 120 Hz. Rotation (±45°) turns
  the image keeping its width; Balance (±100%) turns one side down. The Guard narrows
  the image (quick in, slow out, to no less than 25% of its side) while its
  correlation would fall below Guard at (default 0). The level is compensated for the
  width (the power in over the power out, 300 ms, -6 to +3 dB). Output -24..+12 dB.
  No latency. Its editor:
  - **Vectorscope** (left, square, the display's height less the meter's): the
    output as a goniometer, mid up and side across; L and R on the diagonals in
    `gridBar` with a circle at full scale, M and S in `gridBeat`; L, M, R in
    `captionCaps` `textTertiary`. The newest 2048 samples as 1.6 px dots in the hue in
    four ages, 18% to 84% alpha, newest brightest; the scale follows the level (shrinks
    at once, grows over 600 ms) so a quiet source still fills it. A rotation other than
    0 draws a dashed `textSecondary` line from the centre to where a centred sound now
    sits. Silent for a quarter second: a 3 px dot at the centre and "No signal" in
    `caption` `textTertiary`.
  - **Correlation meter** (under the scope, 38 px, its own well): -1 to +1 on an 8 px
    track tinted by zone at 20%: below 0 `danger`, 0 to 0.3 `warning`, 0.3 up `success`;
    a `lineStrong` tick at 0; -1, 0, +1 under it in mono `caption` - 1. The reading
    (300 ms) is a bar from 0 in its zone's colour at 75% with a 2 px `textPrimary` end;
    the lowest of the last `motion.peakHoldMs` holds as a 1.5 px tick in its zone's
    colour; the input's correlation is a small `textSecondary` triangle under the
    track. The guard's threshold is a line with a notch above the track: solid in the
    hue while the guard is on, dashed `textTertiary` while off; drag it (steps of
    0.05, double-click resets; hover brightens it and shows a left-right cursor). The
    reading in `value` at the right in its zone's colour, "–" in silence.
  - **Width bands** (the rest of the display): 20 Hz-20 kHz across (grid at 50, 100,
    200, 500, 1k, 2k, 5k, 10k; decades `gridBar`, the rest `gridBeat`; labels in mono
    `caption` - 1 under the plot), width 0-200% up on a linear scale (100%
    `lineStrong`, 50 and 150% `gridBeat`; 0%, 100%, 200% labelled at the left). Above
    200%, a **headroom strip** across the top of the plot (14% of its height, 16 to
    26 px; `textPrimary` at 3.5%, a dashed `gridBar` edge at 200%, OVER in
    `captionCaps` `textTertiary` in the gutter) holds 200 to 800% on a log scale
    (400% half way) for what the output measures past what a band can be set to.
    The bands and their drags stay on the linear part (a drag into the strip sets
    200%). A 20 px row above the plot
    holds each band's name in `captionCaps` `textTertiary` and its width in `value`
    `textSecondary` ("Mono" at 0; only the value when the band is narrow, nothing when
    it is narrower still). Each band is a block in the hue at 14% up to its width
    with a 2 px line in the hue and a handle (the built-ins' 9 px disc) at its middle;
    hover or drag raises the fill to 22% and the value to `textPrimary`. Drag anywhere
    in a band for its width (it settles on 100% and on mono within 3%); double-click
    resets. The crossovers are 1 px `lineStrong` lines from the top of the well (they
    divide the caption row too), each with a grip at its foot: a 7 x 16 px upright
    pill in `textSecondary` with two `surface0` notches, so it reads as "drag sideways"
    beside the round "drag up and down" handles; hover or drag: line 1.5 px and grip
    `textPrimary` with a 18% halo, and the frequency in a `surface3` chip near the top.
    Mono below is a dashed line with its grip at the top; the region under it is
    `canvas` at 80% (no side there) with MONO in `captionCaps`; Off, the grip sits dim
    (`textTertiary`) at the left edge. Over everything, the width the output measures
    at each frequency (side over mid, from a 4096-point FFT of the two taps, 56 log
    bands each averaged with its neighbours, about 150 ms of averaging) as a 1.25 px
    `textPrimary` line at 60%, broken where a band is below -80 dBFS or 60 dB under the
    loudest; it shows what the settings actually did to the sound. It runs on up into
    the headroom strip; over each run of bands measuring past 200% whose widest
    reaches 210% (a run that only grazes the edge would flicker), an 8 x 6 px
    triangle in `warning` points up at the strip's top above the run's widest band,
    with that width beside it in mono `caption - 1` `warning` ("340%", ">800%" past
    the strip) where it does not collide with the last. While the guard
    narrows, the width it leaves is a dashed `warning` line in each band.
  - Controls, in four groups split by `lineSubtle` rules, every label on one baseline:
    Low, Mid, High ("Mono" at 0) and Mono below ("Off") as knobs; Mode as a segmented
    control (Diffuse | Haas, centred on the knobs' rings, captioned Mode), Spread and
    Time (Haas time: disabled, ring and label `textDisabled`, in Diffuse); Rotation
    ("+12°") and Balance ("C", "L 40", "R 40"); the Guard pill (in the hue when on,
    captioned "Mono safe") and Output.
  - Header status while the guard narrows: "Guard −35%" in `warning`.

Draggable points are one handle everywhere: a 9 px `textPrimary` disc with a 1.5 px
ring in the hue. A display's readout is `value` at its top right.

## 7.2b Built-in presets

Every built-in effect has presets: a factory list (8 to 14 for most, six levels for
Gain), the first of them its defaults ("Default", or Patina's "Gentle Age"), and the
user's own. The instruments keep their own preset bars in their faces (7.4b-7.4e).

- **Where.** A preset bar (design-system.md "Preset bar") in the effect card's
  header in the Editor panel, right of the name and before the menu, and in the
  effect's window, right of the title and before Bypass. It is 208 px wide, or what
  the header can spare while leaving the name 72 px; below 112 px it hides (the
  menu's items stay). A face may also have its own preset control (Patina's amber
  display); it uses the same list, so the two always show the same preset.
- **The name** is the preset whose values the effect has right now, or "Custom".
  Previous and next step through the whole list (factory, then the user's),
  wrapping; from Custom, next goes to the first and previous to the last.
- **The menu** (a click on the name): the factory presets, then the user's under
  a "Yours" section header in name order, the current one ticked; a separator;
  "Save preset...", "Delete "<name>"" (for a preset of the user's, dimmed
  otherwise) and "Show presets folder".
- **Save preset...** opens a sheet asking for a name (the current preset's, or "My
  preset"); a preset of the user's with the same name is replaced. Every bar for
  that effect updates at once.
- **Files.** One JSON file per preset in Documents/Northlight/Effect Presets/<effect
  name>: the name, the type and each parameter's value by its name. A file from an
  older version loads with the defaults for parameters it lacks; one for another
  effect is ignored. Delete moves the file to the recycle bin.
- **Undo.** Applying a preset sets every parameter it changes in one undo step
  ("Preset Vocal Plate").

## 7.3 Generic parameter editor

For GUI-less hosted plugins. A scrollable grid of parameter rows:

- Row 28 px: name `body` left (160 px), control centre (knob for continuous, segmented
  for choices under 5 options, combo above, checkbox for booleans), value `value`
  mono right (80 px, editable numeric field).
- Sections (from the plugin's group info or the built-in's layout) as uppercase
  `caption` headers with `space.lg` above.
- Drag on a knob begins a gesture (one undo step); the value field commits on Enter.
- Sidechain and latency shown in a footer line.

## 7.4 Plugin browser (side panel tab and window)

```
| 🔍 Search plugins                         [All ▾] [Scan] |
| ☆ Favourites (3)                                       |
|   ★ DC1A3                  Klanghelm        VST3       |
| Recent                                                 |
| Effects                                                |
|   Northlight EQ            Built-in                    |
|   DC1A3                    Klanghelm        VST3       |
| Instruments                                            |
|   …                                                    |
| Blocked (1)                                            |
|   ⚠ BadPlugin.vst3         crashed on scan   Retry     |
```

- Search field at the top with a format filter segmented control (All, VST3, CLAP,
  Built-in) and a category toggle (Effects, Instruments). Type-ahead filters as you
  type; Enter inserts the highlighted plugin on the selected track.
- Rows 28 px per the Table component; name `body`, manufacturer `small`
  `textSecondary`, format badge; star toggles favourite; drag a row to a chain or a
  track header to insert.
- Blocked section lists blocklisted files with the reason in `small` `warning` and a
  Retry ghost button per row; "Clear blocklist" in the overflow menu.
- Scan: a ghost button "Scan for changes" with a chevron menu for "Rescan all";
  while scanning, a progress bar replaces the button with "Scanning 12 of 48 ·
  Surge XT.vst3" and a Cancel; results appear as they arrive.
- The window version (from the app menu) is the same panel at 720×560.

## 7.4g Files (the browser's second tab)

```
| [ Plugins | Files ]                                    |
| 🔍 Search files                                        |
| ( Music › Drums                         ▾ )  [↑] [+]   |
|   📁 Loops                                             |
|   ▶ Kick 01.wav                       0.60 s   48 kHz  |
|   ♪ Hat Closed.wav                    0.25 s   48 kHz  |
| ( ■  ━━━━━━━━━━━━●━━━━━━━━━━              -6.2 dB )    |
```

- A segmented control at the top of the Browser panel switches Plugins and
  Files (remembered). Leaving Files stops a preview.
- Places: the saved project's folder, Music, and folders the user adds (the
  "+" button or the place menu's "Add a folder…"; "Remove this folder from the
  list" and "Show in Explorer" there too). The place button (ghost, chevron)
  shows the place and the path below it; ↑ (and Backspace) goes up, never above
  the place. Place, ↑ and + sit in one tray.
- Rows 28 px: folders first (folder icon, `textSecondary`), then audio files
  (wav, aif, aiff, flac, mp3, ogg) with the length in `value` `textSecondary` and
  the rate in `textTertiary` at the right. A click on a folder opens it.
- Search looks under the whole place for names holding every word typed; a
  result shows its folder in `small` `textTertiary` after the name. Escape in
  the field stops the preview and clears it.
- Click a file to audition it: its icon becomes a play mark in `accent`, the
  name turns `accent` and `accentSubtle` fills the row as it plays. Another
  click, another file, Escape or Stop ends it with a 5 ms fade; the preview
  plays over the mix at the footer's volume, outside the faders and meters.
- Double-click or Enter puts the file on a new track at the playhead; drag a row
  onto the arrange view for a new track where it is dropped (the drop preview a
  file from Explorer shows), or onto a Sampler's waveform to load it.
- The footer tray: Stop and the preview volume (a slider with its dB readout).
- Empty folder: "No audio files here"; no match: "No files match"; reading:
  "Reading…" in `small` `textTertiary`.

## 7.4a Instruments: channels, MIDI tracks and the rack

An instrument is not an effect, and its sound is not its MIDI's. An instrument
lives on an **instrument channel**: an audio channel whose **instrument slot**,
ahead of its inserts, takes only instruments (built-in, VST3 or CLAP), and whose
inserts, channel strip, sends, fader and automation process the instrument's main
output. **MIDI tracks** hold the notes (clips, an input, arm and monitoring, MIDI
out) and make no audio: each aims at an instrument channel on a MIDI channel
(1-16). Several MIDI tracks may play one instrument (a multitimbral plugin tells
them apart by channel); a MIDI track's volume scales its notes' velocities, and
its mute stops new notes. Each further stereo output of an instrument is an **output
track** with its own processing.

The "+" menu's **MIDI track** submenu offers: **With a new instrument** (any
built-in or scanned instrument), which makes both in one step, the MIDI track and,
just under it, its instrument channel (named for the instrument); **Playing** any
instrument channel already in the project (the MIDI track goes just above it); or
**Playing nothing yet**. Ctrl+Shift+T and the empty project's "Add MIDI track" make
a MIDI track with a new Aurora channel. The insert menus list effects only; an
instrument chosen from the browser goes to the selected
instrument channel, the one the selected MIDI track plays (or a new channel for
it, when it plays none), or a new MIDI track and instrument channel.

- **Arrange**: a MIDI track's header has no meter, solo or automation; in their
  place a chip says what it plays ("→ Drums: Pulsar  ch 2"); a click (or "Plays..."
  in its menu) offers every instrument channel, Nothing, the MIDI channel and New
  instrument. It has no Send to. An instrument channel's header has no input or
  arm; its lane reads "Instrument channel: Pulsar ← Drums, Fills". An output
  track's lane reads "Output 3/4 of Drums: Pulsar".
- **Editor panel sends**: a SENDS section ends the strip, after the inserts and
  the channel strip: a card per send with the bus's colour and name, a vertical
  fader (the mixer's law) with the level under it, Pre/Post and a remove button,
  then a "+ Send" slot listing the FX tracks and groups the channel can feed (not
  ones that would loop back into it; ones it already feeds are ticked). A MIDI
  track has no sends; its instrument channel's show instead.
- **Editor panel**: selecting a MIDI track shows the instrument channel it plays,
  titled "MIDI 1 → Drums: Pulsar"; a MIDI track that plays nothing shows a
  "+ Instrument" slot that makes a channel for it. An instrument channel shows an
  INSTRUMENT section first, before the strip and the inserts: the instrument's
  card (its own editor for a built-in, Open for a plugin with a GUI) with Replace
  and Remove in its menu and no wet/dry footer. The card is not reordered with the
  inserts.
- **Mixer**: strips for the channels that make audio (MIDI tracks have none). An
  instrument row under the module row (reserved on every strip when any has one,
  so the sections stay aligned): the slot with an INST / VST3 / CLAP badge, or
  "+ Instrument". Its menu: Open, Bypass, Outputs, Replace instrument, Remove
  instrument.
- **Instruments panel** (dock tab, F8): the rack. A unit per instrument channel,
  64 px, a `surface1` panel with the channel's hue down its left edge: power (in
  the hue), the instrument's name with a BUILT-IN / VST3 / CLAP badge, the channel
  and who plays it ("Drums: Pulsar ← Drums, Fills (ch 2)"), OUTPUTS as chips, Edit,
  and a menu (open, select track, replace, remove). Each output pair is a chip:
  "1/2 Drums: Pulsar" for the main pair (the channel itself) and each pair with an
  output track (click selects it), a dashed "+ Track for 3/4" for a pair without
  one (click makes it). "+ Instrument" at the end makes an instrument channel with
  the instrument chosen and a MIDI track to play it.

## 7.4b Aurora

The synth bundled with Northlight reads as its own product, so its editor is bolder
than the built-in effects' while keeping to the theme's palette (Sea grass, Lichen,
Overcast, Heather, Thrift, Gorse) and type:

- A night sky (the canvas darkening to the top) with twinkling stars and three
  aurora ribbons in Sea grass, Lichen and Heather drifting across the top; they move and
  brighten while Aurora sounds and hold still when it is silent or with reduce
  motion on.
- The wordmark AURORA, tracked wide with a Sea grass-to-Heather gradient, "POLYSYNTH ·
  NORTHLIGHT" beside it; the preset bar (previous, the name as a menu, next; "Custom"
  when the values match no preset); twelve voice lights that light as voices sound.
- Six glass panels (`surface1` at 72% over the sky, a rim and a top highlight),
  each a hue dot and caption, a display and its knobs in its hue: OSCILLATORS (both
  cycles, drag across to morph sine, triangle, saw, square), UNISON (the voices as
  a fan, spread by the detune and coloured by the pan; drag for the spread),
  FILTER (the response; drag for cutoff and resonance; the mode in a corner chip),
  ENVELOPES (the amp envelope filled and the filter envelope dashed; the corner chip
  picks which the handles edit), LFO (its shape, the live value as a marker; the
  shape in a corner chip) and OUTPUT (a scope of the output).
- Factory presets: Northern Lights, Solar Wind, Glacier Keys, Polar Bass, Ice
  Pluck, Starfield, Comet Lead, Aurora Drift.

## 7.4c Pulsar

The drum synth bundled with Northlight, Aurora's sibling for percussion: eight
synthesised pads on the General MIDI drum notes (kick C1, rim C#1, snare D1,
clap D#1, closed hat F#1, low tom A1, open hat A#1, high tom C2) and on C3 to G3
in that order, played out whatever the note's length; the closed hat chokes the
open one. Each pad: Tune, Decay, two shaping controls named for the sound (kick
Click and Punch, rim Ring and Click, snare Snappy and Crack, clap Bright and
Spread, hats Bright and Noise, toms Noise and Punch), Level, Pan and Output: the
main pair or an extra one (3/4 to 15/16). Pulsar has as many extra outputs as its
pads use, each named for the pads on it ("Kick", "Closed hat + Open hat"), and
each becomes a track of its own from the rack's chip or the mixer's Outputs menu,
named that way. The master: Drive, Velocity, Level (applied to every output).
Kits: Pulsar, Boom Room, Tight Electro, Dusty, Minimal Clicks, Industrial.

The voices follow the TR-808's circuits. The kick and toms are a sine that drops
gently onto its note (further and faster with Punch), soft-clipped, the kick with
its trigger's click through a low-pass as bright as Click. The snare is two shell
modes under high-passed noise (the wires, as loud as Snappy) with a band of noise
for Crack. The clap is band-passed noise retriggered: three short bursts and a
longer fourth, then a tail as long as the decay. The hats are six square
oscillators at the 808's ratios, mixed with noise, through a band-pass and a
high-pass that open with Bright. The rim is two short modes (about 1.7 kHz and
460 Hz) clipped together. Velocity drives the kick, toms and snare harder, as the
808's accent does. Filters are set in hertz and noise is scaled to the sample
rate, so a pad sounds the same at 44.1 kHz and at 192 kHz.

- Deep space (the canvas darkening around the star) with stars, and the pulsar in
  the top-left corner: a white core, an `Overcast` halo and two beams that turn slowly
  while it plays and flare on every hit, which also sends a ring in the pad's hue
  out across the sky behind the glass. Still when silent or with reduce motion on.
- The wordmark PULSAR tracked wide in an Overcast-to-Rowan gradient, "DRUM SYNTH ·
  NORTHLIGHT", and the kit bar (previous, the name as a menu, next; "Custom" off-kit).
- Eight pad tiles (4×2) of glass, each in its hue (Rowan, Gorse, Kelp, Machair,
  Sea grass, Lichen, Overcast, Loch): its short name, its note, and its sound, rendered and
  drawn as a waveform; a hit lights the tile from within and its rim. Clicking a
  tile selects it (a stronger rim).
- The selected pad's panel: its name and note, its output in a chip at the
  caption's right, its sound drawn large over a time grid (drag across for the
  decay, up and down for the tune, Shift for fine), and its six knobs. A tile whose
  pad has its own output shows the pair ("3/4") in a chip at its bottom right.
- "Outputs" beside the kit bar: all to the main output, one output per pad, or
  four groups (kick; snare, clap and rim; the hats; the toms).
- MASTER: Drive, Velocity, Level and a scope of the output.

## 7.4d Nebula

The pad synth bundled with Northlight, for slow, airy, ethereal sounds. Each note is a
cloud of partials (4 to 24), each a pair of sines detuned by the spread and
wandering on its own with the drift, panned alternately left and right (wider the
higher), shaped by Brightness (the spectrum's tilt), Odd/even (hollow to full),
Stretch (towards a bell) and a vowel Formant; a Halo of partials an octave up, a
Sub and Breath (noise around the note); a slow envelope; Motion, an LFO sweeping
the brightness, the formant and the partials' pans; a gentle Tone low-pass; and
Space, a large eight-line reverb. Eight voices. Presets: Nebula, Choir of Dust,
Glass Halo, Hollow Moon, Solar Breath, Deep Field, Ice Bells, Drifting Veil.

- Gas clouds in Heather, Thrift, Lichen and Loch drifting behind the panels and
  breathing with the sound, dust rising through them; still when silent or with
  reduce motion on. The wordmark NEBULA, light and tracked wide in a Thrift-to-Lichen
  gradient on a soft glow; "PAD SYNTH · NORTHLIGHT"; the preset bar; eight voice lights.
- SPECTRUM: the partials as columns of light on a log axis (55 Hz to 12 kHz, for
  A3 and the octave), the halo's in Sea grass, under the formant's cloud, swaying with
  the motion; drag across for the vowel, up and down for the brightness; the octave
  in a corner chip. Partials, Bright, Odd/even, Stretch, Vowel, Formant.
- CLOUD: where the partials sit in the stereo field (left to right, low to high),
  each a pair of dots as far apart as the spread, wandering with the drift. Drift,
  Spread, Width, Halo, Sub, Breath.
- ENVELOPE (draggable), MOTION (the wave with the live value), SPACE (the tail as
  long as the size and as bright as the mix; drag for both) with their knobs.

## 7.4e Starbit

The chiptune synth bundled with Northlight, after 8-bit consoles' sound chips. A
pulse (12.5, 25, 50 or 75% duty, with a duty sweep that cycles them), a 4-bit
stepped triangle and saw, LFSR noise (long, or short and metallic, pitched with
the note) and a 32-step 4-bit Wave you draw; volume in 16 steps; arpeggios in
tracker style (Chord cycles the held notes on one voice; Octave, Major, Minor,
Power, Maj7, Min7 cycle a shape over each note) at a rate in steps a second;
vibrato with a delay, a pitch sweep into each note and glide; Mono (newest note,
legato) or four voices; Bits, Rate (sample and hold) and a chip Echo. Presets:
Starbit, Hero Lead, Chord Arp, Major Sparkle, Tri Bass, Wave Pad, Laser Zap,
Noise Snare, Metal Hat, Crunchy Saw.

- Pixel art: a 5×7 pixel-font wordmark STARBIT, each letter a palette colour
  with a drop shadow, "CHIP SYNTH · NORTHLIGHT" in mono, square stars that blink in
  whole steps a few times a second while it plays, windows and screens with
  notched corners and 2 px borders, scanlines over the screens, four square voice
  lights. The knobs are the app's own.
- WAVE: the cycle as 32 columns of 16 cells; the shape, the duty (for the pulse)
  and the noise mode (for the noise) in chips; with Wave chosen, click and drag to
  draw the steps. Duty sweep, Octave, Coarse, Velocity.
- VOLUME: the envelope as 16-step bars. ARP: the steps as blocks as high as their
  offsets, the one playing lit; the mode and Mono/Poly in chips; a Sync chip under
  Rate makes each step a note value at the project tempo, on the song's grid
  while it plays (the Rate knob dims and shows the value). PITCH: the sweep
  and the vibrato over a note's first second. LO-FI: a sine as the bits and rate
  leave it.

## 7.4f Sampler

One sample across the keyboard: root note and key range, Tune and Fine, Glide;
Loop (forward, with a crossfade into the loop's end) or One-shot (to the end
point, note-offs ignored); Voices (1-32; the oldest fades out over the limit),
Velocity, Level; an ADSR on the amplitude; a resonant low-pass whose cutoff the
envelope moves by Env octaves. Hues: the sample `Lichen`, loop `Gorse`, keys
`Sea grass`, envelope `Overcast`, filter `Loch`.

- Header: "Sampler" in the hue over a small decaying wave; the file's name
  (`label`; `warning` when it did not load) with its rate, channels and length
  in `caption` `textTertiary`; the voices sounding with a light; a ghost
  **Load…** button (a file chooser).
- SAMPLE tray, full width: the waveform in a well, bright from the start to the
  end point and at the muted waveform alpha outside; the loop shaded in its hue
  at 12% with the crossfade as a gradient before its end; a 1 px line per voice
  sounding (`textPrimary`, fainter while it releases); the length that plays at
  the top in `value`. Start and end (S, E) are handles on the top edge, the
  loop's (L) on the bottom edge (shown while Loop is on); drag them, each stays
  on its side of its partner. Empty, the well reads "Drop an audio file here, or
  Load one"; a missing file reads its name and the reason in `warning`. Under it
  a keyboard of all 128 notes in a well: the range lit in the keys' hue, the
  root solid, C labels; click a key for the root, drag an end of the range.
- Trays below: PITCH (Root, Low, High as note names; Tune, Fine, Glide), PLAY
  (Loop fade, Voices, Velocity; Level and the Loop and One-shot pills), ENVELOPE
  (the curve in a well, times on a gentle log scale; Attack, Decay, Sustain,
  Release), FILTER (the response in a well, a fainter second curve where the
  envelope takes the cutoff; Cutoff, Reso, Env).
- Dropping an audio file on the waveform (from Explorer or the browser's Files
  tab) loads it as one undo step and resets the start, end and loop points to
  the whole file; the well outlines in `dropTarget` while a file is over it.
- From the arrange view: right-click an audio clip, **Open in Sampler**, for a
  new instrument channel and MIDI track playing that file, its start and end at
  the part the clip uses.

## 7.5 States and errors

- Plugin missing on this machine: the chain card shows the name in `textTertiary`
  with a `warning` icon and "Not installed"; state is preserved; the card can be
  removed or replaced.
- Plugin failed to load (layout refused, crash on instantiate): a toast with the
  reason and a "Blocklist" action; the card shows "Failed" in `danger`.
- Latency change: no UI beyond the card's latency label updating.

## Acceptance

- A chain of three cards matches sizes and states, including a bypassed card and
  the insertion line mid-drag.
- At 100% and 150%, a plugin window shows the 32 px bar and the plugin at its
  native size.
- The browser shows favourites, both formats and a blocked row.
- Scan progress and Cancel work when a plugin hangs during the scan.
