## What this changes

<!-- What the change does and why. Link the issue it closes: "Closes #123". -->

## How it was checked

<!-- Tests added or run, and for a visible change, --capture screenshots at 100% and 200%. -->

## Checklist

See [CONTRIBUTING.md](../CONTRIBUTING.md) for each of these. Tick what applies, and
say which items don't.

- [ ] Builds with no warnings, and `ctest` passes
- [ ] Every changed `.cpp`/`.h` is formatted with `clang-format`
- [ ] New behaviour has tests named for the behaviour (a new command has apply, undo and redo tests)
- [ ] Code the audio callback or a render worker reaches follows [docs/rt-rules.md](../docs/rt-rules.md)
- [ ] A visible change meets the [design bar](../docs/design/principles.md#the-design-bar) and its screen spec is updated
- [ ] A change to painting stays within the [UI performance budgets](../docs/ui-performance.md)
- [ ] Docs and the **Unreleased** section of `CHANGELOG.md` are updated
- [ ] I agree that this contribution is licensed under the GNU AGPLv3
