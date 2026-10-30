# Corpus

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-0078D4?style=flat-square)](#download)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/license-MIT-yellow?style=flat-square)](LICENSE)

A catalog for my personal files. I wanted something to tag the files I already have and find them again without digging through folders, so I built it.

Drop in a folder, tag what's inside, then search by name and tags together. Pictures, videos, music and books open inside the app. Everything else opens in its default program. Corpus only remembers where your files are: it never copies or changes them, unless you rename a file or send it to the Recycle Bin from inside the app.

It's a native Windows app in C++ (Win32 and Direct2D, no UI framework), one `.exe`, library stored in SQLite.

![Filtering by tags, then opening a picture, a video and a book](docs/screenshots/corpus-demo.gif)

| | |
| --- | --- |
| ![The file grid with the tag sidebar](docs/screenshots/01-grid.jpg) | ![A video playing in the viewer](docs/screenshots/04-video.jpg) |
| ![A picture in the viewer with its tags](docs/screenshots/03-image.jpg) | ![An EPUB in the reader](docs/screenshots/05-book.png) |

## What it does

- Imports whole folders (drag them in, or Ctrl+O). Each folder can become a tag, shown as a tree in the sidebar (a folder inside another is `Music/Album`). Imported folders are watched, so new files show up on their own.
- Tags many files at once with Ctrl+T, or by dragging files onto a tag in the sidebar. Tags can be renamed, merged and deleted, a whole branch at a time.
- Searches by name and tag in one box, see below.
- Views pictures (anything Windows can decode, plus PSD), plays videos and music with libmpv (FLAC, MP3, WAV, M4A, OGG, OPUS, AAC, with their embedded covers), and reads PDF, CBZ, EPUB, text, Markdown and HTML. Books reopen where you stopped.
- Undo and redo for tag edits and removing files from the library (Ctrl+Z, Ctrl+Y).
- Drag files out of the grid into Explorer or a chat, or copy them with Ctrl+C.
- Finds files again after you move them outside Corpus (the Missing view).
- Backs up the library on start, and can export and restore from Settings.

## Download

Grab `Corpus.exe` from the [releases page](https://github.com/GothHeit/Corpus/releases). It's a single file: put it anywhere and run it. The library lives in `%LOCALAPPDATA%\Corpus`, and `Corpus.exe --library <file.db>` uses another one. Keep the library on a local drive; SQLite doesn't work well on network shares.

## Build

Corpus cross-compiles to Windows with MinGW-w64, from Linux or WSL:

```bash
sudo apt install cmake g++ mingw-w64
git clone https://github.com/GothHeit/Corpus.git
cd Corpus
cmake -B build-gui -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake
cmake --build build-gui
```

That gives you `build-gui/Corpus.exe`.

Video playback needs `libmpv-2.dll`. To embed it, run this before configuring (or configure again afterwards):

```bash
powershell.exe -NoProfile -File "$(wslpath -w tools/get-libmpv.ps1)"
```

Without it everything else works, and videos and music open in their default program.

The core library and the CLI build on Linux with plain `cmake -B build && cmake --build build`, which also builds the tests.

## Searching

Plain words must all appear in a file's path or tags, in any order, ignoring case and accents. Tags go in braces and can use `AND`, `OR`, `NOT` (uppercase), `-` and parentheses:

- `akira 1988` finds files with both words
- `{film -watched}` finds films you haven't watched
- `{movie (isshi OR katsuhiro)}` finds movies with either tag
- `pdf {university}` finds files tagged `university` with "pdf" in the path
- `{music/kanye}` finds files tagged `music/kanye` or anything under it, like `music/kanye/mbdtf`

Clicking tags in the sidebar writes these for you.

## Shortcuts

Library:

- Enter: open. Space: quick look.
- Ctrl+F: search. Ctrl+T: tag the selection. Ctrl+O: add a folder.
- Ctrl+Z / Ctrl+Y: undo / redo. Ctrl+C: copy files.
- Ctrl+I: info pane. Ctrl+B: sidebar. Ctrl+, : settings.
- Ctrl+A: select all. Esc: clear selection.
- F2: rename. Del: remove from the library. Shift+Del: Recycle Bin.

Viewer:

- Left / Right: previous / next file. Home / End: first / last.
- + / - / 0: zoom. Z: actual size. F: fullscreen. Esc: close.
- Ctrl+T: show the file's tags, with a field to add more (Enter adds, Tab completes, Esc hides).
- Videos and music: Space or K play/pause, Left / Right seek (step while paused, Ctrl+Left / Right always step), Up / Down volume, M mute, [ / ] speed, Backspace normal speed.
- Music plays on to the next track in the results. Esc keeps it playing in a strip under the grid; click its name to come back.
- Books: Left / Right turn pages, Space scrolls, W fit width, R reading direction.
