#ifndef WINDOWS_HPP
#define WINDOWS_HPP

#include <windows.h>
#include <filesystem>
#include <shellapi.h>
#include "renderer.hpp"
#include "title_bar.hpp"
#include "text_field.hpp"
#include "file_grid.hpp"
#include "board_view.hpp"
#include "board_export.hpp"
#include "tag_panel.hpp"
#include "tag_sidebar.hpp"
#include "info_pane.hpp"
#include "tooltip.hpp"
#include "context_menu.hpp"
#include "rename_sheet.hpp"
#include "tag_sheet.hpp"
#include "tag_notes_sheet.hpp"
#include "folder_tags_sheet.hpp"
#include "confirm_sheet.hpp"
#include "shortcuts_sheet.hpp"
#include "import_bar.hpp"
#include "viewer.hpp"
#include "media_controls.hpp"
#include "settings_page.hpp"
#include "missing_check.hpp"
#include "moved_search.hpp"
#include "folder_watch.hpp"
#include "audio_scan.hpp"
#include "inbox_capture.hpp"
#include "../../include/folder_import.hpp"
#include "../../include/library.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class library;
class file;
class content_index;
class content_indexer;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// The app window. It owns the renderer and the components, splits the client area between them
// and forwards each Windows message to the component it concerns.
//
//   +-------------------------------------+
//   | o o o  Corpus                       |  TitleBar (both rows)
//   |  [ SearchBar            ] [↓ Sort][+] |  the sort and Add folder buttons are the title bar's
//   |-----------+-------------------------|
//   |TagSidebar |  FileGrid               |  the sidebar holds the Library views and the tags, and slides
//   |           |                         |  out of the way when hidden (see ToggleSidebar)
//   |           |-------------------------|
//   |           |  ImportBar              |  only while files are added, and for a notice after a change
//   +-----------+-------------------------+
//   The TagPanel is not a row: it floats, centered, over the bottom of the FileGrid while files are selected.
//
// The InfoPane takes a strip on the right while open. The ContextMenu and the sheets (one at a time, see OpenSheet)
// are drawn over everything. The Viewer and the SettingsPage cover everything under the title row.
class Window
{
public:
    // Thumbnails are kept on disk in 'thumbnailFolder'.
    // instanceKey marks the window for single_instance. A 'secondary' window is the one "Open in another window" makes:
    // its own process showing one file in the viewer, with no claim on the library's window, the media keys or the
    // main window's settings. Closing the viewer closes the window.
    Window(library &lib, const std::wstring &thumbnailFolder, std::uint64_t instanceKey, bool secondary = false);
    ~Window();

    // Runs the message loop until the window is closed.
    void Run();

    // Shows files given on the command line or by a second start in the viewer, without adding them to the library.
    // Files Corpus can't show are ignored. With none, only brings the window to the front.
    void OpenExternal(const std::vector<std::wstring> &files);

    // The "--library" path Corpus was started with, passed on to the windows it opens; empty for the usual library.
    void SetLibraryArgument(const std::wstring &path) { m_libraryArgument = path; }

private:
    friend LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

    bool m_secondary = false;
    std::wstring m_libraryArgument;

    // What the grid is sorted by. Saved between runs (see SetSort), so the values must not change.
    enum class SortKey { kAdded = 0, kName = 1, kSize = 2, kType = 3 };

