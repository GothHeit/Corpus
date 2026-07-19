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

### CLI

```sh
g++ src/*.cpp -o corpus
```

### GUI (Windows, via MinGW)

See [`visual/compilevisual.md`](visual/compilevisual.md) — includes the build command and how to produce
a statically linked `.exe` (no MinGW DLL dependency on the machine that runs it).

## Structure

```
include/    data model headers (library, file, tag, saving)
src/        data model implementation + CLI (main.cpp)
visual/     Win32/GDI GUI
libs/       saved libraries (sample.json is a versioned example)
```

## License

MIT — see [LICENSE](LICENSE).
