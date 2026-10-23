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
#include "context_menu.hpp"
#include "rename_sheet.hpp"
#include "import_bar.hpp"
#include "viewer.hpp"
#include "settings_page.hpp"
#include "missing_check.hpp"
#include "../../include/folder_import.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
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
//   |  [ SearchBar                     ]  |
//   |-----------+-------------------------|
//   |TagSidebar |  FileGrid               |  the sidebar holds the Library views and the tags, and slides
//   |           |                         |  out of the way when hidden (see ToggleSidebar)
//   |           |-------------------------|
//   |           |  ImportBar              |  only while files are added
//   |           |-------------------------|
//   |           |  TagPanel               |
//   +-----------+-------------------------+
//
// The InfoPane takes a strip on the right, over the full height under the search row, while it is open. The
// ContextMenu is drawn over everything while it is open, and so is the RenameSheet, which dims the window.
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

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HitTestResizeBorder(LPARAM lParam) const;
    RECT FrameInsets(const RECT &windowRect) const;
    void PaintFrame() const;
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
    void ToggleSidebar();
    void StepSidebarSlide();
    void ShowView(LibraryView view);
    void SelectionChanged();
    void OpenSelected();
    void ToggleInfo();
    void OpenContextMenu(POINT pt);
    void RunMenuItem(ContextMenu::Item item);
    void StartRename();
    void HandleRenameAction(RenameSheet::Action action);
    bool RenameFile(const std::string &path, const std::wstring &name, std::wstring &error);
    void DeleteSelectedFiles();
    void OpenFiles(const std::vector<file*> &files);
    void ViewFile(file *f, const std::vector<file*> &among, bool inSelection = false);
    void CloseViewer();
    void OpenSettings();
    void CloseSettings();
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
    void SetQuery(const std::wstring &query);
    void RemoveSelectedFiles();
    void RemoveFromLibrary(const std::vector<file*> &files);
    bool BeginChange();
    void FinishChange();
    void ReportSaveError();
    void OnPaint();
    void OnHover(POINT pt);
    int Scale(int value) const;

    HINSTANCE m_hInstance;
    HWND m_hWnd = nullptr;
    library &m_lib;
    Renderer m_renderer;
    bool m_trackingMouse = false;  // asked Windows for WM_MOUSELEAVE
    bool m_pressedGridButton = false;  // the last press hit "Add folder..." or "Clear filters"
    bool m_closedViewer = false;       // the last press closed the viewer
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
    std::unordered_set<std::string> m_foundSinceCheck;  // paths an import found since a check last ended
    bool m_missingChecked = false;
    bool m_missingPending = false;  // a check ended while a dialog was up; see kMissingChecked

    TitleBar m_titleBar;
    TextField m_searchBar;
    FileGrid m_fileGrid;
    TagPanel m_tagPanel;
    TagSidebar m_sidebar;
    InfoPane m_infoPane;
    ContextMenu m_contextMenu;
    RenameSheet m_rename;
    std::string m_renamePath;  // the file the sheet renames
    ImportBar m_importBar;
    Viewer m_viewer;
    SettingsPage m_settings;

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
};

#endif
