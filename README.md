# Corpus

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B17-00599C.svg)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6.svg)
![Status](https://img.shields.io/badge/status-work%20in%20progress-yellow.svg)

A personal file catalog organized by **tags**, not folders. Instead of moving or copying files, Corpus
just keeps references to them and lets you filter your library by any combination of tags, the files
stay exactly where they always were.

The GUI is built **from scratch**, in raw Win32/GDI. No Qt, GTK, or Dear ImGui.

## Why

Folders force you to decide "where" a file lives. But many files are several things at once (an image that's both `wallpaper` and `icon`, a pdf that should be at `documents` but also in the `university` folder). Corpus models
that directly: a file can have N tags, a tag can point to N files, and the "current view" is just the
result of filtering by a set of tags.

## Searching

The GUI has a search bar that filters the current grid by filename or tag (case-insensitive, substring match).
Case is ignored for accented and non-Latin letters too (`AÇÃO` finds `ação`, `москва` finds `МОСКВА`), but
accents are not: `acao` doesn't find `ação`. Spaces at the start and end of the search are ignored.

It also supports a boolean tag query language inside curly braces — `AND` / `OR` / `NOT` / `-` / parentheses,
matching whole tag names, ignoring case (not substring). Writing tags next to each other means `AND`. `-` negates only at the
start of a word: inside a word it is part of the tag name, so `{sci-fi}` finds the tag `sci-fi` and
`{-sci-fi}` finds files without it.

A search can have several `{...}` groups, and a file must match every one of them. The free text outside the
groups still does the plain name/tag search above, and must match too. The groups only separate the text,
so `my {Wallpaper} photos` searches the text `my photos`:

```
movie{Wallpaper AND 4K}
{(Drama AND (Anime OR Manga)) -Shounen}
{sci-fi OR fantasy} 2024 {-watched}
```

A malformed expression in any `{}` group (unclosed or empty brace, dangling operator) marks the search bar
with a red border and keeps showing the last valid result instead of clearing the grid.

## Adding and opening files

Drag files or folders from Explorer onto the window to add them. Only their paths are stored: nothing is
copied or moved. Double-click an item, or select some and press Enter, to open them with their default
program (a folder opens in Explorer).

## Tagging and removing files

The strip at the bottom of the window edits the tags of the selected files. Click its field (or press
`Ctrl+T`), type one or more tags separated by spaces and press Enter to add them to every selected file.
While typing, the last word is completed in grey from the tags already in the library, the most used
first: `Tab` accepts the completion and `Up`/`Down` show the others. A word that only differs from an
existing tag in case is that tag, so `anime` adds `Anime`. Characters the search can't take in a tag name
(`{`, `}`, `(`, `)`, and `-` at the start) are refused, and the words `AND`, `OR` and `NOT` are skipped.

Next to the field, each tag of the selection is a chip: its `×` takes the tag off every selected file. A
dimmed chip is on only some of them, and clicking it adds it to the rest. When the chips don't fit, the
mouse wheel scrolls them sideways.

`Delete` removes the selected files from the library, after asking. The files themselves stay where they
are. A tag left on no file at all, by a removal or by taking it off, is deleted with it.

Each of these changes is saved in one transaction: if anything fails, none of it is kept.

## Storage

The library lives in a SQLite database. The GUI keeps it in `%LOCALAPPDATA%\Corpus\corpus.db`, so it
doesn't depend on where the `.exe` is; the CLI uses `libs/corpus.db` (not versioned). The database has to
be on a local disk: SQLite's file locks don't work over network shares, and from Windows that includes
`\\wsl.localhost\`, so a GUI built inside WSL can't keep its library in the repository. Everything is loaded into memory
at startup, so filtering and searching never touch the disk; every change (adding or removing a file,
tagging or untagging it, creating, renaming or deleting a tag) is written to the database the moment it happens, so nothing is
lost if the app crashes or is killed. If a write fails, the GUI says so and reloads the library from the
database, so the screen never shows a change that wasn't saved; files dropped together are saved all or
none.

```
files(id, path UNIQUE, identity)   -- identity: see "Moved and missing files"
tags(id, name UNIQUE)
file_tags(file_id, tag_id)   -- which file has which tag, deleted along with the file or the tag
```

The schema version is kept in `PRAGMA user_version` (currently `2`), and the database runs in WAL mode
with foreign keys on. A database from an older version is upgraded in place when it is opened, in one
transaction: version 1 only lacks the `identity` column. The first time a database is opened it is created and a JSON library can be
imported into it, in a single transaction; if that is interrupted the database stays empty and the import
runs again next time. `save_library` still writes the old JSON format, as a readable export.

A `library` with no database attached works exactly the same, in memory only.

## Moved and missing files

Besides its path, Corpus records each file's identity on the file system: on Windows the volume serial
number and the NTFS file ID, which stay the same when the file is renamed or moved on the same drive
(elsewhere the device and inode numbers). It is read when a file is added, and `library::record_identities`
reads it again for files that are in place but have none (stored before the database kept identities) or a
different one (a program that saves by renaming a temporary file over the original gives it a new one).

- `library::missing_files` lists the files that are no longer at their path. A file on a drive that isn't
  plugged in doesn't count as missing.
- `library::find_moved(folder, &error)` searches a folder and everything under it for missing files, by
  identity, and returns where each one is now. It changes nothing. A folder on a volume none of the missing
  files were on is only searched where such a volume is mounted inside it (only those mounts are opened, so
  an unrelated DVD drive or card reader isn't woken up), and `error` says why a search stopped early (the folder can't be read, say).
- `library::relink(file, new_path)` points a file to its new path, keeping its tags.

Nothing is relinked on its own: finding a file and relinking it are separate steps, so the app can show
what was found first. A file moved to another drive gets a new identity and has to be relinked by hand.
FAT and exFAT drives (most USB sticks and memory cards) have no stable file IDs, so files on them get no
identity and are always relinked by hand. An identity recorded on one of them by an older build is
forgotten once that same drive is back, so a different stick given the letter of an unplugged drive doesn't
erase the identities of the files that were on it.

The CLI has these as options 10 (list missing files, recording identities first), 11 (find moved files in a
folder and relink them) and 12 (relink one file by hand).

## Importing folders

A folder is imported in two steps, so a big one can be scanned without freezing the app:

- `scan_folder(folder, &cancel, progress)` lists every file in the folder and the folders under it, and reads
  their identities. It only touches the file system, so it can run on a background thread. `progress` is
  called for each file found, with the count so far and its path, and the scan stops as soon as `cancel`
  reads true. Links to folders aren't followed and a folder reached twice is listed once, so a link back up
  the tree can't loop. Subfolders that can't be read are skipped and counted; folders themselves aren't added.
- `library::add_files(scan.files)` then adds the files not in the library yet, on the thread that owns the
  library, in one transaction: if anything fails nothing is saved and the library stays as it was.

The CLI imports a folder with option 13, showing how many files it has found as it goes.

## Building

All commands below are run from the project root.

### Via CMake

```sh
cmake -B build
cmake --build build --target corpus-cli
```

`corpus-gui` (the actual app) only builds `if(WIN32)`. On Linux/WSL, cross-compile it with the
MinGW toolchain file:

```sh
cmake -B build-gui -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake
cmake --build build-gui --target corpus-gui
```

The sections below describe the same builds as raw `g++`/`windres` commands, useful if you don't
want to go through CMake.

### SQLite

SQLite is vendored as its amalgamation (`third_party/sqlite/sqlite3.c`), which is C, not C++, so it is
compiled on its own first, with `gcc` (it takes a while, but only has to be done once):

```sh
gcc -c -O2 -DSQLITE_DQS=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_FOREIGN_KEYS=1 third_party/sqlite/sqlite3.c -o third_party/sqlite/sqlite3.o
```

The builds below link that `sqlite3.o`.

### App (Windows, via MinGW)

The `.exe` uses its own icon (`corpus.ico`), embedded via a resource script. Before linking, compile
that resource with `windres`:

```sh
windres apps/gui/resource.rc -O coff -o apps/gui/resource.res
```

Then build:

```sh
g++ apps/gui/main.cpp apps/gui/window.cpp apps/gui/title_bar.cpp apps/gui/text_field.cpp apps/gui/file_grid.cpp apps/gui/tag_panel.cpp apps/gui/ui_util.cpp src/library.cpp src/database.cpp src/file.cpp src/file_identity.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp src/text.cpp third_party/sqlite/sqlite3.o apps/gui/resource.res -o apps/gui/corpus -ldwmapi -lgdi32 -lgdiplus -luser32 -lshell32 -lole32 -mwindows
```

It can be run from anywhere: the library is in `%LOCALAPPDATA%\Corpus\corpus.db`, created empty on the first run.

To try a build without touching your real catalog, point it at another database (created empty if it
doesn't exist; it must be on a local disk too):

```
corpus-gui.exe --library C:\Temp\scratch.db
```

`tools/smoke-test.ps1` does that automatically: it starts `build-gui\corpus-gui.exe` on a new database in
`%TEMP%` (deleted afterwards), fails if an error dialog appears instead of the window, saves a screenshot
of the window as `build-gui\smoke-test.png` and closes the app. It exits with 1 on a failure, and also
runs from WSL:

```sh
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w tools/smoke-test.ps1)"
```

`-Exe` and `-Screenshot` pick another `.exe` or PNG path, `-Timeout` how many seconds to wait for the window.

#### Missing DLL when running on another machine

If the `.exe` complains about a missing DLL on another machine (e.g. `libgcc_s_seh-1.dll not found`,
`libstdc++-6.dll not found`), it's because by default `g++` links the MinGW runtime (`libgcc`, `libstdc++`)
as a DLL, requiring MinGW to be installed (or the DLLs present) on the machine that **runs** the
program — not just the one that compiled it. To avoid this, embed the runtime inside the `.exe` itself:

```sh
g++ apps/gui/main.cpp apps/gui/window.cpp apps/gui/title_bar.cpp apps/gui/text_field.cpp apps/gui/file_grid.cpp apps/gui/tag_panel.cpp apps/gui/ui_util.cpp src/library.cpp src/database.cpp src/file.cpp src/file_identity.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp src/text.cpp third_party/sqlite/sqlite3.o apps/gui/resource.res -o apps/gui/corpus -ldwmapi -lgdi32 -lgdiplus -luser32 -lshell32 -lole32 -mwindows -static-libgcc -static-libstdc++ -static
```

| Flag | Effect |
|---|---|
| `-static-libgcc` | embeds the gcc runtime (`libgcc`) in the `.exe` instead of depending on `libgcc_s_seh-1.dll` |
| `-static-libstdc++` | embeds the std lib (`libstdc++`) in the `.exe` instead of depending on `libstdc++-6.dll` |
| `-static` | same for the remaining libs g++/MinGW would otherwise link dynamically (e.g. `libwinpthread`) |

The `.exe` gets bigger (the whole runtime goes with it), but runs standalone on any Windows without
needing to install anything. Without these flags, it works the same on a machine that already has MinGW installed.

### CLI (dev/testing only)

A minimal terminal harness over the same data model, used to test outside of Windows/MinGW — not the
actual product. It opens `libs/corpus.db` (or the path given as its first argument) and, if the database
is new, offers to import one of the JSON libraries in `libs/`.

```sh
g++ src/*.cpp apps/cli/main.cpp third_party/sqlite/sqlite3.o -o corpus -lpthread
```

`corpus-roundtrip-test` (CMake target) imports a library, edits it, reopens it and compares the result.
Given a count, it also times a generated library of that size:

```sh
cmake --build build --target corpus-roundtrip-test
./build/corpus-roundtrip-test /tmp 50000
```

`corpus-core-test` (CMake target) unit-tests the model in memory: tag query parsing and evaluation
(malformed queries included), filtering and search (brace groups, hyphenated tags, case beyond ASCII),
edits, and removing files and tags. It also checks that removals reach the database, which it keeps in the
folder given as its argument. Run as root on Linux, it mounts a tmpfs inside that folder to test finding
moved files on a volume mounted there; otherwise it says that check was skipped:

```sh
cmake --build build --target corpus-core-test
./build/corpus-core-test /tmp
```

## Structure

```
include/      core data model headers (library, file, tag, database, saving, tag_query, file_identity, text)
src/          core data model implementation — no entry point, just the library
apps/cli/     terminal harness, round-trip test and unit tests for the model (dev/testing only)
apps/gui/     the actual product: Win32/GDI GUI
libs/         JSON libraries (sample.json is a versioned example) and the CLI's corpus.db
third_party/  vendored SQLite amalgamation
cmake/        MinGW cross-compile toolchain file, for building apps/gui from Linux/WSL
tools/        GUI smoke test (PowerShell)
```

### How the GUI works

Windows talks to the app through **messages** (`WM_LBUTTONDOWN`, `WM_KEYDOWN`, `WM_PAINT`, ...).
`Window` receives all of them and forwards each one to the component it concerns. Every component
owns a rectangle of the window and follows the same shape: `SetBounds`, `Paint`, and `On<Event>` handlers.

```
main.cpp        opens the library database, creates the Window, runs the message loop
window.*        creates the window, splits it between the components, routes messages
title_bar.*     the round close/minimize/maximize buttons, dragging, maximize/restore
text_field.*    a single-line text field and its caret (the search bar, the tag field)
file_grid.*     the file grid: layout, scrolling, scrollbar, selection
tag_panel.*     the tags of the selected files: the tag field, its completion and the tag chips
theme.hpp       every color and shared size
ui_util.*       small helpers (DPI scaling, fonts, UTF-8 conversion, rounded shapes)
```

A typical round trip: the user scrolls → `WM_MOUSEWHEEL` → `Window` → `FileGrid::OnMouseWheel` updates
its scroll position and marks its own rectangle as dirty (`InvalidateRect`) → Windows sends `WM_PAINT`
→ `Window::OnPaint` asks every component to paint into an off-screen bitmap and copies it to the screen.
Nothing runs between messages, so an idle window uses no CPU.

## License

MIT — see [LICENSE](LICENSE).