    // A change Ctrl+Z takes back and Ctrl+Y makes again, kept by paths and tag names: the library's pointers don't
    // survive a reload. A step is what the core did to the tags (tag_change records, made by the core itself, so the
    // window never works out an outcome twice), or files taken out of the library.
    struct UndoStep
    {
        std::wstring label;               // what was done, for the notice: "Tagged 3 files · beach"
        std::vector<tag_change> changes;  // made in order, taken back in reverse
        std::vector<file_snapshot> removed;
        std::string tagFrom, tagTo;       // a tag renamed, or merged: the search follows it
        bool merge = false;               // the search follows only if it did the first time ('followed')
        bool followed = false;            // the rename carried the search's tag over to the new name
        bool branch = false;              // tagFrom/tagTo are branches: the search follows the tags under them too
        std::vector<std::pair<std::string, std::string>> moved;  // branches moved up when a tag was deleted, as the search followed them
        std::vector<BoardEdit> boardEdits;  // changes to boards: where files lie, or a board's saved search
    };

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    bool OnSetCursor(LPARAM lParam);
    void OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM key, LPARAM lParam);
    bool ShortcutKey(WPARAM key, bool ctrl);
    bool FieldKey(WPARAM key, bool ctrl, bool shift);
    bool FileKey(WPARAM key, bool ctrl, bool shift, bool repeat);
    void OnLButtonDown(POINT pt, WPARAM keys, bool doubleClick);
    bool PressPanes(POINT pt, bool doubleClick);
    void PressGrid(POINT pt, WPARAM keys, bool doubleClick);
    void OnRButtonDown(POINT pt);
    bool OnMouseMove(POINT pt);
    void OnLButtonUp(POINT pt);
    void OnCaptureChanged();
    void OnMouseWheel(POINT pt, int delta);
    void OnTimer(UINT_PTR timer);
    LRESULT HitTest(LPARAM lParam);
    POINT HoverAfterLeave();
    void SetMonitorFullscreen(bool fullscreen);
    void FitToMonitor();
    void CheckColorProfile(bool reread);
    void CenterOnScreen();
    void LayoutComponents();
    void RefreshResults();
    void LibraryChanged();
    void CountViews();
    void CheckMissing();
    void OnMissingChecked();
    bool RoomForSidebar() const;
    bool SidebarButtonShown() const;
    void ToggleSidebar();
    void StepSidebarSlide();
    void ShowView(SidebarView view);
    // Choosing a Library row (click, Ctrl+1..9, keyboard, the view menu): a bare inbox: leaves the search, then the view shows.
    void ChooseView(SidebarView view);
    std::wstring ViewLabelText() const;
    void SyncViewLabel();
    void OpenViewMenu();
    void OpenTagMenu(const std::string &tag, POINT pt);
    void SetTagState(const std::string &tag, TagState state);
    std::unordered_map<std::string, size_t> HiddenByExclusions(const std::wstring &query, SidebarView view);
    bool InView(const file *f, SidebarView view) const;
    void SortFiles(std::vector<file*> &files);
    void RankFiles();
    void SetSort(SortKey key, bool descending);
    void UpdateSortButton();
    void OpenSortMenu();
    void HandleTitleAction(TitleBar::Action action);
    void UpdateBanner();
    void FindMovedFiles();
    void OnMovedFound();
    void MoveSize(const std::string &from, const std::string &to);
    void CopySelected();
    void ShowSelectedInExplorer();
    void DragFilesOut();
    void SelectionChanged();
    void OpenSelected();
    void OpenSelectedInNewWindow();
    void ToggleInfo();
    // The menu of the picture or video in the viewer.
    void OpenViewerMenu(POINT pt);
    void OpenContextMenu(POINT pt);
    void RunMenuItem(ContextMenu::Item item);
    void RunMenuItemNow(ContextMenu::Item item);
    void StartRename();
    void HandleRenameAction(Sheet::Action action);
    void HandleTagNotesAction(Sheet::Action action);
    bool RenameFile(const std::string &path, const std::wstring &name, std::wstring &error);
    void DeleteSelectedFiles();
    void OpenFiles(const std::vector<file*> &files);
    void ViewFile(file *f, const std::vector<file*> &among, bool inSelection = false);
    void CloseViewer();
    // The strip's name: back to the viewer on the music playing.
    void ReopenViewer();
    void OpenSettings();
    void CloseSettings();
    void SetTileSize(int size);
    void OpenTagSheet();
    // What the tag sheet tags: the file the viewer shows, if it is in the library, else the selection.
    std::vector<file*> TagSheetFiles() const;
    void OpenShortcuts();
    Sheet *OpenSheet();
    void CloseSheets();
    void HandleSheetAction(Sheet *sheet, Sheet::Action action);
    void HandleTagSheetAction(Sheet::Action action);
    void StartTagRename(const std::string &name);
    void OpenTagNotes(const std::string &name);
    void ExportTagMarkdown(const std::string &name);
    bool RenameTag(std::wstring &error);
    void OfferFolderTags();
    void HandleFolderTagsAction(Sheet::Action action);
    void TagByFolder(const std::string &folder);
    void ShowNotice(const std::wstring &text, bool error = false);
    void QueueSelected(bool next);
    void Ask(ConfirmSheet::Question question, std::function<void()> then);
    void HandleConfirmAction(Sheet::Action action);
    bool HoldMovedResult();
    void ResumeHeldResults();
    std::vector<file*> FilesAt(const std::vector<std::string> &paths) const;
    void Remember(UndoStep step);
    void Undo(bool redo);
    bool RunStep(const UndoStep &step, bool forward);
    void TakeOutOfLibrary(const std::vector<file*> &files);
    void RenameTagNow(tag *t, const std::string &name);
    void RenameBranchNow(const std::string &branch, const std::string &name);
    std::vector<tag*> BranchTags(const std::string &branch) const;
    void DeleteTag(const std::string &name);
    void DeleteTagNow(const std::string &name);
    void DeleteTagKeepChildren(const std::string &name);
    bool FollowTagInQuery(const std::string &from, const std::string &to, bool branch = false);
    bool SidebarFieldReachable() const;
    void FocusSearch();
    void LeaveSearch(bool toFirstResult);
    void NoticeTagged(int changed, size_t files, const std::vector<std::string> &tags);
    void HandleSettingsAction(SettingsPage::Action action);
