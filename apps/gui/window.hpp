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
#include "import_bar.hpp"
#include "../../include/folder_import.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
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
//   |TagSidebar |  FileGrid               |
//   |           |-------------------------|
//   |           |  ImportBar              |  only while files are added
//   |           |-------------------------|
//   |           |  TagPanel               |
//   +-----------+-------------------------+
class Window
{
public:
    explicit Window(library &lib);
    ~Window();

    // Runs the message loop until the window is closed.
    void Run();

private:
    friend LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HitTestResizeBorder(LPARAM lParam) const;
    void CenterOnScreen();
    void LayoutComponents();
    void RefreshResults();
    void SelectionChanged();
    void OpenFiles(const std::vector<file*> &files);
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

    TitleBar m_titleBar;
    TextField m_searchBar;
    FileGrid m_fileGrid;
    TagPanel m_tagPanel;
    TagSidebar m_sidebar;
    ImportBar m_importBar;

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
