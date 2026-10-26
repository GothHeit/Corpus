#ifndef WINDOWS_HPP
#define WINDOWS_HPP

#include <windows.h>
#include <shellapi.h>
#include "renderer.hpp"
#include "title_bar.hpp"
#include "text_field.hpp"
#include "file_grid.hpp"
#include "tag_panel.hpp"
#include "tag_sidebar.hpp"
#include "info_pane.hpp"
#include "tooltip.hpp"
#include "context_menu.hpp"
#include "rename_sheet.hpp"
#include "tag_sheet.hpp"
#include "folder_tags_sheet.hpp"
#include "confirm_sheet.hpp"
#include "import_bar.hpp"
#include "viewer.hpp"
#include "settings_page.hpp"
#include "missing_check.hpp"
#include "moved_search.hpp"
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
//   |           |-------------------------|
//   |           |  TagPanel               |
//   +-----------+-------------------------+
//
// The InfoPane takes a strip on the right, over the full height under the search row, while it is open. The
// ContextMenu is drawn over everything while it is open, and so are the sheets (RenameSheet, TagSheet,
// FolderTagsSheet, ConfirmSheet; one at a time, see OpenSheet), which dim the window.
//
// The Viewer and the SettingsPage each cover everything under the title row while they are open.
class Window
{
public:
    // Thumbnails are kept on disk in 'thumbnailFolder'.
    // instanceKey marks the window for single_instance.
    Window(library &lib, const std::wstring &thumbnailFolder, std::uint64_t instanceKey);
    ~Window();

    // Runs the message loop until the window is closed.
    void Run();

    // Shows files given on the command line or by a second start in the viewer, without adding them to the library.
    // Files Corpus can't show are ignored. With none, only brings the window to the front.
    void OpenExternal(const std::vector<std::wstring> &files);

private:
    friend LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

    // What the grid is sorted by. Saved between runs (see SetSort), so the values must not change.
    enum class SortKey { kAdded = 0, kName = 1, kSize = 2, kType = 3 };

    // A change Ctrl+Z takes back and Ctrl+Y makes again, kept by paths and tag names: the library's pointers don't
    // survive a reload. A step is tags added or taken off files, files taken out of the library, or a tag renamed.
    struct UndoStep
    {
        struct Links
        {
            std::string tag;
            std::vector<std::string> paths;  // the files the change added the tag to, or took it off
            bool add = true;
        };
        std::wstring label;               // what was done, for the notice: "Tagged 3 files · beach"
        std::vector<Links> links;         // done in order, undone backwards
        std::vector<file_snapshot> removed;
        std::string tagFrom, tagTo;       // a tag renamed, or merged (then 'links' does the merge)
        bool merge = false;
        bool followed = false;            // the merge carried the search's tag over to the target
    };

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HitTestResizeBorder(LPARAM lParam) const;
    void SetMonitorFullscreen(bool fullscreen);
    void FitToMonitor();
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
    void ToggleInfo();
    void OpenContextMenu(POINT pt);
    void RunMenuItem(ContextMenu::Item item);
    void RunMenuItemNow(ContextMenu::Item item);
    void StartRename();
    void HandleRenameAction(Sheet::Action action);
    bool RenameFile(const std::string &path, const std::wstring &name, std::wstring &error);
    void DeleteSelectedFiles();
    void OpenFiles(const std::vector<file*> &files);
    void ViewFile(file *f, const std::vector<file*> &among, bool inSelection = false);
    void CloseViewer();
    void OpenSettings();
    void CloseSettings();
    void SetTileSize(int size);
    void OpenTagSheet();
    Sheet *OpenSheet();
    void CloseSheets();
    void HandleSheetAction(Sheet *sheet, Sheet::Action action);
    void HandleTagSheetAction(Sheet::Action action);
    void StartTagRename(const std::string &name);
    bool RenameTag(std::wstring &error);
    void OfferFolderTags();
    void HandleFolderTagsAction(Sheet::Action action);
    void ShowNotice(const std::wstring &text, bool error = false);
    void Ask(ConfirmSheet::Question question, std::function<void()> then);
    void HandleConfirmAction(Sheet::Action action);
    bool HoldMovedResult();
    void ResumeHeldResults();
    std::vector<file*> FilesAt(const std::vector<std::string> &paths) const;
    void Remember(UndoStep step);
    void Undo(bool redo);
    bool RunStep(const UndoStep &step, bool forward);
    void ApplyLinks(const UndoStep::Links &links, bool add);
    void TakeOutOfLibrary(const std::vector<file*> &files);
    void RenameTagNow(tag *t, const std::string &name);
    bool FollowTagInQuery(const std::string &from, const std::string &to);
    bool SidebarFieldReachable() const;
    void FocusSearch();
    void LeaveSearch(bool toFirstResult);
    void NoticeTagged(int changed, size_t files, const std::vector<std::string> &tags);
    void HandleSettingsAction(SettingsPage::Action action);
    POINT CursorInWindow() const;
    void HandleViewerAction(Viewer::Action action);
    Viewer::Bookmark LastPage(const std::string &path) const;
    void SavePage(const std::string &path, size_t page, size_t count, const std::string &location);
    void OnDropFiles(HDROP drop);
    void ChooseFolders();
    void AddPaths(std::vector<std::string> paths);
    void StartImport(std::vector<std::string> paths);
    void ScanDropped(std::vector<std::string> paths);
    void ShowImportProgress();
    void FinishImport();
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
    void SetQuery(const std::wstring &query);
    void RemoveSelectedFiles();
    void RemoveFromLibrary(const std::vector<file*> &files, bool undoable);
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
    bool m_pressedGridButton = false;  // the last press hit "Add folder..." or "Clear filters"
    bool m_closedOverlay = false;       // the last press closed the viewer or picked a menu row
    bool m_viewerInSelection = false;  // the viewer steps through the selected files, which stay selected
    bool m_monitorFullscreen = false;  // the viewer's fullscreen covers the whole monitor, taskbar included
    RECT m_beforeFullscreen = {};      // where the window was before it covered the monitor

