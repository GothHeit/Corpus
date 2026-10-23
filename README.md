# Corpus

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/language-C%2B%2B17-00599C.svg)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6.svg)
![Status](https://img.shields.io/badge/status-work%20in%20progress-yellow.svg)

A personal file catalog organized by **tags**, not folders. Instead of moving or copying files, Corpus
just keeps references to them and lets you filter your library by any combination of tags, the files
stay exactly where they always were.

The GUI is built **from scratch**, in raw Win32, drawn with Direct2D and DirectWrite. No Qt, GTK, or Dear ImGui.

## Why

Folders force you to decide "where" a file lives. But many files are several things at once (an image that's both `wallpaper` and `icon`, a pdf that should be at `documents` but also in the `university` folder). Corpus models
that directly: a file can have N tags, a tag can point to N files, and the "current view" is just the
result of filtering by a set of tags.

## Searching

The GUI has a search bar that filters the current grid by filename or tag (case-insensitive, substring match).
Case is ignored for accented and non-Latin letters too (`AÇÃO` finds `ação`, `москва` finds `МОСКВА`), but
accents are not: `acao` doesn't find `ação`. Spaces at the start and end of the search are ignored.

It also supports a boolean tag query language inside curly braces — `AND` / `OR` / `NOT` / `-` / parentheses,
matching whole tag names, ignoring case (not substring). Writing tags next to each other means `AND`. `-`
negates only at the start of a word: inside a word it is part of the tag name, so `{sci-fi}` finds the tag
`sci-fi` and `{-sci-fi}` finds files without it.

A search can have several `{...}` groups, and a file must match every one of them. The free text outside the
groups still does the plain name/tag search above, and must match too. The groups only separate the text,
so `my {Wallpaper} photos` searches the text `my photos`:

```
movie{Wallpaper AND 4K}
{(Drama AND (Anime OR Manga)) -Shounen}
{sci-fi OR fantasy} 2024 {-watched}
```

A search can also be narrowed to one of the sidebar's Library views, passed to `library::search` next to
the query rather than written in it, so no tag name is taken: All files, Untagged (files with no tag) or
Missing (files no longer at their path, as `library::missing_files` sees them). The query still applies
inside the view, so `{image}` in Missing finds the missing images. Missing doesn't touch the disk on each
search: `library::refresh_missing` checks every file once and the view keeps that result (adding, removing and
relinking files keep it up to date), so call it when the view is chosen or the disk may have changed. Without a
refresh, the first search in Missing does it.

A malformed expression in any `{}` group (unclosed or empty brace, dangling operator) underlines the search
bar in rose, says what is wrong under it ("Unclosed brace"), and keeps showing the last valid result instead of
clearing the grid. When nothing matches, "Clear filters" empties the search and goes back to All files.

The sidebar on the left starts with the Library views (All files, Untagged, Missing), each with how many files
of the library it holds. The disk is checked for missing files in the background when Corpus opens (the Missing
count appears once that is done), again each time Missing is picked, and each time you come back to Corpus from
another window while Missing shows, so files deleted or put back meanwhile show up; otherwise its count is as of
the last check. Below them, the sidebar lists every tag of the library with how many of the files on screen have it; tags
none of them have are dimmed but stay in place. Clicking a tag cycles it through required (a violet square,
written `{Tag}` in the search), excluded (a rose dash, `{-Tag}`) and off, by editing the search text, which
stays the one source of truth: typing `{Tag}` or `{-Tag}` yourself marks the tag in the sidebar too. A tag
inside a bigger group, such as `{a OR b}`, isn't marked, and clicking it adds a group of its own. The field
above the list narrows it to the tags whose name contains what is typed (`Esc` empties it). The button next to the
title, or `Ctrl+B`, slides the sidebar away to give the files the whole width, and brings it back; Corpus opens
with it as you last left it. Windows too narrow for both leave the sidebar out.

## Adding and opening files

Drag files or folders from Explorer onto the window to add them, with everything inside the folders (an empty
library also has an "Add folder…" button that picks folders instead). Only
their paths are stored: nothing is copied or moved. A bar at the bottom of the file area shows how many files
have been found so far, with a Cancel button, then what was added ("Added 120 files · 3 skipped"); the window
stays usable meanwhile. Select some items and press Enter to open them with their default program (a folder
opens in Explorer).

## Viewing pictures

Double-clicking a picture (or a video, see below) opens it inside Corpus instead, over the whole window under the
title bar, and Space does the same for the selected picture, Quick Look style; double-clicking anything else opens
it in its default program. A picture is anything Windows has a decoder for (JPEG, PNG, GIF, BMP, TIFF, ICO, WebP, and HEIC, AVIF or
camera raw files once their extensions from the Microsoft Store are installed); PSD, AI, SVG and the like keep
opening in their default program, and so does a picture Windows fails to read.

The picture first fits the window, never enlarged past its own size. The wheel (or `+` and `-`) zooms at the
pointer, dragging pans, a double-click switches between fitted and actual size, and `0` fits it again. The arrow
keys (and the buttons on the sides) step through the pictures on screen, or through the selected ones when
several are selected. Animated GIFs play, as many times as they ask to, and an icon shows its largest size. `Esc`
or Space closes the viewer with the last picture shown selected (or, opened among selected pictures, with them
still selected and the last one focused); Enter, which opened the viewer, does nothing in it. Dropping files on the window
closes the viewer, so the import shows. The name, position, size and zoom float under the picture and fade
out, with the pointer, after two seconds without moving the mouse. Photos are turned the way the camera was held.

Pictures are decoded on a thread of their own, so a big photo never freezes the window. Anything over 64
megapixels, or larger than the graphics card's biggest bitmap, is scaled down to fit, and its size says
"reduced". A GIF is decoded one frame ahead of the one on screen rather than all at once, so a long animation only
costs two frames of memory; one over 16 megapixels shows its first frame as a still.

`F` lets the viewer cover the whole window, title bar included, and `F` or `Esc` gives the window back.

## Playing videos

Videos (MKV, MP4, MOV, AVI, WebM, WMV, MPEG and TS, among others) play in the same viewer, through
[libmpv](https://mpv.io), so the arrows step through pictures and videos alike. The video is decoded on the
graphics card where it can be and fills the viewer, keeping its shape. Subtitles are on: the track the file marks as
the default, or else the first one, including subtitle files next to the video (`Movie.srt` for `Movie.mkv`),
styled ASS subtitles with the fonts embedded in the file, and the picture subtitles of DVDs and Blu-rays.

Under the name, a control bar fades in and out with it: play and pause, the time played, a seek bar to click or
drag, the length, the volume (the speaker mutes), a menu of subtitle tracks (with Off) and one of audio tracks
when there are several, and a button that covers the whole window. The controls stay up while the video is
paused. A click on the video plays or pauses it and a double-click covers the window; the wheel sets the volume.
Over a video, `Left` and `Right` seek 5 seconds while it plays and step to the previous and next file while it is
paused, `Up` and `Down` set the volume, Space or `K` plays and pauses, `M` mutes, and `Page Up`, `Page Down`, `Home` and `End` step through the files. `Esc` closes an open menu first, then leaves
the whole window, then closes the viewer. A video that ends stays on its last frame, and
play starts it again.

libmpv is a DLL, `libmpv-2.dll`, built into `corpus-gui.exe` so Corpus stays a single file you can move anywhere.
The first time a video opens, Corpus writes it out to `%LOCALAPPDATA%\Corpus` (or a `Corpus` folder in `%TEMP%`
when that can't be written to) and loads it from there, so a session without videos neither loads nor pays for it;
later sessions load the copy already written, and a new build of Corpus with another libmpv replaces it. A
`libmpv-2.dll` next to the `.exe` is ignored then. A build made without the DLL (see "libmpv"
under Building) plays videos only with one next to it. Without libmpv, or with a video it can't play, videos open in
their default program as before.

## Reading PDFs, comics, EPUBs and text

PDFs, comic book archives (`.cbz`, a zip of pictures, one per page) and EPUB books open in the same viewer as a book,
page by page, on the page you left them at: Corpus keeps how far each one was read in the library. A PDF page is
drawn by Windows' own PDF renderer, and a comic's pages are its pictures in the order of their names ("page 2" before
"page 10"), read through Windows' zip folders, so neither needs anything installed. A PDF locked with a password, or
a file that isn't really a PDF or a zip, opens in its default program instead.

An EPUB is a zip of web pages, one per chapter. Corpus lays its text out with the bundled litehtml into pages that
fill the viewer, and draws them with DirectWrite: light text on the viewer's dark backdrop, in a column about 70
characters wide, whatever colors and margins the book asks for (its fonts are used when Windows has them). Each
chapter starts on a new page, and a page ends between two lines. When the window changes size the pages are laid out
again, and the text you were reading stays on screen; the place in the text is what is kept, so a book opens at
the same sentence on a smaller or bigger screen. EPUBs have no page fit to choose, since their pages always fit.

Text files (`.txt`), Markdown (`.md`, `.markdown`) and web pages (`.html`, `.htm`) open the same way, as one long chapter
paged like an EPUB's, and keep the place you stopped in the same way. A text file's encoding is found from its byte
order mark (UTF-8 or UTF-16), else it is read as UTF-8 if it is valid, else as the Windows ANSI code page; paragraphs
broken by hand at the same width are joined to fill the page, and any other keeps its line breaks. Markdown is turned
into HTML by a small converter of Corpus's own: headings, paragraphs, emphasis, code, lists, quotes, rules, links
(shown, not clickable) and pictures next to the file. A web page is drawn as it is, without scripts or anything from the
web, and with its local style sheets and pictures. Files over 16 MB open in their default program.

A PDF or comic page fits the viewer whole, or by its width with the rest scrolled to (`W`, the fit button, or a double-click
in the middle). `Left` and `Right` turn the page that way, `Up` and `Down` scroll it and turn it at its ends,
`Page Up` and `Page Down` scroll a screen at a time, and `Home` and `End` go to the first and last page. The wheel
scrolls, and turns the page once it goes on past the end of one; a click near either side turns the page that way,
and dragging pans. `Ctrl` with the arrows, `Page Up`, `Page Down`, `Home` or `End` steps through the files instead,
and `Ctrl` with the wheel zooms. For manga, `R` (or the arrow button) turns the pages right to left: `Left` then goes
forward, and the slider runs from the right. Both choices are kept for next time.

Under the name, a page bar fades in and out with it: the page and the page count, a slider to click or drag to any
page, and the two buttons. Pages are made on a thread of their own, as wide as the screen, and the ones on either
side of the page shown are made ahead, so turning a page shows the next one at once.

## Tagging and removing files

The strip at the bottom of the window edits the tags of the selected files. Click its field (or press
`Ctrl+T`), type one or more tags separated by spaces and press Enter to add them to every selected file.
While typing, the last word is completed in grey from the tags already in the library, the most used
first: `Tab` accepts the completion and `Up`/`Down` show the others. A word that only differs from an
existing tag in case is that tag, so `anime` adds `Anime`. Characters the search can't take in a tag name
(`{`, `}`, `(`, `)`, and `-` at the start) are refused, and the words `AND`, `OR` and `NOT` are skipped.

Next to the field, each tag of the selection is a chip: its `×` takes the tag off every selected file. A
chip with a dashed outline is on only some of them and says how many (`Travel 2/5`); clicking it adds it to
the rest. When the chips don't fit, the
mouse wheel scrolls them sideways.

`Delete` removes the selected files from the library, after asking. The files themselves stay where they
are. A tag left on no file at all, by a removal or by taking it off, is deleted with it.

`F2` (or Rename in the right-click menu) renames the file on disk and in the library, keeping its tags; a name
already taken in the folder is refused. `Shift+Delete` (Delete from disk in the menu) moves the selected files to
the Recycle Bin after asking, and takes them out of the library; Corpus never deletes a file for good.

Settings has a switch that adds Corpus to the "Open with" list of Windows for the pictures, videos, PDFs, EPUBs,
CBZs and text files it shows (entries under `HKEY_CURRENT_USER\Software\Classes`, no administrator rights, the
default program of each type unchanged). `corpus-gui.exe <file>` opens the file in the viewer without adding it to
the library; if Corpus is already open on that library, the file opens in that window instead.

Each of these changes is saved in one transaction: if anything fails, none of it is kept.

## Storage

The library lives in a SQLite database. The GUI keeps it in `%LOCALAPPDATA%\Corpus\corpus.db`, so it
doesn't depend on where the `.exe` is; the CLI uses `libs/corpus.db` (not versioned). The database has to
be on a local disk: SQLite's file locks don't work over network shares, and from Windows that includes
`\\wsl.localhost\`, so a GUI built inside WSL can't keep its library in the repository. Everything is loaded into memory
at startup, so filtering and searching never touch the disk (Missing only when it is refreshed); every change (adding or removing a file,
tagging or untagging it, creating, renaming or deleting a tag) is written to the database the moment it happens, so nothing is
lost if the app crashes or is killed. If a write fails, the GUI says so and reloads the library from the
database, so the screen never shows a change that wasn't saved; files dropped together are saved all or
none.

```
files(id, path UNIQUE, identity)   -- identity: see "Moved and missing files"
tags(id, name UNIQUE)
file_tags(file_id, tag_id)   -- which file has which tag, deleted along with the file or the tag
reading_progress(file_id, position, length, location, last_read)   -- see "Reading progress"
```

The schema version is kept in `PRAGMA user_version` (currently `3`), and the database runs in WAL mode
with foreign keys on. A database from an older version is upgraded in place when it is opened, in one
transaction: version 1 lacks the `identity` column, version 2 the `reading_progress` table. The first time a database is opened it is created and a JSON library can be
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

## Reading progress

Corpus remembers where each document (a PDF, a comic, a book) was left, so it opens there next time.

- `library::set_progress(file, progress)` records a `reading_progress`: `position` (for paged files, the page
  index from 0), `length` (the page count, or 0 if unknown), `location` (where to resume in a file without
  fixed pages, such as an EPUB, as its reader writes it) and `last_read` (seconds since 1970; 0 means now).
  A negative position or length, or a position not below a known length, is refused.
- `file::get_progress()` gives it back, or nullptr for a file never read; `library::clear_progress(file)`
  forgets it.
- `library::recently_read(limit)` lists the files with progress, the most recently read first.

The progress belongs to the file's row, so it follows the file when it is relinked and goes when the file is
removed. Like every other change, it is written to the database at once. The CLI shows and edits it with
option 14.

## Importing folders

A folder is imported in two steps, so a big one can be scanned without freezing the app:

- `scan_folder(folder, &cancel, progress)` lists every file in the folder and the folders under it, and reads
  their identities. It only touches the file system, so it can run on a background thread. `progress` is
  called for each file found, with the count so far and its path, and the scan stops as soon as `cancel`
  reads true. Links to folders aren't followed and a folder reached twice is listed once, so a link back up
  the tree can't loop. Subfolders that can't be read are skipped and counted; folders themselves aren't added.
  Files the system keeps for itself are skipped and counted too: `desktop.ini`, `Thumbs.db` and `.DS_Store`,
  and on Windows anything both hidden and system (folders like `$RECYCLE.BIN` with all they hold).
- `library::add_files(scan.files)` then adds the files not in the library yet, on the thread that owns the
  library, in one transaction: if anything fails nothing is saved and the library stays as it was.

The CLI imports a folder with option 13, showing how many files it has found as it goes. The GUI runs the
scan on a thread of its own when folders are dropped, and calls `add_files` back on the window's thread once
it is done; a cancelled import adds nothing.

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

### litehtml

The EPUB reader lays out its chapters with litehtml (`third_party/litehtml/`), which comes with the gumbo HTML parser
in C. Both go into one static library, also built once (the `-w` silences their own warnings):

```sh
cd third_party/litehtml
gcc -c -O2 -w -std=c99 src/gumbo/*.c -Isrc/gumbo/include -Isrc/gumbo/include/gumbo
g++ -c -O2 -w -std=c++17 src/*.cpp -Iinclude -Iinclude/litehtml -Isrc/gumbo/include -Isrc/gumbo/include/gumbo
ar rcs liblitehtml.a *.o && rm *.o
cd ../..
```

The GUI build below links that `liblitehtml.a`.

### App (Windows, via MinGW)

The `.exe` carries its own icon (`corpus.ico`) and fonts (Geist and Geist Mono, from `third_party/geist/`),
embedded via a resource script and loaded from memory, so nothing has to be installed. Before linking,
compile that resource with `windres`:

```sh
windres apps/gui/resource.rc -O coff -o apps/gui/resource.res
```

Then build:

```sh
g++ apps/gui/main.cpp apps/gui/window.cpp apps/gui/title_bar.cpp apps/gui/text_field.cpp apps/gui/file_grid.cpp apps/gui/tag_panel.cpp apps/gui/tag_sidebar.cpp apps/gui/filter_query.cpp apps/gui/import_bar.cpp apps/gui/missing_check.cpp apps/gui/renderer.cpp apps/gui/thumbnails.cpp apps/gui/image_decoder.cpp apps/gui/book_pages.cpp apps/gui/pdf_book.cpp apps/gui/cbz_book.cpp apps/gui/epub_book.cpp apps/gui/text_book.cpp apps/gui/html_canvas.cpp apps/gui/zip_folder.cpp apps/gui/book_reader.cpp apps/gui/viewer.cpp apps/gui/video_player.cpp apps/gui/ui_util.cpp src/library.cpp src/database.cpp src/file.cpp src/file_identity.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp src/text.cpp third_party/sqlite/sqlite3.o third_party/litehtml/liblitehtml.a -Ithird_party/mpv -Ithird_party/litehtml/include apps/gui/resource.res -o apps/gui/corpus -ld2d1 -ldwrite -ldwmapi -lgdi32 -luser32 -lshell32 -lole32 -lwindowscodecs -luuid -lruntimeobject -lshcore -lshlwapi -mwindows
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
`-Library <corpus.db>` starts from a copy of that database instead of an empty one, to see the grid with files.
`-OpenFile sample` (or a path to a picture) starts it with that file on the command line, so the window opens on the
viewer, then starts a second copy that must hand the file to the first and end; `smoke-test-second.png` shows the result.

### libmpv

Videos play through `libmpv-2.dll`, which the build puts inside `corpus-gui.exe` when it finds it at
`build-gui\libmpv-2.dll` as it configures (`-DCORPUS_LIBMPV_DLL=<path>` takes it from elsewhere; configuring again
picks up a new one). Without it the build still works and says so, and the `.exe` plays videos only with the DLL
next to it. Only its C API header is vendored (`third_party/mpv/mpv/client.h`, ISC license); the DLL is loaded at
run time, so nothing links against it, and it is never committed. `tools/get-libmpv.ps1` downloads a pinned
prebuilt build for Windows x64, checks its SHA-256 and puts the DLL in `build-gui\`, before configuring:

```sh
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w tools/get-libmpv.ps1)"
```

`-Destination <folder>` puts it elsewhere. The build is shinchiro's
[mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake) of 2026-10-18 (mpv `v0.41.0-1205-g01e2533f3`),
`mpv-dev-x86_64-20261018-git-01e2533f31.7z`, from the mpv-player-windows project's
[libmpv folder on SourceForge](https://sourceforge.net/projects/mpv-player-windows/files/libmpv/), which keeps old
builds. That build includes GPL parts (mpv's default build, and FFmpeg with x264), so the DLL is under the GPL,
so a `corpus-gui.exe` with it built in carries GPL code. A newer build from the same place also works (Corpus takes any libmpv with client API 2.x): update
the name and the hash in the script.

#### Missing DLL when running on another machine

If the `.exe` complains about a missing DLL on another machine (e.g. `libgcc_s_seh-1.dll not found`,
`libstdc++-6.dll not found`), it's because by default `g++` links the MinGW runtime (`libgcc`, `libstdc++`)
as a DLL, requiring MinGW to be installed (or the DLLs present) on the machine that **runs** the
program — not just the one that compiled it. To avoid this, embed the runtime inside the `.exe` itself:

```sh
g++ apps/gui/main.cpp apps/gui/window.cpp apps/gui/title_bar.cpp apps/gui/text_field.cpp apps/gui/file_grid.cpp apps/gui/tag_panel.cpp apps/gui/tag_sidebar.cpp apps/gui/filter_query.cpp apps/gui/import_bar.cpp apps/gui/missing_check.cpp apps/gui/renderer.cpp apps/gui/thumbnails.cpp apps/gui/image_decoder.cpp apps/gui/book_pages.cpp apps/gui/pdf_book.cpp apps/gui/cbz_book.cpp apps/gui/epub_book.cpp apps/gui/text_book.cpp apps/gui/html_canvas.cpp apps/gui/zip_folder.cpp apps/gui/book_reader.cpp apps/gui/viewer.cpp apps/gui/video_player.cpp apps/gui/ui_util.cpp src/library.cpp src/database.cpp src/file.cpp src/file_identity.cpp src/tag.cpp src/saving.cpp src/tag_query.cpp src/text.cpp third_party/sqlite/sqlite3.o third_party/litehtml/liblitehtml.a -Ithird_party/mpv -Ithird_party/litehtml/include apps/gui/resource.res -o apps/gui/corpus -ld2d1 -ldwrite -ldwmapi -lgdi32 -luser32 -lshell32 -lole32 -lwindowscodecs -luuid -lruntimeobject -lshcore -lshlwapi -mwindows -static-libgcc -static-libstdc++ -static
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
(malformed queries included), filtering and search (brace groups, hyphenated tags, case beyond ASCII, Library views),
edits, and removing files and tags. It also checks that removals reach the database, which it keeps in the
folder given as its argument. Run as root on Linux, it mounts a tmpfs inside that folder to test finding
moved files on a volume mounted there; otherwise it says that check was skipped:

```sh
cmake --build build --target corpus-core-test
./build/corpus-core-test /tmp
```

`corpus-litehtml-test` (CMake target) lays out a short HTML page with the vendored litehtml through a stub
container with fixed-width fonts, to check that it builds, links and wraps text as expected.

## Structure

```
include/      core data model headers (library, file, tag, database, saving, tag_query, file_identity, text)
src/          core data model implementation — no entry point, just the library
apps/cli/     terminal harness, round-trip test and unit tests for the model (dev/testing only)
apps/gui/     the actual product: Win32 GUI, drawn with Direct2D/DirectWrite
libs/         JSON libraries (sample.json is a versioned example) and the CLI's corpus.db
third_party/  vendored SQLite amalgamation, the Geist fonts (SIL Open Font License, see OFL.txt), libmpv's API header,
              and litehtml with its gumbo HTML parser (lays out EPUB chapters)
cmake/        MinGW cross-compile toolchain file, for building apps/gui from Linux/WSL
tools/        GUI smoke test and the libmpv download (PowerShell)
```

### How the GUI works

Windows talks to the app through **messages** (`WM_LBUTTONDOWN`, `WM_KEYDOWN`, `WM_PAINT`, ...).
`Window` receives all of them and forwards each one to the component it concerns. Every component
owns a rectangle of the window and follows the same shape: `SetBounds`, `Paint`, and `On<Event>` handlers.

```
main.cpp        opens the library database, creates the Window, runs the message loop
window.*        creates the window, splits it between the components, routes messages
renderer.*      Direct2D/DirectWrite: shapes, text layout and measuring, the bundled fonts
title_bar.*     the macOS-style close/minimize/maximize buttons, the title, dragging, maximize/restore
text_field.*    a single-line text field and its caret (the search bar, the tag field)
file_grid.*     the file grid: tiles, layout, scrolling, scrollbar, selection, empty states
thumbnails.*    the tiles' thumbnails: made by Windows on background threads, kept on disk and in memory
viewer.*        the in-window viewer: fit, zoom, pan, stepping through files, the fading overlays, video controls,
                the page bar of a book
image_decoder.* decodes pictures for the viewer with WIC on a thread of its own, one GIF frame at a time
book_reader.*   makes the pages of a book for the viewer on a thread of its own, and the pages around it ahead
book_pages.*    a book's pages, by format: pdf_book.cpp (Windows.Data.Pdf), cbz_book.cpp (pictures in a zip) and
                epub_book.cpp (an EPUB's chapters laid out by litehtml and cut into pages), text_book.cpp
                (text, Markdown and HTML files turned into a page for it)
html_canvas.*   what litehtml needs to lay out and draw HTML: DirectWrite fonts and Direct2D drawing
zip_folder.*    reads a zip (a comic, an EPUB) through the shell's zip folders
video_player.*  plays videos for the viewer with libmpv (built into the .exe, written out and loaded on the first video)
                in a child window under the overlays
tag_panel.*     the tags of the selected files: the tag field, its completion and the tag chips
import_bar.*    the status bar shown while dropped files and folders are added: progress, Cancel, outcome
theme.hpp       every color and shared size (one dark palette)
strings.hpp     the text the GUI shows, kept in one place for translation
ui_util.*       small helpers (DPI scaling, UTF-8 conversion, counts with thousands separators, hover fades)
```

A typical round trip: the user scrolls → `WM_MOUSEWHEEL` → `Window` → `FileGrid::OnMouseWheel` updates
its scroll position and marks its own rectangle as dirty (`InvalidateRect`) → Windows sends `WM_PAINT`
→ `Window::OnPaint` asks every component to paint through the `Renderer`, which Direct2D shows in one go.
Nothing runs between messages, so an idle window uses no CPU. The import bar's timers only run while an import
does and for a few seconds after, and the hover fades' only for the 83 ms a fade takes. Since every paint redraws
the whole window, the grid keeps what its tiles draw (laid out names, type icons) instead of rebuilding it.

Tiles show the thumbnail Windows makes for the file (photos, video frames, PDF pages: whatever has a thumbnail
handler, through `IShellItemImageFactory`); shortcuts (.lnk) show the icon Explorer gives them, usually their
program's; other files keep their type icon. Two background threads make them,
visible tiles first and then a screen of tiles below and above, so scrolling never waits for them; each one fades
in over the icon in 167 ms. They are kept as small PNGs in a `thumbnails` folder next to the library
(`%LOCALAPPDATA%\Corpus\thumbnails`), one file per library file and size, named after a hash of its path, so the
next start shows them at once. A cached thumbnail is made again when the file's size or modification time
changes; the files on screen are checked each time the window becomes active. Files Windows has no thumbnail
for are written down too, but a failure that may pass (a locked file, a handler that timed out) is tried again
later. Cache files not read in 60 days of using Corpus are deleted at start: of the wait between two starts,
only the first week counts, so coming back after a long break keeps them. Up to 64 MB of thumbnails stay in
memory, the ones seen longest ago going first, though the ones on screen and next to it always stay. Closing
waits at most two seconds for a thumbnail being made.

## License

MIT — see [LICENSE](LICENSE).

### Third-party code

| Component | Version | License | Where |
|---|---|---|---|
| SQLite | amalgamation | public domain | `third_party/sqlite/` |
| Geist, Geist Mono | | SIL Open Font License 1.1 | `third_party/geist/OFL.txt` |
| libmpv client API header | | ISC | `third_party/mpv/mpv/client.h` |
| libmpv (prebuilt DLL, built into the `.exe`, never committed) | shinchiro 20261018 | GPL (includes FFmpeg) | [mpv-player/mpv](https://github.com/mpv-player/mpv) |
| litehtml | v0.10 (commit 9bc84b8) | BSD 3-Clause | `third_party/litehtml/LICENSE` |
| gumbo HTML parser (bundled with litehtml) | litehtml v0.10 copy | Apache 2.0 | `third_party/litehtml/src/gumbo/LICENSE` |

litehtml and gumbo are vendored unmodified, keeping only their sources, headers and license files. The license texts and notices, including the ones inside litehtml's files, are built into the `.exe` and shown on a page hidden in Settings (click the name in Settings > About five times).