public:
    // The full-text index (see content_index.hpp) and its background indexer, made by main on the catalog's file and
    // alive as long as the window. Starts indexing, unless the user turned it off in the settings.
    void SetContent(content_index *index, content_indexer *indexer);
private:
    // Content search: whether it is on, whether results can use it right now, the settings row, and what to index.
    bool ContentSearching() const;
    void IndexContent();
    void UpdateContentStatus();
    void ToggleContent();
    void ClearContentIndex();
    void OnContentProgress();
    void SetContentWords();
public:
    // Where the automatic backups are kept, listed in the settings.
    void SetBackupFolder(std::filesystem::path folder);
    // A backup the user chose to restore: the window closed to let main swap the database file, then start again.
    const std::filesystem::path &PendingRestore() const { return m_pendingRestore; }
private:
    POINT CursorInWindow() const;
    void HandleViewerAction(Viewer::Action action);
    // Tells Windows' media controls what plays.
    void SyncMediaControls();
    Viewer::Bookmark LastPage(const std::string &path) const;
    void SavePage(const std::string &path, size_t page, size_t count, const std::string &location);
    // The Inbox (inbox_capture.hpp): Ctrl+V and drops save what they bring into a watched folder next to the catalog.
    void SetUpInbox();
    void PasteIntoInbox();
    void CaptureIntoInbox(IDataObject *data);
    bool WantsDrop(IDataObject *data, POINT screen);
    void OnOleDrop(IDataObject *data, POINT screen);
    void ChooseFolders();
    void AddPaths(std::vector<std::string> paths);
    void StartImport(std::vector<std::string> paths);
    void ScanDropped(std::vector<std::string> paths);
    void ShowImportProgress();
    void FinishImport();
    void StartWatching();
    // Settings > Playback: choose the libmpv-2.dll.
    void PickLibmpv();
    void RefreshOutputs();
    void PickOutput(bool exclusive, size_t index);
    // Backup: Export/Import catalog in the settings, the list of automatic backups, and Restore.
    void ExportCatalog();
    void ImportCatalog();
    void RefreshBackups();
    void AskRestore(size_t index);
    void WatchedFoldersChanged(std::vector<std::string> known);
    void OnWatchFound();
    // Songs' own tags (title, artist, album) are read in the background for every audio file that has none yet.
    void ScanAudioTags();
    void OnAudioTags();
    void StopWatching(size_t index);
    void CancelImport();
    void StopImport();
    void HideImportBar();
    void ApplyTagEdit(const TagEdit &edit);
    int ApplyTagEdit(const TagEdit &edit, const std::vector<file*> &files);
    bool HandleSidebarPress(POINT pt, bool doubleClick);
    std::vector<file*> DropFilesAt(POINT pt) const;
    void EndTagDrag();
    void EndFileDrag();
    bool Dragging() const { return !m_dragTag.empty() || m_fileDragPending; }
    // 'user': a click or key of the user, which a board records for undo and announces; false for a search that follows
    // a tag change (rename, delete, their undo), which a board saves quietly.
    void SetQuery(const std::wstring &query, bool user = true);
    // ---- Boards (window_board.cpp): the file area becomes the board view while the sidebar's Boards row is shown.
    bool BoardMode() const { return m_sidebar.View() == SidebarView::kBoards; }
    // 'commit': the search text first becomes the query of the board that is shown (a switch of tab does; entering the
    // row, a new board and a deleted one don't, since the text isn't that board's).
    void OpenBoard(board *b, bool commit = true);
    void CommitBoardQuery(bool notify = false, bool record = true);
    void RememberBoardEdit(BoardEdit edit);
    bool RunBoardEdit(const BoardEdit &edit, bool forward, std::int64_t &shown);
    ULONGLONG m_lastBoardEdit = 0;  // when the last board edit was remembered, to merge a held key's steps
    void NewBoard();
    void HandleBoardAction(BoardView::Action action);
    void AfterBoardInput();
    void SyncBoardSelection();
    void PlaceTagPanel();
    void OpenBoardMenu(BoardView::Action action, POINT pt);
    void StartBoardRename(std::int64_t id);
    bool RenameBoardNow(std::wstring &error);
    void AskDeleteBoard(std::int64_t id);
    void ExportBoard();
    void OnBoardExported(bool done);
    void RefreshBoard(const std::vector<file*> &files);
    void BoardsChanged();
    // A video plays in its tile, a song in the strip; both may sound at once, and M mutes the video.
    void PlayBoardVideo(file *f);
    bool ToggleBoardMute();
    void PlayBoardSong(file *f);
    void SyncBoardPlayer();
    void RemoveSelectedFiles();
    void RemoveFromLibrary(const std::vector<file*> &files, bool undoable);
    bool ReloadLibrary();
    bool BeginChange();
    bool FinishChange();
    void ReportSaveError();
    void OnPaint();
    void OnHover(POINT pt);
    void UpdateTooltip(POINT pt);
    int Scale(int value) const;

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    library &m_lib;
    Renderer m_renderer;
    bool m_trackingMouse = false;  // asked Windows for WM_MOUSELEAVE
    bool m_trackingMaximize = false;  // asked Windows for WM_NCMOUSELEAVE, over the maximize button
    bool m_pressedGridButton = false;  // the last press hit "Add folder..." or "Clear filters"
    bool m_closedOverlay = false;       // the last press closed the viewer or picked a menu row
    bool m_viewerInSelection = false;  // the viewer steps through the selected files, which stay selected
    bool m_gridFullscreen = false;     // F11 outside the viewer: the window covers the monitor, top strip hidden
    bool TopHidden() const { return m_gridFullscreen || m_viewer.Fullscreen(); }
    void ToggleGridFullscreen();
    bool m_monitorFullscreen = false;  // the viewer's fullscreen covers the whole monitor, taskbar included
    HMONITOR m_colorMonitor = nullptr;  // the monitor whose color profile pictures are shown in
    RECT m_beforeFullscreen = {};      // where the window was before it covered the monitor

    // The sidebar is open or hidden as the user last left it. It slides between the two: m_sidebarLevel is how
    // much of it shows, 0 to 1.
    bool m_sidebarOpen = true;
    float m_sidebarLevel = 1.0f;
    float m_slideFrom = 1.0f;
    ULONGLONG m_slideStart = 0;
    bool m_sliding = false;

    // The Missing view: the paths found missing by the last check of the disk, which runs on a thread of its own
    // (see CheckMissing). By path, not file: the library may delete files while a check runs.
    MissingCheck m_missingCheck;
    std::unordered_set<std::string> m_missingPaths;
    std::unordered_map<std::string, std::uint64_t> m_sizes;  // the size of every file found on disk by that check
    std::unordered_set<std::string> m_foundSinceCheck;  // paths an import found since a check last ended
    bool m_missingChecked = false;
    bool m_missingPending = false;  // a check ended while a dialog was up; see kMissingChecked
    MovedSearch m_movedSearch;      // "Find moved files..." in the Missing view
    // The watched folders: new files saved in them come in on their own (see StartWatching).
    FolderWatch m_folderWatch;
    AudioScan m_audioScan;
    bool m_watchPending = false;    // a rescan found files while a dialog was up, like m_missingPending
    std::string m_inboxFolder;      // <library folder>\Inbox, watched; empty in a secondary window
    inbox::DropTarget *m_dropTarget = nullptr;  // registered for the window
    std::string m_libraryFolder;    // where the database and the thumbnail cache are: never cataloged
    bool m_movedPending = false;    // a search ended while a dialog or sheet was up, like m_missingPending

    // The grid's order: every file of the library ranked once by the key (see RankFiles), so a search only sorts its
    // results by rank. Ranked again when files come, go or change name or size, or the key changes.
    SortKey m_sortKey = SortKey::kAdded;
    bool m_sortDescending = true;
    std::unordered_map<const file*, size_t> m_sortRank;
    bool m_sortDirty = true;

    TitleBar m_titleBar;
    TextField m_searchBar;
    FileGrid m_fileGrid;
    BoardView m_board;
    BoardExporter m_boardExporter;
    std::string m_lastValidQuery;     // the last search text that was a valid query
    std::int64_t m_boardId = 0;       // the board shown, or the last one (0: none yet)
    std::int64_t m_menuBoard = 0;     // the board the open tab menu is about
    std::int64_t m_renameBoard = 0;   // the board the rename sheet renames (0: it renames a file or a tag)
    TagPanel m_tagPanel;
    TagSidebar m_sidebar;
    InfoPane m_infoPane;
    Tooltip m_tooltip;
    ContextMenu m_contextMenu;
    RenameSheet m_rename;
    std::string m_renamePath;  // the file the sheet renames
    std::string m_renameTag;   // or the tag it renames (then m_renamePath is empty)
    std::string m_menuQueued;  // the queued song of the rail the open context menu is about
    std::string m_menuSong;    // the song of the music stage's rail the open context menu is about
    std::wstring m_viewLabel;  // the Library row named at the left of the search row while the sidebar is hidden
    std::string m_menuTag;     // the tag the open context menu is about, when it is the tag menu
    TagSheet m_tagSheet;
    TagNotesSheet m_tagNotes;
    FolderTagsSheet m_folderTags;
    std::string m_retagRoot;                  // Settings' Tag by folder: the watched folder whose files the open sheet is about
    std::vector<std::string> m_retagFiles;    // and those files
    std::vector<FolderTag> m_folderTagOffer;  // the folder names an import suggests, until the user answers
    std::filesystem::path m_backupFolder;
    std::vector<std::filesystem::path> m_backupFiles;  // the settings' list, by row
    std::filesystem::path m_pendingRestore;
    ShortcutsSheet m_shortcuts;
    ConfirmSheet m_confirm;
    std::function<void()> m_confirmed;  // what the confirm sheet's main button does
    std::function<void()> m_confirmedAlternate;  // what the confirm sheet's second button does
    std::vector<UndoStep> m_undo;       // the last change at the back
    std::vector<UndoStep> m_redo;       // the last change undone at the back; a new change clears it
    ImportBar m_importBar;
    Viewer m_viewer;
    MediaControls m_mediaControls;
    SettingsPage m_settings;

    // Dragging a tag from the sidebar onto files: the press waits for either a small move (a drag) or the release (a click).
    std::string m_dragTag;          // the tag pressed; empty when no press is pending
    POINT m_dragStart = {};
    bool m_dragDouble = false;      // the press was the second click of a double-click
    bool m_dragActive = false;      // the pointer moved far enough
    POINT m_dragPoint = {};

    // Dragging the selected files onto a sidebar tag: same wait as above.
    bool m_fileDragPending = false;
    bool m_fileDragDeferred = false;  // the press was on a file already selected, so its click waits for the release
    bool m_fileDragActive = false;
    POINT m_fileDragStart = {};
    POINT m_fileDragPoint = {};
    // Dragged out of the window, the files go to other programs (see DragFilesOut). A drop of them back onto Corpus,
    // which comes as WM_DROPFILES, is not an import.
    bool m_draggingOut = false;

    // Dropped files and folders are read on a thread of their own (see StartImport). Only the window's thread
    // touches the library.
    std::thread m_importThread;
    std::atomic<bool> m_cancelImport{ false };
    bool m_importDonePending = false;  // the import finished while a dialog was up; see kImportDone

    // Content search. The index and indexer belong to main; the clearing thread is joined before the next one
    // starts and in the destructor.
    content_index *m_contentIndex = nullptr;
    content_indexer *m_indexer = nullptr;
    bool m_contentOn = true;
    bool m_contentProgressPending = false;           // progress came while a dialog was up
    std::thread m_contentClear;
    std::atomic<bool> m_contentClearing{ false };
    std::unordered_set<std::string> m_contentHitPaths;   // the files in the grid found only through their text
    std::vector<std::string> m_contentWords;             // the plain words of the query, for the snippet
    std::vector<std::string> m_queuedDrops;  // dropped while an import runs; imported after it

    // Shared with the import thread, under m_importMutex.
    std::mutex m_importMutex;
    size_t m_importFound = 0;
    std::string m_importFolder;   // the dropped folder being read, empty for a dropped file
    std::string m_importCurrent;  // the last file found
    folder_scan m_importResult;   // everything found, once the thread is done
    std::vector<std::string> m_importUnopened;  // dropped folders that could not be listed
    std::vector<std::string> m_importRoots;     // dropped folders that were read: the roots of the folder-name tags
};

#endif
