# UI states

Every screen and state of the window and how to reach it. Generated from `tools/ui-states.psd1` by
`tools/ui-screenshots.ps1 -WriteDocs`; edit the manifest, not this file. "Shot" says whether `tools/ui-screenshots.ps1`
captures the state (yes) or not yet (the reason is in the last column).

## Library

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `01-grid` | Grid, files added | Ctrl+O, pick a folder, accept the folder tags sheet. | yes |
| `18-grid-sidebar` | Grid with the sidebar open | Ctrl+B (open by default). | yes |
| `19-grid-three-selected` | 3 files selected, tag bar active | Click a tile, Shift+click another. | yes |
| `20-details-song` | Info pane on a song | Select a song, Ctrl+I. | yes |
| `21-empty` | Empty library | First start, nothing added: the drop hint. | yes |
| `22-grid-sidebar-hidden` | Grid with the sidebar hidden | Ctrl+B. | yes |
| `23-grid-tiles-large` | Biggest thumbnails | Ctrl+plus, several times. | yes |
| `24-grid-tiles-small` | Smallest thumbnails | Ctrl+minus, several times. | yes |
| `25-search-no-results` | Search with no matches | Ctrl+F, type a word nothing contains. | yes |
| `26-search-tag-query` | Search with a {tag} query | Ctrl+F, type {files} (Shift+[ and Shift+]). | yes |
| `27-details-picture` | Info pane on a picture | Select a picture, Ctrl+I. | yes |
| `28-grid-small-window` | Grid in a small window | Window about 480 x 560. | yes |
| `29-view-untagged` | Untagged / Missing views | Click Untagged or Missing in the sidebar. | no: Needs the sidebar rows' coordinates (no shortcut). |
| `30-tile-hover` | Pointer over a tile | Move the pointer onto a tile. | yes |
| `31-import-progress` | Import progress bar | Add a big folder; the bar shows while it runs. | no: The scratch library is too small to catch it mid-way. |
| `32-drop-target` | Dragging files over the window | Drag a file from Explorer over the window. | no: Needs a real OLE drag from another program. |
| `64-search-in-text` | Search hit inside a book ("in text" tile) | Ctrl+F, type a word that is only inside an EPUB. | yes |
| `65-details-in-text` | Info pane with the text snippet | Search a word inside a book, select the hit, Ctrl+I. | yes |
| `69-details-notes` | Info pane with the notes of the file's tags | Give a tag a note (tag menu, Notes...), select a file with it, Ctrl+I. | no: Needs a tag with a note in the scratch library. |
| `70-sidebar-inbox` | Inbox row selected (search shows inbox:) | Click Inbox in the sidebar; click it again to clear. | no: Needs files without maison/* or ref/* tags; the smoke library is empty. |
| `71-inbox-pasted` | Notice after Ctrl+V of a picture | Copy a picture in a browser, Ctrl+V in the grid; Info pane shows its Source. | no: Needs the clipboard of a real program. |

## Menus

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `33-menu-file` | File context menu | Select a file, Menu key (or Shift+F10). | yes |
| `34-menu-song` | Song context menu | Select a song, Menu key. | yes |
| `35-menu-grid` | Menu on the empty grid | Right-click empty space in the grid. | yes |
| `36-menu-sort` | Sort menu | Click the sort button in the title bar. | yes |
| `37-menu-tag` | Tag context menu (Notes, Export as Markdown, Rename, Delete) | Right-click a tag in the sidebar. | no: Needs the tag rows' coordinates. |
| `76-menu-board-tab` | Board tab menu (Rename, Export as PNG, Delete in rose) | Right-click a board tab. | no: Needs a board. |
| `77-menu-board-file` | Menu of a file on a board (adds Bring to front, Send to back, Take off board) | Right-click a file on the board. | no: Needs a board. |
| `86-menu-board-canvas` | Menu of the empty canvas of a board (New board, Place all, Fit all, Export as PNG) | Right-click empty room on a board. | no: Needs a board. |

## Sheets

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `38-sheet-tag` | Add tag sheet | Select files, Ctrl+T. | yes |
| `39-sheet-rename` | Rename sheet | Select a file, F2. | yes |
| `40-sheet-remove` | Remove from library confirm | Select files, Del. | yes |
| `41-sheet-delete` | Move to Recycle Bin confirm (danger) | Select files, Shift+Del. Esc cancels; nothing is deleted. | yes |
| `42-sheet-folder-tags` | Folder tags sheet after an import | Ctrl+O, pick a folder with subfolders. | yes |
| `43-sheet-restore` | Restore a backup confirm | Settings, Restore a backup, Restore. | no: Would need a backup file in the real catalog folder. |
| `44-picker-folder` | Add folder picker (Windows dialog) | Ctrl+O. | no: It is Windows' own window, not Corpus's; the window capture does not include it. |
| `45-notice-queued` | Notice: added to the queue | Select a song, Q. | yes |
| `46-notice-error` | Notice, error style: no music selected | Select a picture, Q. | yes |
| `63-sheet-shortcuts` | Keyboard shortcuts sheet | F1 (F1 or Esc closes). | yes |
| `68-sheet-tag-notes` | Tag notes sheet (note, creator, year, link, taken, refused) | Right-click a tag in the sidebar, Notes...; or click a note in the info pane. | no: Needs the tag rows' coordinates. |
| `78-sheet-board-rename` | Rename board sheet | Right-click a board tab, Rename board. | no: Needs a board. |
| `79-sheet-board-delete` | Delete board confirm (danger) | Right-click a board tab, Delete board. Esc cancels. | no: Needs a board. |
| `80-notice-board-export` | Save dialog and notice for Export PNG | Export PNG... on the bar, or Ctrl+E. | no: The save dialog is Windows' own window. |

## Settings

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `47-settings-top` | Settings, top | Ctrl+comma. | yes |
| `48-settings-bottom` | Settings, scrolled to the end (Help and About) | Ctrl+comma, then the wheel. | yes |
| `49-settings-small` | Settings in a small window | Ctrl+comma in a 480 x 560 window. | yes |
| `50-licenses` | Licenses page | Settings, Help, Licenses row. | no: The row needs a click (no keyboard path yet). |

## Viewer

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `08-picture-retracted` | Picture over parked music, strip retracted | Play a song, Esc, open a picture. | yes |
| `09-picture-strip-out` | Strip slid over the picture | Pointer at the bottom edge. | yes |
| `17-retracted-line` | Retracted strip line (bottom 20%) | Just after the pointer moves over a picture. | yes |
| `51-viewer-picture` | Picture, nothing playing | Open a picture with no music parked. | yes |
| `52-viewer-picture-zoom` | Picture zoomed in | Viewer, + three times. | yes |
| `53-viewer-picture-tags` | Tags field in the viewer | Viewer, Ctrl+T. | yes |
| `54-viewer-fullscreen` | Picture fullscreen | Viewer, F. F or Esc leaves it. | yes |
| `55-viewer-small` | Picture in a small window | Viewer, window 480 x 560. | yes |
| `56-viewer-video` | Video playing, controls shown | Open a video; move the pointer. | no: The scratch library has no video file (none can be generated without a tool). |
| `57-viewer-video-paused` | Video paused, tracks menu | Space; the audio/subtitle track shortcuts. | no: Same: needs a video. |
| `58-viewer-book` | Comic book (CBZ), first page | Open a .cbz. | yes |
| `59-viewer-book-next` | Book, second page | Right. | yes |
| `60-viewer-book-epub` | EPUB book, reading layout | Open an .epub. | yes |

## Now playing

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `02-stage-wide` | Stage with the Up next rail | Open a song. | yes |
| `03-rail-hover` | Pointer on a rail row (+ shows) | Hover a rail row. | yes |
| `04-rail-queued` | Queue and its divider | Q on a rail row. | yes |
| `05-stage-narrow` | Stage, narrow (no rail) | Window about 1000 wide. | yes |
| `06-stage-small` | Stage, small window | Window 480 x 560. | yes |
| `07-grid-strip` | Grid with the music strip | Esc in the stage: music goes on in the strip. | yes |
| `10-grid-small-strip` | Small window with the strip | Strip, window 480 x 560. | yes |
| `11-stage-paused` | Stage paused | Space. | yes |
| `12-stage-no-cover` | Song without a cover | Open a song with no picture. | yes |
| `13-stage-play-on` | "Play on" mode | Click the divider. | yes |
| `14-rail-drag` | Queued row mid-drag | Drag a queued row. | yes |
| `15-cover-full` | Cover filling the screen | Hover the cover, click its corner button. | yes |
| `16-stage-900dip` | Stage at about 900 DIP | Window 900 DIP wide. | yes |

## Boards

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `72-boards-canvas` | Board view: tabs, canvas with placed files, tray of the files not placed | Click Boards in the sidebar (the first board opens, its query in the search box). | no: Needs a catalog with a board; the scratch library has none, and the Boards row has no keyboard shortcut to click for. |
| `73-boards-empty` | Board view with no boards (New board from this search) | Click Boards in a catalog with no board. | no: Needs the sidebar row's coordinates. |
| `74-boards-selected` | A placed file selected: violet outline and the resize handle, selection bar over the canvas | Click a file on the board. | no: Needs a board. |
| `75-boards-tray-drag` | A tray file dragged onto the canvas (ghost under the pointer) | Press a tray tile and drag up. | no: Needs a board and a mouse drag. |
| `81-boards-media-hover` | Pointer over a placed video or song: round play mark in its middle (none otherwise) | Hover a video or song on the board. | no: Needs a board and the pointer. |
| `82-boards-video-playing` | A video playing inside its tile, pause mark while hovered, cut round files over it | Click the play mark of a video, or select it and press Space. | no: Needs a board, libmpv and the pointer. |
| `83-boards-video-frame` | A video paused on earlier shows that frame on its tile (after a restart too) | Pause a board video, leave the board and come back. | no: Needs a board, libmpv and the pointer. |
| `84-boards-tabs-overflow` | Many boards: tabs fade out at the edge, the + keeps its place beside Export PNG | Make more boards than fit (Ctrl+N), or narrow the window; Ctrl+PgUp/PgDn scroll the board in view into sight. | no: Needs a catalog with many boards. |
| `85-notice-board-query` | Notice "Board search changed - Ctrl+Z to undo" after a sidebar tag click on a board | On a board, click a tag in the sidebar; Ctrl+Z puts the search back. | no: Needs a board. |

## Window

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `61-window-maximized` | Maximized / fullscreen window | Double-click the title bar; F or F11 in the viewer. | no: The script sizes the window itself; maximizing would move it off the scratch size. |
| `62-window-short` | Wide but very short window | Window 1600 x 300. | yes |
| `66-window-f11-grid` | F11 fullscreen in the grid (top strip hidden) | F11 in the grid; F11 or Esc leaves. | no: Covers the whole monitor; the script keeps the window at its scratch size. |
| `67-window-secondary` | Second window with one video (Open in another window) | Right-click a file, Open in another window; Esc or the close button closes just that window. | no: A separate process; the script drives one window. |
