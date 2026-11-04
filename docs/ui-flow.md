# UI flow map

How a person moves between the screens of Corpus's single window. The states themselves, with how to reach each one, are
in [ui-states.md](ui-states.md) (generated from `tools/ui-states.psd1`). Key routing is in `apps/gui/window_keys.cpp`
(`Window::OnKeyDown`) and `apps/gui/viewer_input.cpp`.

## Who gets the keys

Top to bottom, the first that applies takes the key and nothing below it sees it:

1. A **sheet** (tag, rename, confirm, folder tags) is open.
2. A **context menu** is open.
3. The **viewer** is open. Only Ctrl+Z / Ctrl+Y pass through to the library (undo a tag taken off in the viewer).
4. The **settings** page is open. Ctrl+, closes it and Ctrl+B still toggles the sidebar.
5. Library shortcuts that work anywhere: Ctrl+, Ctrl+B Ctrl+I Ctrl+plus/minus Ctrl+T Ctrl+O Ctrl+F.
6. A text field with the keyboard (search, then the sidebar's tag filter; Tab goes search, grid, tag filter).
7. The grid: Enter, Space, F2, Del, Shift+Del, Shift+F10 / Menu key, Ctrl+C, Q / Shift+Q, Ctrl+Z / Ctrl+Y, arrows.

## Screens and transitions

```
                    Ctrl+O / drop / Add button
   (empty library) ------------------------------> folder picker --> Folder tags sheet --> Library grid
                                                                       (Enter accepts)

                       Ctrl+, / Settings row                      Ctrl+, / Done
   Library grid  <-------------------------------> Settings  (Licenses page hidden inside)
        |
        | Enter / double-click / Space (quick look, a selection or all on screen)
        v
     Viewer ----- picture --- + - 0 zoom, Z actual size, F fullscreen, Ctrl+T tags field
        |   \---- video  ---- Space/K, arrows seek, Up/Down volume, M, [ ] speed, track shortcuts
        |    \--- book   ---- Left/Right pages, W fit width, R direction
        |     \-- music ---- the "stage": cover, Up next rail, queue (Q / Shift+Q), cover fullscreen
        |
        | Esc (closes the viewer; music keeps playing in the strip under the grid)
        v
   Library grid + music strip --- click the strip's name --> Viewer on the playing song
```

- Enter on a file the viewer cannot show opens it in its default program (a confirm sheet asks first when many are chosen).
- Left / Right / Home / End in the viewer step through the results (or the selection, when the viewer was opened from several).
- A picture or video opened over parked music shows the strip retracted; the pointer at the bottom edge slides it out.
- The window can be any size: the stage drops the rail when narrow; the grid, settings and viewer shrink with it (small
  and short window states are in the manifest).

## From the grid

| From | Action | To |
|---|---|---|
| Grid | Ctrl+F, or click the search | Search field (Esc empties it, then leaves; Enter or Down goes to the first result) |
| Grid | Tab | Search, then the sidebar's tag filter, then back |
| Grid | Click a Library row (All, Pictures, Videos, Audio, Books, Untagged, Missing) | Same grid, filtered |
| Grid | Click a tag in the sidebar | Search gets a `{tag}` term |
| Grid | Ctrl+T | Add tag sheet (Enter adds, Esc closes) |
| Grid | F2 | Rename sheet (a file, or the tag under the pointer in the sidebar) |
| Grid | Del / Shift+Del | Confirm sheet: remove from the library / delete from disk (danger style) |
| Grid | Right-click, Shift+F10, Menu key | Context menu: file, song, empty grid, tag or sort (Esc closes) |
| Grid | Sort button in the title bar | Sort menu |
| Grid | Ctrl+I | Info pane for the selection (also from the menu: Info) |
| Grid | Ctrl+B | Sidebar hidden or shown (a button in the search row does the same) |
| Grid | Ctrl+plus / Ctrl+minus | Bigger / smaller thumbnails |
| Grid | Drag files out | Explorer or another program receives them |
| Grid | Q / Shift+Q on music | Added to the queue / played next, with a notice |
| Settings | Backup > Restore | Confirm sheet (danger) |
| Anywhere | Drop files or folders on the window | Import progress bar, then the folder tags sheet |

## Transient things

- **Notices** (queued, play next, no music selected, export done...) show briefly over the grid; the error style is red.
- **Import progress** is a bar over the grid; the import can be cancelled.
- **Tooltips** appear after a short hover (e.g. the sort button when the window is too narrow for its label).
- **Viewer controls and strip** hide when the pointer is idle and come back on movement.
