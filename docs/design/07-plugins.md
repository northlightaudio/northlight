# 7. Inserts, plugin windows, browser

## 7.1 Insert chain panel (Editor tab, dock)

Selecting a track or bus and opening the Editor tab shows its chain as a horizontal
strip of device cards, Bitwig-style, so the whole signal path reads left to right.

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
- Body: for built-ins, the generic editor's knobs laid out in a grid of 28 px knobs
  with `caption` labels, 4 per row, section headers for multi-section plugins; for
  hosted plugins with a GUI, a thumbnail placeholder with an "Open" primary button
  (the native editor opens in a plugin window) and the three most important
  parameters as knobs if the plugin exposes them; for hosted plugins without a GUI,
  the generic editor in the card.
- Footer 24 px: wet/dry mini slider with "Mix 100%".
- Bypassed card: body at 50% alpha, power icon `textTertiary`, header name struck.
- Drag the header to reorder; a 2 px `accent` insertion line between cards. Drag a
  plugin from the browser onto the strip to insert at the line.
- The "+" card is a ghost card that opens the plugin menu (recent, built-ins,
  favourites, then by manufacturer).
- Sidechain: a small "key: 02 Hats" chip under the header when set, `accentSubtle`
  fill, click to change.

## 7.2 Plugin window

Native window with a 32 px Aural bar above the plugin's own editor:

```
| ⏻  DC1A3 · 01 Kick     VST3   mix ──o── 100%   presets ▾   ⟲   📌   ⤢ |
```

- Bar: `surface0`; power toggle, "Plugin · Track" title in `label` (track name in
  `textSecondary`), format badge, wet/dry slider (80 px) with value, presets combo
  (factory programs and user presets, with Save… at the bottom), an "A/B" toggle
  (**Decision**: deferred, keep the slot), pin (always on top) toggle, and a
  generic-editor toggle for plugins that have both.
- The plugin's editor sits below at its native size; the window follows plugin
  resize requests; DPI scaling passed through.
- Position and size remembered per plugin type; pin state remembered.
- Closing the window never unloads the plugin; the card in the chain shows "open" as
  an `accent` dot on its header while the window is open.

## 7.3 Generic parameter editor

For built-ins and GUI-less hosted plugins. A scrollable grid of parameter rows:

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
|   Aural EQ                 Built-in                    |
|   DC1A3                    Klanghelm        VST3       |
|   Aural Test Gain          Leafshade        CLAP       |
| Instruments                                            |
|   …                                                    |
| Blocked (1)                                             |
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

## 7.5 States and errors

- Plugin missing on this machine: the chain card shows the name in `textTertiary`
  with a `warning` icon and "Not installed"; state is preserved; the card can be
  removed or replaced.
- Plugin failed to load (layout refused, crash on instantiate): a toast with the
  reason and a "Blocklist" action; the card shows "Failed" in `danger`.
- Latency change (plugin reports new latency): no UI beyond the card's latency label
  updating; the graph re-solves silently.

## Acceptance

- Chain strip screenshot with three cards matches sizes and states, including a
  bypassed card and a drag insertion line mid-drag.
- Plugin window bar screenshot at 100% and 150% with DC1A3 shows the 32 px bar and
  the plugin at its native size.
- Browser screenshot with favourites, both formats and a blocked row.
- Scan progress and cancel verified with the stub scanner tests' hanging plugin.
