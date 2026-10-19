# Architecture notes

Living document. The intended architecture is in [PLAN.md](PLAN.md), section 3; this
file records what is actually built and the decisions made along the way.

## Current state (Phase 0)

```
aural.exe
  src/app         JUCEApplication + DocumentWindow with a placeholder component
  src/engine/rt   Real-time guard: thread-local RT flag, violation handler,
                  debug operator new/delete hook

aural_unit_tests.exe
  tests/unit/rt   Catch2 tests for the guard
```

Build targets:

| Target | Kind | Depends on |
|---|---|---|
| `aural_options` | INTERFACE | Compiler flags and defines shared by everything |
| `aural_rt` | STATIC | `aural_options` |
| `aural_rt_checks` | OBJECT | `aural_rt`; holds the global `operator new` override so it is linked into executables rather than discarded from a static lib |
| `aural` | GUI app | JUCE modules, `aural_rt`, `aural_rt_checks` |
| `aural_unit_tests` | console app | Catch2, `aural_rt`, `aural_rt_checks` |

## Decisions log

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | C++20 + JUCE 8 over Rust | VST3 hosting and plugin-window embedding are mature in JUCE and immature in Rust; the hybrid (Rust engine, JUCE shell) stays an option after Phase 2. |
| 2026-10-19 | Own engine rather than Tracktion Engine | Licensing freedom and control of the threading/data model. Re-evaluate at the end of Phase 2 if the milestone slips. |
| 2026-10-19 | JUCE and Catch2 via `FetchContent`, no vcpkg yet | Nothing yet needs a package with a real build step. vcpkg (bundled with VS) comes in when spdlog or a resampler lands. |
| 2026-10-19 | RT checks via global `operator new` override in an OBJECT library | A static library's override can be discarded by the linker; OBJECT library objects are always linked. |
| 2026-10-19 | `JUCE_MODAL_LOOPS_PERMITTED=0` | Forces async dialogs from day one so the message thread never re-enters during a modal loop, which is a classic source of host/plugin deadlocks. |
