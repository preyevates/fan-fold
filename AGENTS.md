# AGENTS.md

Working notes for anyone — human or agent — changing Fan Fold. Read this before editing.

## What this is

A Qt 6 / QML desktop notes application for KDE Plasma 6. Notes are plain Markdown files in a
folder the user chooses; the application owns no database and never moves a file the user did
not ask it to move.

## Layout

```
src/fanfold/     document engine — storage, catalogue, file lifecycle. No UI.
src/shell/       the application: main.cpp, adapters, and qml/ (the interface).
tests/fanfold/   engine test suite, run by ctest.
third_party/     vendored Vditor editor (MIT). Treated as owned code, not a dependency.
packaging/       Debian packaging.
tools/           build and packaging scripts.
```

The engine knows nothing about QML. The shell talks to it through the adapters in
`src/shell/*adapter*`. Keep that boundary: engine changes must not reach for UI concepts,
and QML must not reimplement engine logic.

## Build and verify

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cd build && ctest
```

Before proposing a change as finished, all of these must be clean:

```sh
# 1. strictest compiler flags — currently zero warnings, keep it that way
cmake -S . -B build -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Wshadow -Wconversion'
cmake --build build -j"$(nproc)" 2>&1 | grep -E 'warning:|error:'

# 2. QML linting — currently zero errors
qmllint src/shell/qml/*.qml          # run from src/shell/qml so imports resolve

# 3. tests
cd build && ctest
```

`qmllint` reports `unqualified` warnings for context properties injected from C++. Those are
expected and are not errors; a *new* `Error:` line is a real defect.

## Conventions that matter

**Comments explain why, never what.** A comment earns its place by recording why a value is
that value, or why the obvious approach fails. Delete anything that restates the code.

**Never write project history into the source.** No release numbers, no dates, no narrative
about who asked for what. Record the constraint, not the incident that revealed it. If a
comment would only make sense to someone who was there, rewrite it.

**Platform knowledge is precious — keep it.** Several comments encode hard-won Wayland, Qt,
Plasma and Vditor behaviour. Those are the most valuable lines in the file. Do not "tidy"
them away.

**Q_INVOKABLE must be public.** moc silently ignores it in a private section and the QML call
fails at runtime with a TypeError rather than at build time.

**Measure a Text item's natural width with a separate invisible `TextMetrics`.** Reading
`contentWidth` on the item you are constraining is a feedback loop.

**Only replace the `<svg>` inside a Vditor toolbar button.** The surrounding element hides a
working `<input type="file">`; swapping `innerHTML` destroys the control.

**Vditor insert APIs are layered traps.** `insertValue` can silently no-op in IR mode, the
sanitiser discards raw HTML, and the editor caches its own selection range. Splice the source
buffer instead, with the caret snapshotted while the pane has focus.

**Never introduce a user-visible string naming another project.** Attribution belongs in
`NOTICE`, `LICENSE` and the Settings credits block — nowhere else.

## Things that are deliberate

- **The fan window is a `PlasmaCore.Dialog` with `type: Dock`.** A plain `Window` cannot
  position itself on Wayland; KWin places it wherever it likes. This is why the application
  requires Plasma.
- **The window is masked to the occupied strip of the lane.** Every pixel of a dock window
  eats input, and a full-height column swallows clicks meant for whatever is underneath.
  The mask is lifted when the fan is empty, because the welcome panel needs the whole area.
- **Archive and Trash arm on first press and act on the second.** They move the user's file;
  a single stray click must never file a note.
- **`Assets/` and `Archive/` are created on demand, not at startup.** An untouched notes
  folder stays untouched.
- **Settings migrate from the pre-1.0 directory on launch.** The migration never overwrites
  newer state and never deletes a directory that still has contents.

## Testing UI changes

The engine has unit tests. The interface does not, and cannot usefully have them — it is a
dock window on a compositor. Verify UI work by running the application against a scratch
notes folder:

```sh
./build/src/shell/fanfold --root /tmp/scratch-notes --state /tmp/scratch-state
```

Never point a test run at a real notes folder.
