# Every screen and state of Corpus's window, for UI review. Read by tools/ui-screenshots.ps1, which walks it, writes one PNG per
# state (named after Id) and a contact sheet. docs/ui-states.md is generated from this file:
#   powershell.exe -NoProfile -File tools\ui-screenshots.ps1 -WriteDocs
#
# Id     the PNG's name; it stays the same between runs, so the diff against the last run can match them.
# Group  the section of the contact sheet and of the doc.
# Title  what the picture shows.
# Reach  how a person gets there (keys, clicks), as the script does it.
# Auto   $true: the script captures it. $false: not captured yet (Why says what is missing); the contact sheet shows a gap.
@{
    States = @(
        # --- Library: the grid and what is around it ---
        @{ Id = '21-empty';               Group = 'Library';   Auto = $true;  Title = 'Empty library';                          Reach = 'First start, nothing added: the drop hint.' }
        @{ Id = '01-grid';                Group = 'Library';   Auto = $true;  Title = 'Grid, files added';                      Reach = 'Ctrl+O, pick a folder, accept the folder tags sheet.' }
        @{ Id = '18-grid-sidebar';        Group = 'Library';   Auto = $true;  Title = 'Grid with the sidebar open';             Reach = 'Ctrl+B (open by default).' }
        @{ Id = '22-grid-sidebar-hidden'; Group = 'Library';   Auto = $true;  Title = 'Grid with the sidebar hidden';           Reach = 'Ctrl+B.' }
        @{ Id = '23-grid-tiles-large';    Group = 'Library';   Auto = $true;  Title = 'Biggest thumbnails';                     Reach = 'Ctrl+plus, several times.' }
        @{ Id = '24-grid-tiles-small';    Group = 'Library';   Auto = $true;  Title = 'Smallest thumbnails';                    Reach = 'Ctrl+minus, several times.' }
        @{ Id = '25-search-no-results';   Group = 'Library';   Auto = $true;  Title = 'Search with no matches';                  Reach = 'Ctrl+F, type a word nothing contains.' }
        @{ Id = '64-search-in-text';      Group = 'Library';   Auto = $true;  Title = 'Search hit inside a book ("in text" tile)'; Reach = 'Ctrl+F, type a word that is only inside an EPUB.' }
        @{ Id = '65-details-in-text';     Group = 'Library';   Auto = $true;  Title = 'Info pane with the text snippet';          Reach = 'Search a word inside a book, select the hit, Ctrl+I.' }
        @{ Id = '69-details-notes';       Group = 'Library';   Auto = $false; Title = 'Info pane with the notes of the file''s tags'; Reach = 'Give a tag a note (tag menu, Notes...), select a file with it, Ctrl+I.'; Why = 'Needs a tag with a note in the scratch library.' }
        @{ Id = '70-sidebar-inbox';       Group = 'Library';   Auto = $false; Title = 'Inbox row selected (search shows inbox:)'; Reach = 'Click Inbox in the sidebar; click it again to clear.'; Why = 'Needs untagged files (the Inbox rule is a setting, default untagged); the scratch library tags them all on import.' }
        @{ Id = '71-inbox-pasted';        Group = 'Library';   Auto = $false; Title = 'Notice after Ctrl+V of a picture';    Reach = 'Copy a picture in a browser, Ctrl+V in the grid; Info pane shows its Source.'; Why = 'Needs the clipboard of a real program.' }
        @{ Id = '26-search-tag-query';    Group = 'Library';   Auto = $true;  Title = 'Search with a {tag} query';               Reach = 'Ctrl+F, type {files} (Shift+[ and Shift+]).' }
        @{ Id = '19-grid-three-selected'; Group = 'Library';   Auto = $true;  Title = '3 files selected, tag bar active';        Reach = 'Click a tile, Shift+click another.' }
        @{ Id = '20-details-song';        Group = 'Library';   Auto = $true;  Title = 'Info pane on a song';                    Reach = 'Select a song, Ctrl+I.' }
        @{ Id = '27-details-picture';     Group = 'Library';   Auto = $true;  Title = 'Info pane on a picture';                 Reach = 'Select a picture, Ctrl+I.' }
        @{ Id = '28-grid-small-window';   Group = 'Library';   Auto = $true;  Title = 'Grid in a small window';                  Reach = 'Window about 480 x 560.' }
        @{ Id = '29-view-untagged';       Group = 'Library';   Auto = $false; Title = 'Untagged / Missing views';               Reach = 'Click Untagged or Missing in the sidebar, or Ctrl+6 / Ctrl+9.'; Why = 'Missing needs a file deleted from disk; Untagged can use Ctrl+6 once the script is run on a PC.' }
        @{ Id = '30-tile-hover';          Group = 'Library';   Auto = $true;  Title = 'Pointer over a tile';                    Reach = 'Move the pointer onto a tile.' }
        @{ Id = '31-import-progress';     Group = 'Library';   Auto = $false; Title = 'Import progress bar';                     Reach = 'Add a big folder; the bar shows while it runs.'; Why = 'The scratch library is too small to catch it mid-way.' }
        @{ Id = '32-drop-target';         Group = 'Library';   Auto = $false; Title = 'Dragging files over the window';          Reach = 'Drag a file from Explorer over the window.'; Why = 'Needs a real OLE drag from another program.' }

        # --- Menus ---
        @{ Id = '33-menu-file';           Group = 'Menus';     Auto = $true;  Title = 'File context menu';                       Reach = 'Select a file, Menu key (or Shift+F10).' }
        @{ Id = '34-menu-song';           Group = 'Menus';     Auto = $true;  Title = 'Song context menu';                       Reach = 'Select a song, Menu key.' }
        @{ Id = '35-menu-grid';           Group = 'Menus';     Auto = $true;  Title = 'Menu on the empty grid';                  Reach = 'Right-click empty space in the grid.' }
        @{ Id = '36-menu-sort';           Group = 'Menus';     Auto = $true;  Title = 'Sort menu';                               Reach = 'Click the sort button in the title bar.' }
        @{ Id = '37-menu-tag';            Group = 'Menus';     Auto = $false; Title = 'Tag context menu (Require, Exclude, Clear with the current state ticked; Notes, Export as Markdown, Rename, Delete)'; Reach = 'Right-click a tag in the sidebar.'; Why = 'Needs the tag rows'' coordinates.' }

        # --- Sheets and dialogs ---
        @{ Id = '68-sheet-tag-notes';     Group = 'Sheets';    Auto = $false; Title = 'Tag notes sheet (note, creator, year, link, taken, refused)'; Reach = 'Right-click a tag in the sidebar, Notes...; or click a note in the info pane.'; Why = 'Needs the tag rows'' coordinates.' }
        @{ Id = '38-sheet-tag';           Group = 'Sheets';    Auto = $true;  Title = 'Add tag sheet';                           Reach = 'Select files, Ctrl+T.' }
        @{ Id = '39-sheet-rename';        Group = 'Sheets';    Auto = $true;  Title = 'Rename sheet';                            Reach = 'Select a file, F2.' }
        @{ Id = '40-sheet-remove';        Group = 'Sheets';    Auto = $true;  Title = 'Remove from library confirm';             Reach = 'Select files, Del.' }
        @{ Id = '41-sheet-delete';        Group = 'Sheets';    Auto = $true;  Title = 'Move to Recycle Bin confirm (danger)';       Reach = 'Select files, Shift+Del. Esc cancels; nothing is deleted.' }
        @{ Id = '42-sheet-folder-tags';   Group = 'Sheets';    Auto = $true;  Title = 'Folder tags sheet after an import';       Reach = 'Ctrl+O, pick a folder with subfolders.' }
        @{ Id = '43-sheet-restore';       Group = 'Sheets';    Auto = $false; Title = 'Restore a backup confirm';                Reach = 'Settings, Restore a backup, Restore.'; Why = 'Would need a backup file in the real catalog folder.' }
        @{ Id = '44-picker-folder';       Group = 'Sheets';    Auto = $false; Title = 'Add folder picker (Windows dialog)';      Reach = 'Ctrl+O.'; Why = 'It is Windows'' own window, not Corpus''s; the window capture does not include it.' }
        @{ Id = '63-sheet-shortcuts';     Group = 'Sheets';    Auto = $true;  Title = 'Keyboard shortcuts sheet';                Reach = 'F1 (F1 or Esc closes).' }
        @{ Id = '45-notice-queued';       Group = 'Sheets';    Auto = $true;  Title = 'Notice: added to the queue';              Reach = 'Select a song, Q.' }
        @{ Id = '46-notice-error';        Group = 'Sheets';    Auto = $true;  Title = 'Notice, error style: no music selected';  Reach = 'Select a picture, Q.' }

        # --- Settings ---
        @{ Id = '47-settings-top';        Group = 'Settings';  Auto = $true;  Title = 'Settings, top';                           Reach = 'Ctrl+comma.' }
        @{ Id = '48-settings-bottom';     Group = 'Settings';  Auto = $true;  Title = 'Settings, scrolled to the end (Help and About)'; Reach = 'Ctrl+comma, then the wheel.' }
        @{ Id = '49-settings-small';      Group = 'Settings';  Auto = $true;  Title = 'Settings in a small window';              Reach = 'Ctrl+comma in a 480 x 560 window.' }
        @{ Id = '50-licenses';            Group = 'Settings';  Auto = $false; Title = 'Licenses page';                           Reach = 'Settings, Help, Licenses row.'; Why = 'Needs the script taught Tab to reach the row.' }

        # --- Viewer: pictures ---
        @{ Id = '08-picture-retracted';   Group = 'Viewer';    Auto = $true;  Title = 'Picture over parked music, strip retracted';  Reach = 'Play a song, Esc, open a picture.' }
        @{ Id = '09-picture-strip-out';   Group = 'Viewer';    Auto = $true;  Title = 'Strip slid over the picture';             Reach = 'Pointer at the bottom edge.' }
        @{ Id = '17-retracted-line';      Group = 'Viewer';    Auto = $true;  Title = 'Retracted strip line (bottom 20%)';       Reach = 'Just after the pointer moves over a picture.' }
        @{ Id = '51-viewer-picture';      Group = 'Viewer';    Auto = $true;  Title = 'Picture, nothing playing';                Reach = 'Open a picture with no music parked.' }
        @{ Id = '52-viewer-picture-zoom'; Group = 'Viewer';    Auto = $true;  Title = 'Picture zoomed in';                       Reach = 'Viewer, + three times.' }
        @{ Id = '53-viewer-picture-tags'; Group = 'Viewer';    Auto = $true;  Title = 'Tags field in the viewer';                Reach = 'Viewer, Ctrl+T.' }
        @{ Id = '54-viewer-fullscreen';   Group = 'Viewer';    Auto = $true;  Title = 'Picture fullscreen';                      Reach = 'Viewer, F. F or Esc leaves it.' }
        @{ Id = '55-viewer-small';        Group = 'Viewer';    Auto = $true;  Title = 'Picture in a small window';               Reach = 'Viewer, window 480 x 560.' }
        @{ Id = '56-viewer-video';        Group = 'Viewer';    Auto = $false; Title = 'Video playing, controls shown';           Reach = 'Open a video; move the pointer.'; Why = 'The scratch library has no video file (none can be generated without a tool).' }
        @{ Id = '57-viewer-video-paused'; Group = 'Viewer';    Auto = $false; Title = 'Video paused, tracks menu';               Reach = 'Space; the audio/subtitle track shortcuts.'; Why = 'Same: needs a video.' }
        @{ Id = '58-viewer-book';         Group = 'Viewer';    Auto = $true;  Title = 'Comic book (CBZ), first page';            Reach = 'Open a .cbz.' }
        @{ Id = '59-viewer-book-next';    Group = 'Viewer';    Auto = $true;  Title = 'Book, second page';                       Reach = 'Right.' }
        @{ Id = '60-viewer-book-epub';    Group = 'Viewer';    Auto = $true;  Title = 'EPUB book, reading layout';               Reach = 'Open an .epub.' }

        # --- Now playing (music) ---
        @{ Id = '02-stage-wide';          Group = 'Now playing'; Auto = $true; Title = 'Stage with the Up next rail';            Reach = 'Open a song.' }
        @{ Id = '03-rail-hover';          Group = 'Now playing'; Auto = $true; Title = 'Pointer on a rail row (+ shows)';        Reach = 'Hover a rail row.' }
        @{ Id = '04-rail-queued';         Group = 'Now playing'; Auto = $true; Title = 'Queue and its divider';                  Reach = 'Q on a rail row.' }
        @{ Id = '14-rail-drag';           Group = 'Now playing'; Auto = $true; Title = 'Queued row mid-drag';                    Reach = 'Drag a queued row.' }
        @{ Id = '13-stage-play-on';       Group = 'Now playing'; Auto = $true; Title = '"Play on" mode';                         Reach = 'Click the divider.' }
        @{ Id = '11-stage-paused';        Group = 'Now playing'; Auto = $true; Title = 'Stage paused';                           Reach = 'Space.' }
        @{ Id = '12-stage-no-cover';      Group = 'Now playing'; Auto = $true; Title = 'Song without a cover';                   Reach = 'Open a song with no picture.' }
        @{ Id = '15-cover-full';          Group = 'Now playing'; Auto = $true; Title = 'Cover filling the screen';               Reach = 'Hover the cover, click its corner button.' }
        @{ Id = '05-stage-narrow';        Group = 'Now playing'; Auto = $true; Title = 'Stage, narrow (no rail)';                Reach = 'Window about 1000 wide.' }
        @{ Id = '16-stage-900dip';        Group = 'Now playing'; Auto = $true; Title = 'Stage at about 900 DIP';                 Reach = 'Window 900 DIP wide.' }
        @{ Id = '06-stage-small';         Group = 'Now playing'; Auto = $true; Title = 'Stage, small window';                    Reach = 'Window 480 x 560.' }
        @{ Id = '07-grid-strip';          Group = 'Now playing'; Auto = $true; Title = 'Grid with the music strip';              Reach = 'Esc in the stage: music goes on in the strip.' }
        @{ Id = '10-grid-small-strip';    Group = 'Now playing'; Auto = $true; Title = 'Small window with the strip';            Reach = 'Strip, window 480 x 560.' }

        # --- Boards ---
        @{ Id = '72-boards-canvas';       Group = 'Boards';    Auto = $false; Title = 'Board view: tabs, canvas with placed files, tray of the files not placed'; Reach = 'Click Boards in the sidebar (the first board opens, its query in the search box).'; Why = 'Needs a catalog with a board; the scratch library has none, and the Boards row has no keyboard shortcut to click for.' }
        @{ Id = '73-boards-empty';        Group = 'Boards';    Auto = $false; Title = 'Board view with no boards (New board from this search)'; Reach = 'Click Boards in a catalog with no board.'; Why = 'Needs the sidebar row''s coordinates.' }
        @{ Id = '74-boards-selected';     Group = 'Boards';    Auto = $false; Title = 'A placed file selected: violet outline and the resize handle, selection bar over the canvas'; Reach = 'Click a file on the board.'; Why = 'Needs a board.' }
        @{ Id = '75-boards-tray-drag';    Group = 'Boards';    Auto = $false; Title = 'A tray file dragged onto the canvas (ghost under the pointer)'; Reach = 'Press a tray tile and drag up.'; Why = 'Needs a board and a mouse drag.' }
        @{ Id = '81-boards-media-hover';  Group = 'Boards';    Auto = $false; Title = 'Pointer over a placed video or song: round play mark in its middle (none otherwise)'; Reach = 'Hover a video or song on the board.'; Why = 'Needs a board and the pointer.' }
        @{ Id = '82-boards-video-playing'; Group = 'Boards';   Auto = $false; Title = 'A video playing inside its tile, pause mark while hovered, cut round files over it'; Reach = 'Click the play mark of a video, or select it and press Space.'; Why = 'Needs a board, libmpv and the pointer.' }
        @{ Id = '83-boards-video-frame';  Group = 'Boards';    Auto = $false; Title = 'A video paused on earlier shows that frame on its tile (after a restart too)'; Reach = 'Pause a board video, leave the board and come back.'; Why = 'Needs a board, libmpv and the pointer.' }
        @{ Id = '84-boards-tabs-overflow'; Group = 'Boards';   Auto = $false; Title = 'Many boards: tabs fade out at the edge, the + keeps its place beside Export PNG'; Reach = 'Make more boards than fit (Ctrl+N), or narrow the window; Ctrl+PgUp/PgDn scroll the board in view into sight.'; Why = 'Needs a catalog with many boards.' }
        @{ Id = '85-notice-board-query';  Group = 'Boards';    Auto = $false; Title = 'Notice "Board search changed - Ctrl+Z to undo" after a sidebar tag click on a board'; Reach = 'On a board, click a tag in the sidebar; Ctrl+Z puts the search back.'; Why = 'Needs a board.' }
        @{ Id = '86-menu-board-canvas';   Group = 'Menus';     Auto = $false; Title = 'Menu of the empty canvas of a board (New board, Place all, Fit all, Export as PNG)'; Reach = 'Right-click empty room on a board.'; Why = 'Needs a board.' }
        @{ Id = '76-menu-board-tab';      Group = 'Menus';     Auto = $false; Title = 'Board tab menu (Rename, Export as PNG, Delete in rose)'; Reach = 'Right-click a board tab.'; Why = 'Needs a board.' }
        @{ Id = '77-menu-board-file';     Group = 'Menus';     Auto = $false; Title = 'Menu of a file on a board (adds Bring to front, Send to back, Take off board)'; Reach = 'Right-click a file on the board.'; Why = 'Needs a board.' }
        @{ Id = '78-sheet-board-rename';  Group = 'Sheets';    Auto = $false; Title = 'Rename board sheet'; Reach = 'Right-click a board tab, Rename board.'; Why = 'Needs a board.' }
        @{ Id = '79-sheet-board-delete';  Group = 'Sheets';    Auto = $false; Title = 'Delete board confirm (danger)'; Reach = 'Right-click a board tab, Delete board. Esc cancels.'; Why = 'Needs a board.' }
        @{ Id = '80-notice-board-export'; Group = 'Sheets';    Auto = $false; Title = 'Save dialog and notice for Export PNG'; Reach = 'Export PNG... on the bar, or Ctrl+E.'; Why = 'The save dialog is Windows'' own window.' }

        # --- Sidebar and views (2026-11-10, sidebar-views) ---
        @{ Id = '90-view-label-hidden';   Group = 'Library';   Auto = $false; Title = 'Sidebar hidden on another Library row: its name and a small arrow at the left of the search row'; Reach = 'Ctrl+2 (Pictures), Ctrl+B; or a window under 560 wide on any row but All files.'; Why = 'Needs a PC run of the script with the new Ctrl+N keys.' }
        @{ Id = '91-menu-views';          Group = 'Menus';     Auto = $false; Title = 'Views menu under the label (Library rows with Ctrl+1..9, the current one ticked)'; Reach = 'Click the view label.'; Why = 'Needs the label''s coordinates.' }
        @{ Id = '92-sidebar-keyboard';    Group = 'Library';   Auto = $false; Title = 'Sidebar with the keyboard in the list: violet ring on a Library row or a tag'; Reach = 'Tab to the tag filter, then Up or Down.'; Why = 'Needs the script taught the Tab ring.' }
        @{ Id = '93-sidebar-excluded';    Group = 'Library';   Auto = $false; Title = 'Excluded tag: rose dash, struck name and, in rose, how many files it hides'; Reach = 'Click a tag twice, or select it and press minus.'; Why = 'Needs the tag rows'' coordinates.' }
        @{ Id = '94-sidebar-drop-inbox';  Group = 'Library';   Auto = $false; Title = 'Inbox row glowing in the drop-target oxblood while files are dragged over it'; Reach = 'Drag files from Explorer over the Inbox row.'; Why = 'Needs a real drag from another program.' }
        @{ Id = '95-notes-on-level';      Group = 'Sheets';    Auto = $false; Title = 'Notes sheet opened on a group level that is no tag yet (it becomes a tag when saved)'; Reach = 'Right-click a level such as colour, Notes.'; Why = 'Needs nested tags and the tag rows'' coordinates.' }

        # --- Keyboard reach and focus (2026-11-10) ---
        @{ Id = '96-grid-focus-ring'; Group = 'Library';   Auto = $false; Title = 'Grid with the keyboard on a tile that is not selected: violet ring round the tile'; Reach = 'Tab from the search to the grid (nothing selected), then the arrows.'; Why = 'Needs the script taught Tab and a keyboard-only capture.' }
        @{ Id = '97-settings-focus';  Group = 'Settings';  Auto = $false; Title = 'Settings with the keyboard on a row or button: violet ring (Done, a switch, the size slider, a button)'; Reach = 'Ctrl+comma, then Tab or Down; Left and Right move the slider.'; Why = 'Needs the script taught Tab.' }
        @{ Id = '98-settings-inbox';  Group = 'Settings';  Auto = $false; Title = 'Inbox section of Settings: the row "Inbox shows files without" with the tags (or "any tag"), and its note'; Reach = 'Ctrl+comma, scroll to Inbox.'; Why = 'Needs the script to scroll to the row.' }
        @{ Id = '99-sheet-inbox-rule'; Group = 'Sheets';    Auto = $false; Title = 'Inbox rule sheet: tags typed with commas, buttons Cancel and Save'; Reach = 'Settings, Inbox row (Enter on it from the keyboard).'; Why = 'Needs the row''s coordinates.' }
        @{ Id = '101-shortcuts-board'; Group = 'Sheets';    Auto = $false; Title = 'F1 on a board: Boards first, then the other sections (Search words last)'; Reach = 'Open a board, F1.'; Why = 'Needs a board.' }

        # --- Info pane and chips (2026-11-10) ---
        @{ Id = '102-info-facts';         Group = 'Library';   Auto = $false; Title = 'Info pane for one file: mono facts line (PNG 900 x 1400, FLAC 24-bit / 96 kHz 4:12, MP4 1920 x 1080 0:24) and the preview'; Reach = 'Select a picture, a song and a video with Ctrl+I open.'; Why = 'Needs the script to open Info with a selection.' }
        @{ Id = '103-chips-parent';       Group = 'Library';   Auto = $false; Title = 'Chips of nested tags: the level above dimmed in front (colour / oxblood), no chip for the level alone, underscores as spaces'; Reach = 'Select a file tagged colour/oxblood or a folder tag such as Lisboa_trip; see the selection bar and Info.'; Why = 'Needs nested tags in the scratch catalog.' }
        @{ Id = '104-chip-partial-tip';   Group = 'Library';   Auto = $false; Title = 'Tooltip on a partial chip: On 3 of 6 - click to add to all'; Reach = 'Select several files with different tags and hover a dashed chip.'; Why = 'Needs the pointer.' }

        # --- Window ---
        @{ Id = '61-window-maximized';    Group = 'Window';    Auto = $false; Title = 'Maximized / fullscreen window';           Reach = 'Double-click the title bar; F or F11 in the viewer.'; Why = 'The script sizes the window itself; maximizing would move it off the scratch size.' }
        @{ Id = '66-window-f11-grid';     Group = 'Window';    Auto = $false; Title = 'F11 fullscreen in the grid (top strip hidden)'; Reach = 'F11 in the grid; F11 or Esc leaves.'; Why = 'Covers the whole monitor; the script keeps the window at its scratch size.' }
        @{ Id = '67-window-secondary';    Group = 'Window';    Auto = $false; Title = 'Second window with one video (Open in another window)'; Reach = 'Right-click a file, Open in another window; Esc or the close button closes just that window.'; Why = 'A separate process; the script drives one window.' }
        @{ Id = '62-window-short';        Group = 'Window';    Auto = $true;  Title = 'Wide but very short window';              Reach = 'Window 1600 x 300.' }
    )
}
