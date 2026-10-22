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

It also supports a boolean tag query language inside curly braces — `AND` / `OR` / `NOT` / `-` / parentheses,
matching tags exactly (not substring). Free text outside the braces still does the plain name/tag search above,
and both are combined:

```
movie{Wallpaper AND 4K}
(Drama AND (Anime OR Manga)) - Shounen
```

A malformed expression inside `{}` (unclosed brace, dangling operator) marks the search bar with a red
border and keeps showing the last valid result instead of clearing the grid.

## Adding and opening files

Drag files or folders from Explorer onto the window to add them. Only their paths are stored: nothing is
copied or moved. Double-click an item, or select some and press Enter, to open them with their default
program (a folder opens in Explorer).

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
files(id, path UNIQUE)
tags(id, name UNIQUE)
file_tags(file_id, tag_id)   -- which file has which tag, deleted along with the file or the tag
```

The schema version is kept in `PRAGMA user_version` (currently `1`), and the database runs in WAL mode
with foreign keys on. The first time a database is opened it is created and a JSON library can be
imported into it, in a single transaction; if that is interrupted the database stays empty and the import
runs again next time. `save_library` still writes the old JSON format, as a readable export.

A `library` with no database attached works exactly the same, in memory only.

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
g++ apps/gui/main.cpp apps/gui/window.cpp apps/gui/title_bar.cpp apps/gui/search_bar.cpp apps/gui/file_grid.cpp apps/gui/ui_util.cpp src/library.cpp src/database.cpp src/file.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp third_party/sqlite/sqlite3.o apps/gui/resource.res -o apps/gui/corpus -ldwmapi -lgdi32 -lgdiplus -luser32 -lshell32 -lole32 -mwindows
```

It can be run from anywhere: the library is in `%LOCALAPPDATA%\Corpus\corpus.db`, created empty on the first run.

#### Missing DLL when running on another machine

If the `.exe` complains about a missing DLL on another machine (e.g. `libgcc_s_seh-1.dll not found`,
`libstdc++-6.dll not found`), it's because by default `g++` links the MinGW runtime (`libgcc`, `libstdc++`)
as a DLL, requiring MinGW to be installed (or the DLLs present) on the machine that **runs** the
program — not just the one that compiled it. To avoid this, embed the runtime inside the `.exe` itself:

```sh
g++ apps/gui/main.cpp apps/gui/window.cpp apps/gui/title_bar.cpp apps/gui/search_bar.cpp apps/gui/file_grid.cpp apps/gui/ui_util.cpp src/library.cpp src/database.cpp src/file.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp third_party/sqlite/sqlite3.o apps/gui/resource.res -o apps/gui/corpus -ldwmapi -lgdi32 -lgdiplus -luser32 -lshell32 -lole32 -mwindows -static-libgcc -static-libstdc++ -static
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

## Structure

```
include/      core data model headers (library, file, tag, database, saving, tag_query)
src/          core data model implementation — no entry point, just the library
apps/cli/     terminal harness and round-trip test for the model (dev/testing only)
apps/gui/     the actual product: Win32/GDI GUI
libs/         JSON libraries (sample.json is a versioned example) and the CLI's corpus.db
third_party/  vendored SQLite amalgamation
cmake/        MinGW cross-compile toolchain file, for building apps/gui from Linux/WSL
```

### How the GUI works

Windows talks to the app through **messages** (`WM_LBUTTONDOWN`, `WM_KEYDOWN`, `WM_PAINT`, ...).
`Window` receives all of them and forwards each one to the component it concerns. Every component
owns a rectangle of the window and follows the same shape: `SetBounds`, `Paint`, and `On<Event>` handlers.

```
main.cpp        opens the library database, creates the Window, runs the message loop
window.*        creates the window, splits it between the components, routes messages
title_bar.*     the round close/minimize/maximize buttons, dragging, maximize/restore
search_bar.*    the query text field and its caret
file_grid.*     the file grid: layout, scrolling, scrollbar, selection
theme.hpp       every color and shared size
ui_util.*       small helpers (DPI scaling, fonts, UTF-8 conversion, rounded shapes)
```

A typical round trip: the user scrolls → `WM_MOUSEWHEEL` → `Window` → `FileGrid::OnMouseWheel` updates
its scroll position and marks its own rectangle as dirty (`InvalidateRect`) → Windows sends `WM_PAINT`
→ `Window::OnPaint` asks every component to paint into an off-screen bitmap and copies it to the screen.
Nothing runs between messages, so an idle window uses no CPU.

## License

MIT — see [LICENSE](LICENSE).
