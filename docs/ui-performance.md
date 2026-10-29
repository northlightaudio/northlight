# UI performance

The interface must stay smooth while a project plays: a hover, a knob turned, a
meter moving must never cost a frame. The budgets users feel are in
[design/accessibility-performance.md](design/accessibility-performance.md) (11.6);
this page is how the code is held to them. Three layers, from strict and
deterministic to realistic and local.

| Layer | What | Runs | Fails when |
|---|---|---|---|
| 1. Work budgets | What each card repaints, and the words its face sets, per interaction | `ctest`, every build, CI | a repaint or a word count is over its budget |
| 2. Paint-time budgets | How long each card and the mixer take to paint at 200% | `ctest -L perf`, Release builds, locally | a median paint time is over its budget |
| 3. Frame benchmarks | Frame intervals and paint times in the running app | `scripts/perf.ps1`, locally | a median or p95/p99 is over this machine's baseline |

## 1. Work budgets (`tests/unit_juce/UiWorkBudgetTests.cpp`)

Counted, not timed, so the result is the same on every machine and CI can gate on
it. Each built-in card (every effect, every instrument) is built with its
processor live and a tone playing, and driven the way a user would through the
components' own handlers:

- **hover**: the pointer enters and leaves every knob;
- **compact hover**: the same on a rendered face at its smallest (the Editor dock);
- **turn**: every knob turned a tenth of its range and back;
- **frame**: ten display frames of playing.

A recorder attached as the card's cached image (`tests/unit_juce/UiPerf.h`) sees
every area the card or anything in it asks to repaint, with no window; the card is
then painted clipped to that area while `ui::paintstats` (`src/ui/PaintStats.h`)
counts the words a face sets (`hardware::drawCrispText`, which measures each line's
glyph outlines and is the costly part of a face's paint) and its cached-art
rebuilds.

| Budget | Limit | Why there |
|---|---|---|
| Hover repaints | ≤ 6% of the card | the knob alone (the largest is 4.1%) |
| Words set by a hover | ≤ 30 | a big knob's printed scale and name (Corona: 21) |
| Compact hover repaints (rendered faces) | ≤ 12% | the knob and where its value replaces its name (10.2%) |
| Turn repaints | ≤ 25% on a rendered face, ≤ 65% with a display | round the knob, plus a display that draws the setting (EQ curve, widener) |
| Words set by a turn | ≤ 64 | Corona's knob row (53) |
| Frame repaints | ≤ 30% on a rendered face, ≤ 75% with a display | the live parts only |
| Words set by a frame | ≤ 24 | the labels beside a meter (19) |
| Whole-card repaints, art rebuilds | none, in any interaction, an undo or preset on a rendered face included | |

The timeline views have budgets of their own
(`tests/unit_juce/TimelineWorkBudgetTests.cpp`): a playhead frame in the arrange
view and the piano roll repaints under 2% of the view, and painting it draws at most
the clips (one per track per column) or the few notes under its columns, counted
with `paintstats` (`clipsDrawn`, `notesDrawn`, `waveColumns`). The views paint only
what their clip bounds cover: rows, clips, notes, waveform columns, grid lines and
labels outside it are skipped, and nothing is copied for a clip that is not being
dragged.

`northlight_juce_tests "[.measure]"` prints every card's numbers. A budget is
changed deliberately: a new card or feature that genuinely needs more says why in
the same commit, here and in the test. A budget is not raised to make a slow
change pass.

## 2. Paint-time budgets (`tests/unit_juce/UiPaintTimeTests.cpp`)

The median of repeated paints at 200% (the software renderer, so slower than the
app's Direct2D), whole and for what a hover and a live frame repaint, for every
card, a 12-track mixer, the arrange view (16 tracks of 20 clips) and the piano roll
(400 notes), with what a playhead frame repaints. Times depend on the machine and the build, so the test
is registered only in a Release build (`ui_paint_time`, label `perf`) and never in
CI:

```powershell
.\scripts\build.ps1 -Preset windows-release -Test -Label perf
```

| Budget | Limit | Slowest now |
|---|---|---|
| Full paint | ≤ 12 ms | the arrange view with 16 tracks of 20 clips 10.6 ms, the mixer 7.5 ms |
| A hover's repaint | ≤ 1.5 ms | Amp 0.5 ms |
| A frame's repaint | ≤ 6 ms | Patina 3.5 ms (six live windows) |
| A playhead frame (arrange view, piano roll) | ≤ 1 ms | 0.01 ms |

## 3. Frame benchmarks (`scripts/perf.ps1`)

The app's own benchmark flags (`src/app/LaunchRun.cpp`), each in a window (frames must
reach a screen; not while the machine is in use for a game): `--benchmark-ui`
(playing and scrolling the demo song: frame intervals, arrange and mixer paint
times, audio load), `--benchmark-panels` (sliding the side panel),
`--benchmark-strip-drag` (dragging a strip EQ band) and `--benchmark-hover` (the
pointer moving knob to knob over Patina's, Corona's, the tape echo's and a guitar pedal's (Flare's) windows,
timing every paint of the window). The script compares the medians and p95/p99s
with `perf/baselines/<computer>.<preset>.json`; one over its baseline by more than
30% plus 0.5 ms fails. Max values are shown, not judged. The benchmarks use the demo
song, a test project the app writes with `--demo-song`; `-Project <file>` opens another
project, and a baseline is only comparable with one taken on the same project.

```powershell
.\scripts\build.ps1 -Preset ci
.\scripts\perf.ps1            # compare with this machine's baseline
.\scripts\perf.ps1 -Update    # a new baseline after a deliberate change; commit it
```

## When a change touches painting

- Layer 1 runs with every `ctest`. A new card or editor is added to
  `uiperf::builtInTypes()` so it is measured.
- Run layer 2 (`-Label perf` in a Release build) for any change to a paint routine,
  a face, a card or the mixer.
- Run layer 3 before merging a change to how the UI repaints (timers, repaint
  regions, a face's layers) and update the baseline if it moved for a good reason.
- A refactor that should not change the look is checked pixel for pixel:
  `northlight_juce_tests "[.card-snapshots]"` saves every card (the built-ins and
  the strip modules, played into the same way every run) at 100% and 200%, a
  rendered face at its smallest too, as PNGs in the folder `NORTHLIGHT_SNAPSHOT_DIR`
  names. Save them before and after and compare the two folders.

## How a face keeps its repaints small

- `hardware::drawCrispText` sets nothing outside the area being repainted, and a
  face skips a whole control's print outside it (Patina's `reach()`).
- Hovering repaints the knob only; the face repaints for the control in hand only
  when compact, and then only round that control (`RenderedFace::setActiveControl`).
- A turned knob repaints round itself (`RenderedCard::controlMoved`).
- A parameter change from the model (undo, automation, a preset) goes through
  `RenderedCard::repaintChanged()`: round each knob whose value moved, round the
  knobs `alsoChanges()` names (a unit that follows a mode), and the whole face only
  for a parameter `printsWidely()` says reaches further; a knob a card drives
  itself registers with `showOnKnob()`.
- Live readouts are components that repaint their own bounds; anything that
  changes the art invalidates the face's cache instead of repainting it whole.
