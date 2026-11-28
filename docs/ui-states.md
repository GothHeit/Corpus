# UI states

Every screen and state of the window and how to reach it. Generated from `tools/ui-states.psd1` by
`tools/ui-screenshots.ps1 -WriteDocs`; edit the manifest, not this file. "Shot" says whether `tools/ui-screenshots.ps1`
captures the state (yes) or not yet (the reason is in the last column).

## Library

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `01-grid` | Grid, files added | Ctrl+O, pick a folder, accept the folder tags sheet. | yes |
| `102-info-facts` | Info pane for one file: mono facts line (PNG 900 x 1400, FLAC 24-bit / 96 kHz 4:12, MP4 H.264 1920 x 1080 0:24, PDF 248 pages) and the preview, never larger than the thumbnail | Select a picture, a song, a video and a PDF with Ctrl+I open. | no: Needs the script to open Info with a selection. |
| `103-chips-parent` | Chips of nested tags: the level above dimmed in front (colour / oxblood), no chip for the level alone, underscores as spaces | Select a file tagged colour/oxblood or a folder tag such as Lisboa_trip; see the selection bar and Info. | yes |
| `104-chip-partial-tip` | Tooltip on a partial chip: On 3 of 6 - click to add to all | Select several files with different tags and hover a dashed chip. | no: Needs the pointer. |
| `110-bar-more` | Selection bar whose chips do not fit: they fade at the cut and a +N at the end counts the ones out of sight (click opens Info) | Select files with many tags in a narrow window. | no: Needs many tags in the scratch catalog. |
| `111-bar-notice` | Selection bar keeps its place while a notice shows under the grid; the notice text goes beside it | Select files, add a tag from the bar (the notice shows). | no: Needs a tag edit during the walk. |
| `112-chip-menu` | Chip menu: Show files with this tag, Add to every selected file (partial chip), Take off oxblood, Take off colour, keep oxblood | Right-click a chip in the selection bar or the Info pane. | no: Needs the pointer over a chip. |
| `113-info-tucked` | Info pane opened in a window under 1100 wide: the sidebar tucks away, and comes back when Info closes | Window 960 wide with the sidebar open, select a file, Ctrl+I. | no: Needs the script to open Info with a selection. |
| `130-view-missing` | Missing view: a file no longer on disk | Click Missing in the sidebar, or Ctrl+9. The walk seeds a file that is not there. | yes |
| `131-narrow-all-files` | Narrow window on All files (sidebar folded, no view label) | Ctrl+1, window 480 x 560. | yes |
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
| `29-view-untagged` | Untagged view | Click Untagged in the sidebar, or Ctrl+6. | yes |
| `30-tile-hover` | Pointer over a tile | Move the pointer onto a tile. | yes |
| `31-import-progress` | Import progress bar | Add a big folder; the bar shows while it runs. | no: The scratch library is too small to catch it mid-way. |
| `32-drop-target` | Dragging files over the window | Drag a file from Explorer over the window. | no: Needs a real OLE drag from another program. |
| `64-search-in-text` | Search hit inside a book ("in text" tile) | Ctrl+F, type a word that is only inside an EPUB. | yes |
| `65-details-in-text` | Info pane with the text snippet | Search a word inside a book, select the hit, Ctrl+I. | yes |
| `69-details-notes` | Info pane with the notes of the file's tags (title with the level above dimmed: motif / fang) | Give a tag a note (tag menu, Notes...), select a file with it, Ctrl+I. The walk seeds colour/oxblood with a note. | yes |
| `70-sidebar-inbox` | Inbox row selected (search shows inbox:) | Click Inbox in the sidebar; click it again to clear. The walk seeds an untagged picture. | yes |
| `71-inbox-pasted` | Notice after Ctrl+V of a picture | Copy a picture in a browser, Ctrl+V in the grid; Info pane shows its Source. | no: Needs the clipboard of a real program. |
| `90-view-label-hidden` | Sidebar hidden on another Library row: its name and a small arrow at the left of the search row | Ctrl+2 (Pictures), Ctrl+B; or a window under 560 wide on any row but All files. | yes |
| `92-sidebar-keyboard` | Sidebar with the keyboard in the list: violet ring on a Library row or a tag | Tab to the tag filter, then Up or Down. | no: Needs the script taught the Tab ring. |
| `93-sidebar-excluded` | Excluded tag: rose dash, struck name and, in rose, how many files it hides | Click a tag twice, or select it and press minus. | no: Needs the tag rows' coordinates. |
| `94-sidebar-drop-inbox` | Inbox row glowing in the drop-target oxblood while files are dragged over it | Drag files from Explorer over the Inbox row. | no: Needs a real drag from another program. |
| `96-grid-focus-ring` | Grid with the keyboard on a tile that is not selected: violet ring round the tile | Tab from the search to the grid (nothing selected), then the arrows. | no: Needs the script taught Tab and a keyboard-only capture. |

