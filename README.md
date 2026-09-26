# Fan Fold

**Markdown sticky notes that live at the edge of your screen.** A standalone Qt 6 / KDE
application for Linux — plain `.md` files, a fan of coloured tabs, and a full WYSIWYG editor
one hover away.

<p align="center">
  <img src="docs/fan-fold/media/fan.gif" alt="The fan rests at the screen edge, spreads on hover, and opens a note" width="620">
</p>

## What's new in 0.2.0

[**Download 0.2.0-11 for Debian / Ubuntu (amd64)**](https://github.com/preyevates/fan-fold/releases/tag/v0.2.0-11)

- **A fan for each folder** — open a folder in the Library to put its notes on the fan.
  New notes go into that folder, and each folder remembers its tab order.
- **Whole-library search, one shortcut away** — press **Ctrl+F** or click the fan's search
  button. Match note titles, paths and contents across folders; results appear on the fan.
- **Scroll large fans** — use the mouse wheel or touchpad to reach more tabs without
  squeezing them together or letting the deck grow off-screen.
- **Typography per note** — choose a font family and size from the editor's **Aa** control,
  or return that note to your Settings defaults.
- **Colour a whole folder** — apply the current note's paper or ink colour to the open
  folder with a confirming second click. Each note remains individually adjustable.
- **Easier Library browsing** — folders start collapsed, expand and scope the fan in one
  click, and keep a folder heading visible while you scroll their notes.
- **Safer editing transitions** — note and folder changes, normal close and tray quit wait
  for pending edits; failed saves block the transition rather than discarding your work.

## The idea

Your notes rest as a slim fan of coloured tabs at the screen edge. Hover, and the fan spreads.
Click, and a note opens in a full Markdown editor. Leave, and everything folds back to the edge.

Nothing floats in your way. Nothing lives in a database. The notes are just files.

## Notes are files

Every note is an ordinary Markdown file in a folder you choose. No database, no lock-in —
your notes stay greppable, syncable, and readable by any editor you like. Archive moves a
file to `Archive/`; the Library brings it back.

Point Fan Fold at a folder you already have and it adopts what is there.

<p align="center">
  <img src="docs/fan-fold/media/note-open.png" alt="An open note: heading, rendered table and fenced code, all on the note's own paper colour" width="620">
</p>

## One folder, one fan

The Library is your whole notes tree; the fan is the folder you are working in. Click a
folder to expand it and show its notes on the fan. Open a note from the Library to switch
to its folder, or click the Library's root title to return to the top level.

The **+** button creates a note in the open folder. Subfolders keep their own fans rather
than spilling every note onto one edge. The open folder and each folder's drag-to-reorder
tab arrangement are remembered between sessions. Archived, trashed and pinned notes stay
off the ordinary folder fan; pinned notes live in their own windows.

The Library shows the current folder and its note count. Folders start collapsed when you
open it, and a sticky folder heading keeps your place while browsing a long list.

## Find a note across the library

Press **Ctrl+F** while Fan Fold is active, or click the **search button on the fan**, to
focus whole-library search. It matches titles, relative file paths and note contents,
case-insensitively, across folders — not just the folder currently open. Title matches
come first, then path matches, then content matches. Archived notes are excluded from
this search; browse the Library to restore them.

The matching notes become the fan while the query is active. Clear the search to return
to your previous folder without losing its saved tab order.

## Room for a large fan

When a folder or search has more notes than fit at the edge, the fan scrolls inside a
bounded area. Use your **mouse wheel or touchpad over the tabs or the spaces between
them**. Your chosen tab spacing stays intact, the **+** and search controls stay outside
the scrolling deck, and opening a note brings its tab into view.

## A real editor

Full WYSIWYG Markdown editing — headings, tables, fenced code with syntax highlighting, task
lists, links, images, even voice memos recorded straight into a note. Autosave runs on a
250 ms quiet period, with crash-recovery journaling and conflict detection if a file changes
underneath you.

The formatting toolbar is themed to the note's own paper colour like everything else:

<p align="center">
  <img src="docs/fan-fold/media/format-toolbar.png" alt="The formatting toolbar open above a note" width="620">
</p>

### Give each note its own typography

Use **Aa** in the editor's formatting toolbar to choose that note's font family and size.
The choice applies to the whole note, including its pinned window — not just selected
text. **Default** clears the override so the note follows Settings again. Typography is
stored as library metadata, leaving the Markdown file unchanged.

## Twenty-five palettes, contrast handled for you

Eight qualitative schemes from ColorBrewer 2.0, plus seventeen published editor and terminal
themes — Nord, Catppuccin, Solarized, Gruvbox, Dracula, Monokai, One Dark, Tokyo Night, Ayu,
Everforest, Rosé Pine, Material, Kanagawa, GitHub, Nightfox — used under their authors'
licences and credited in [`NOTICE`](NOTICE).

Ink auto-derives per swatch against a WCAG AAA (7:1) contrast target: light paper gets
near-black text, dark paper near-white, and the whole card — table borders, code surface,
toolbar glyphs, the tab in the fan — retints together.

Automatic ink handles contrast for you; you can also choose an explicit ink colour.

### Apply a colour to the open folder

In the paper or ink swatch panel, choose **Apply to folder**, then click again to confirm
**Apply to N notes?** This copies the current note's paper or ink setting to the live notes
directly in the open folder, including pinned notes. It does not recolour subfolders or
archived notes. This is a one-time application, not a rule for future notes: every note
can still be recoloured on its own.

<p align="center">
  <img src="docs/fan-fold/media/palettes.png" alt="All 25 palettes with their swatches" width="470">
</p>

## Settings

Notes folder, corner radius, default typography (font, size, line spacing, padding), button size, fan
tab geometry, auto-hide, and per-note icons — all live, all applied the moment you change them.

<p align="center">
  <img src="docs/fan-fold/media/settings.png" alt="The settings panel over a note" width="620">
</p>

## Everything else

- **Auto-hide** — the idle fan leaves a small visible handle at the screen edge.
  Hover the handle to reveal your notes, or show the fan from the tray.
- **Pinned notes** — float a note in its own always-on-top window, identical styling.
- **Library** — browse the folder tree including `Archive/`; restore with a confirming click.
- **Archive and Trash** — both arm on the first press and act on the second, so a stray
  click never files or deletes a note.
- **System tray** — new note, show the fan, quit; the fan is a dock window, the tray its handle.
- **Per-note icons** — give a note a tab icon from your desktop icon theme, or your own image.
- **Drag to reorder** — rearrange a folder's fan; each folder keeps its own order.
- **One fan per session** — launching Fan Fold again reveals the existing fan instead of
  opening a duplicate.
- **Everything themed** — tooltips, scrollbars, tables, panels: no unstyled platform chrome.
- **KDE-native** — Qt 6, KWin dock windows, Plasma virtual-desktop aware, Wayland first.

## Install

### Debian / Ubuntu (recommended)

Download the latest `.deb` from the
[Releases page](https://github.com/preyevates/fan-fold/releases) and install it with apt,
which pulls the Qt 6 and KDE runtime dependencies for you:

```sh
sudo apt install ./fanfold_*_amd64.deb
```

Then launch **Fan Fold** from your application menu, or run `fanfold`.

### Build from source

```sh
sudo apt install build-essential cmake \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtwebengine qml6-module-qtwebchannel \
  qml6-module-org-kde-kirigami qml6-module-org-kde-iconthemes \
  plasma-desktoptheme

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(bash tools/safe-build-jobs.sh)"
sudo cmake --install build
```

QtWebEngine and Kirigami are resolved as QML imports at runtime, so no development
packages are needed for them.

### First run

Fan Fold asks for a notes folder on first launch — pick a new one or an existing folder of
Markdown files. It writes nothing into that folder until you ask it to: `Assets/` appears the
first time you attach an image or icon, `Archive/` the first time you archive a note,
and `.fanfold-displaced/` when saving an existing note.

Settings live in `~/.config/FanFold/`, library state in `~/.local/share/FanFold/`. Your notes
never leave the folder you chose.

**Requirements:** KDE Plasma 6 on Wayland or X11, Qt 6.8 or newer. The published
0.2.0-11 amd64 package is built on Ubuntu 26.04 (resolute); older distributions may
not provide its required library versions. Use `apt` so incompatible dependencies
are reported rather than bypassed.

## Save and filesystem behaviour

External changes stop conflicting saves rather than silently replacing the editor buffer.

Normal close and tray quit wait for pending editor changes and refuse to close when the
final Markdown save fails. If recovery itself fails, keep the window open and copy your
edits; in-memory text is not a durable backup.

Atomic saves retain **one prior displaced inode per note**, in the library's hidden
`.fanfold-displaced/` directory on the same filesystem. Changed or ambiguous retained
files stop subsequent saves and are preserved for inspection. This is bounded protection,
not indefinite external-editor compatibility: writes through an old open file descriptor
after its one-generation retention window can no longer be detected or preserved. Keep
that directory with the library; do not treat it as a complete version history.

Asset and icon writes reject symlink substitution and avoid overwriting existing files.
They do not guarantee confinement against another authorized process concurrently moving
an already-open directory outside the library.

## How it is built

The document engine (`src/fanfold/`) owns storage, the note catalogue and the file lifecycle,
and knows nothing about the interface. The shell (`src/shell/`) is QML over that engine, with
thin adapters between them; the editor runs in a QtWebEngine view. The engine is covered by
its own test suite — `ctest` in the build directory runs it.

## Credits

The editor is [Vditor](https://github.com/Vanessa219/vditor) by B3log (MIT). Palette values
come from [ColorBrewer 2.0](https://colorbrewer2.org) (Apache-2.0) and from the published
themes listed in [`NOTICE`](NOTICE), each under its own licence. Pastels and Grayscale are
original to this project.

Every screenshot and GIF above is a real capture of the installed application on KDE Plasma 6
Wayland at production geometry — no mockups.

## Licence

MIT — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).