    // The sidebar is open or hidden as the user last left it. It slides between the two: m_sidebarLevel is how
    // much of it shows, 0 to 1.
    bool m_sidebarOpen = true;
    float m_sidebarLevel = 1.0f;
    float m_slideFrom = 1.0f;
    ULONGLONG m_slideStart = 0;

    // The Missing view: the paths found missing by the last check of the disk, which runs on a thread of its own
    // (see CheckMissing). By path, not file: the library may delete files while a check runs.
    MissingCheck m_missingCheck;
    std::unordered_set<std::string> m_missingPaths;
    std::unordered_map<std::string, std::uint64_t> m_sizes;  // the size of every file found on disk by that check
    std::unordered_set<std::string> m_foundSinceCheck;  // paths an import found since a check last ended
    bool m_missingChecked = false;
    bool m_missingPending = false;  // a check ended while a dialog was up; see kMissingChecked
    MovedSearch m_movedSearch;      // "Find moved files..." in the Missing view
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
    TagPanel m_tagPanel;
    TagSidebar m_sidebar;
    InfoPane m_infoPane;
    Tooltip m_tooltip;
    ContextMenu m_contextMenu;
    RenameSheet m_rename;
    std::string m_renamePath;  // the file the sheet renames
    std::string m_renameTag;   // or the tag it renames (then m_renamePath is empty)
    std::string m_menuTag;     // the tag the open context menu is about, when it is the tag menu
    TagSheet m_tagSheet;
    FolderTagsSheet m_folderTags;
    std::vector<FolderTag> m_folderTagOffer;  // the folder names an import suggests, until the user answers
    ConfirmSheet m_confirm;
    std::function<void()> m_confirmed;  // what the confirm sheet's main button does
    std::vector<UndoStep> m_undo;       // the last change at the back
    std::vector<UndoStep> m_redo;       // the last change undone at the back; a new change clears it
    ImportBar m_importBar;
    Viewer m_viewer;
    SettingsPage m_settings;

    // Dragging a tag from the sidebar onto files: the press on a row waits until the pointer moves a little (then it
    // is a drag) or is released (then it is the click it always was).
    std::string m_dragTag;          // the tag pressed; empty when no press is pending
    POINT m_dragStart = {};
    bool m_dragDouble = false;      // the press was the second click of a double-click
    bool m_dragActive = false;      // the pointer moved far enough
    POINT m_dragPoint = {};

    // Dragging the selected files from the grid onto a tag of the sidebar: a press on a selected file waits until the
    // pointer moves a little (then it is a drag) or is released (then it is the click it always was).
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