## Menus

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `33-menu-file` | File context menu on a picture (no Play next or Add to queue) | Select a picture, Menu key (or Shift+F10). | yes |
| `34-menu-song` | Context menu of a song (adds Play next and Add to queue) | Select a song, Menu key. | yes |
| `35-menu-grid` | Menu on the empty grid | Right-click empty space in the grid. | yes |
| `36-menu-sort` | Sort menu | Click the sort button in the title bar. | yes |
| `37-menu-tag` | Tag context menu (Require, Exclude, Clear with the current state ticked; Notes, Export as Markdown, Rename, Delete) | Right-click a tag in the sidebar. | no: Needs the tag rows' coordinates. |
| `76-menu-board-tab` | Board tab menu (Rename Ctrl+F2, Export as PNG Ctrl+E, Delete in rose Ctrl+Del; Export dimmed with nothing placed) | Right-click a board tab. | no: Needs a board. |
| `77-menu-board-file` | Menu of a file on a board (adds Bring to front, Send to back, Take off board) | Right-click a file on the board. | no: Needs a board. |
| `86-menu-board-canvas` | Menu of the empty canvas of a board (New board, Place all, Fit all, Export as PNG; rows that would do nothing dimmed) | Right-click empty room on a board, or the Menu key with no placed file selected. With every file placed, Place all is dimmed. | no: Needs a board. |
| `91-menu-views` | Views menu under the label (Library rows with Ctrl+1..9, the current one ticked) | Click the view label. | no: Needs the label's coordinates. |

## Sheets

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `101-shortcuts-board` | F1 on a board: Boards first, then the other sections (Search words last) | Open a board, F1. | yes |
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
| `79-sheet-board-delete` | Delete board confirm (danger; says Ctrl+Z brings it back) | Right-click a board tab, Delete board, or Ctrl+Del on a board. Esc cancels. | no: Needs a board. |
| `80-notice-board-export` | Save dialog and notice for Export PNG | Export PNG... on the bar, or Ctrl+E. | no: The save dialog is Windows' own window. |
| `95-notes-on-level` | Notes sheet opened on a group level that is no tag yet (it becomes a tag when saved) | Right-click a level such as colour, Notes. | no: Needs nested tags and the tag rows' coordinates. |
| `99-sheet-inbox-rule` | Inbox rule sheet: tags typed with commas, buttons Cancel and Save | Settings, Inbox row (Enter on it from the keyboard). | no: Needs the row's coordinates. |

## Settings

