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

### App (Windows, via MinGW)

The `.exe` uses its own icon (`corpus.ico`), embedded via a resource script. Before linking, compile
that resource with `windres`:

```sh
windres apps/gui/resource.rc -O coff -o apps/gui/resource.res
```

Then build:

```sh
g++ apps/gui/test.cpp apps/gui/window.cpp apps/gui/search_bar.cpp apps/gui/ui_font.cpp src/library.cpp src/file.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp apps/gui/resource.res -o apps/gui/corpus -ldwmapi -lgdi32 -lgdiplus -luser32
```

Run it from the project root too (it loads `libs/sample.json` relative to the working directory).

#### Missing DLL when running on another machine

If the `.exe` complains about a missing DLL on another machine (e.g. `libgcc_s_seh-1.dll not found`,
`libstdc++-6.dll not found`), it's because by default `g++` links the MinGW runtime (`libgcc`, `libstdc++`)
as a DLL, requiring MinGW to be installed (or the DLLs present) on the machine that **runs** the
program — not just the one that compiled it. To avoid this, embed the runtime inside the `.exe` itself:

```sh
g++ apps/gui/test.cpp apps/gui/window.cpp apps/gui/search_bar.cpp apps/gui/ui_font.cpp src/library.cpp src/file.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp apps/gui/resource.res -o apps/gui/corpus -ldwmapi -lgdi32 -lgdiplus -luser32 -static-libgcc -static-libstdc++ -static
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
actual product.

```sh
g++ src/*.cpp apps/cli/main.cpp -o corpus
```

## Structure

```
include/    core data model headers (library, file, tag, saving, tag_query)
src/        core data model implementation — no entry point, just the library
apps/cli/   terminal harness for testing the model (dev/testing only)
apps/gui/   the actual product: Win32/GDI GUI
libs/       saved libraries (sample.json is a versioned example)
cmake/      MinGW cross-compile toolchain file, for building apps/gui from Linux/WSL
```

## License

MIT — see [LICENSE](LICENSE).