| Id | State | How to reach it | Shot |
|---|---|---|---|
| `105-about-updates` | About card, update rows idle ("Updates", weekly switch off, Check for updates button, note under the card) | Ctrl+comma, scroll to the end. | no: The scrolled-to-the-end capture (48) shows it; a separate one waits for a scroll step in the script. |
| `106-update-available` | Update available: "Corpus 9.9.9 is available", What's new link, Download update button; Settings opened at About | Settings opens here while an update waits. The walk starts a test build with --update-state available. | yes |
| `107-update-downloading` | Update downloading (percent, progress bar, Cancel button) | Download update. The walk uses --update-state downloading. | yes |
| `108-update-ready` | Update ready: Restart to update (kept across restarts) | After the download passed the signature, version and hash checks; also at the next start. The walk uses --update-state ready. | yes |
| `109-update-error` | Update error in plain words, in rose (offline) | Check for updates with no network. The walk uses --update-state error, then End. | yes |
| `110-settings-catalog-folder` | Backup section: Export, Import and the Catalog folder row with its path (opens Explorer) | Ctrl+comma, scroll to Backup. | no: The scrolled-to-the-end capture (48) may show it; a separate one waits for a scroll step in the script. |
| `111-inbox-moved` | Notice after moving the Inbox: Inbox moved to <name> (and how many files stayed behind, if any) | Settings, Inbox folder, pick a folder. | no: The folder picker is Windows' own window. |
| `112-settings-list-open` | A Settings list open (scaling of enlarged pictures), the chosen item lit | Ctrl+comma, Tab to the scaling row, Enter (or click it). | yes |
| `132-update-notice` | Status-bar line "Corpus 9.9.9 is available - Ctrl+, opens Settings" and the dot on the sidebar cog | Once per version, when the weekly check (or a download kept from the last run) finds it. The walk uses --update-state available. | yes |
| `133-update-dot-sidebar-hidden` | Sidebar hidden while an update waits: the violet dot on the sidebar button | Ctrl+B with an update available. | yes |
| `47-settings-top` | Settings, top | Ctrl+comma. | yes |
| `48-settings-bottom` | Settings, scrolled to the end (Help and About) | Ctrl+comma, then the wheel. | yes |
| `49-settings-small` | Settings in a small window | Ctrl+comma in a 480 x 560 window. | yes |
| `50-licenses` | Licenses page | Settings, Help, Licenses row. | no: Needs the script taught Tab to reach the row. |
| `97-settings-focus` | Settings with the keyboard on a row or button: violet ring (Done, a switch, the size slider, a button) | Ctrl+comma, then Tab or Down; Left and Right move the slider. | no: Needs the script taught Tab. |
| `98-settings-inbox` | Inbox section of Settings: the row "Inbox folder" with its path (mono, cut to the folder name when long), the row "Inbox shows files without" with the tags (or "any tag"), and the note | Ctrl+comma, scroll to Inbox. Inbox folder opens a folder picker; the files move along and a notice says where. | no: Needs the script to scroll to the row. |

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
| `87-viewer-video-speeds` | Speed list open over a video (8x at the top, 0.5x by the button, current one marked) | Click the speed button of a video. | no: Same: needs a video. |
| `88-viewer-video-8x` | Video at 8x: button in violet, toast naming the speed | Press ] until 8x. | no: Same: needs a video. |
| `89-viewer-video-toast-long` | Track toast with a long label, cut with an ellipsis inside the viewer | A or C on a video whose track names are long, in a narrow window. | no: Same: needs a video with tracks. |

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
| `120-boards-first-open` | Boards right after starting Corpus, and after a resize: the tab of the board in view underlined and in sight | Start Corpus, click Boards (or Ctrl+8); then make the window narrower. | yes |
| `121-boards-leave` | Leaving Boards: the search box shows what it held before Boards was opened, not the board's query | Type a search, open Boards, then Ctrl+1. | no: Needs a board. |
| `122-boards-tab-step` | Tab on a board: the next placed file in reading order selected, panned into view if it was off the canvas | On a board, Ctrl+F then Tab, then Tab again. | no: Needs a board. |
| `123-notice-board-undo` | Notice "Undone - Deleted board - name" with the board back in its tab, files where they were | Delete a board (Ctrl+Del, Delete), then Ctrl+Z. | no: Needs a board. |
| `72-boards-canvas` | Board view on first open: board bar with the current board in sight, canvas with placed files, tray of the files not placed | Click Boards in the sidebar, or Ctrl+8 (the first board opens, its query in the search box). The walk seeds two boards. | yes |
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
| `67-window-secondary` | Second window with one file (Open in another window): no Inbox, no update rows in its Settings | Right-click a file, Open in another window; Esc or the close button closes just that window. | no: A separate process; the script drives one window. |
