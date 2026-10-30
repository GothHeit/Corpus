#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "../../include/library.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include "open_with.hpp"
#include <string>

// Opens a file the way that suits it: a picture or a video in the viewer, where the arrows step through the
// pictures and videos among 'among', anything else in its default program. 'inSelection': 'among' is the selected
// files.
void Window::ViewFile(file *f, const std::vector<file*> &among, bool inSelection)
{
    if (!Viewer::CanShow(f->get_path()))
    {
        OpenFiles({ f });
        return;
    }

    std::vector<std::string> paths;
    size_t index = 0;
    for (const file *other : among)
    {
        if (other == f)
            index = paths.size();
        if (other == f || Viewer::CanShow(other->get_path()))
            paths.push_back(other->get_path());
    }
    if (paths.empty() || paths[index] != f->get_path())
    {
        paths = { f->get_path() };
        index = 0;
    }

    m_searchBar.Blur();
    m_sidebar.Blur();
    // What was lit under the pointer is behind the viewer now.
    OnHover(kNoPointer);
    m_viewerInSelection = inSelection;
    InvalidateRect(m_hWnd, nullptr, FALSE);
    HandleViewerAction(m_viewer.Open(std::move(paths), index));
}

// The settings cover the window like the viewer, from the foot of the sidebar or Ctrl+comma.
void Window::OpenSettings()
{
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_settings.Open();
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// The thumbnail size from the settings slider or Ctrl+plus/minus, remembered between runs.
void Window::SetTileSize(int size)
{
    m_fileGrid.SetTileSize(size);
    m_settings.SetTileSize(m_fileGrid.TileSize());
    SaveTileSize(m_fileGrid.TileSize());
}

void Window::HandleSettingsAction(SettingsPage::Action action)
{
    if (action == SettingsPage::Action::kTileSize)
        SetTileSize(m_settings.TileSize());
    else if (action == SettingsPage::Action::kClose)
        CloseSettings();
    else if (action == SettingsPage::Action::kToggleSidebar)
        ToggleSidebar();
    else if (action == SettingsPage::Action::kStopWatching)
        StopWatching(m_settings.StopWatchingIndex());
    else if (action == SettingsPage::Action::kExport)
        ExportCatalog();
    else if (action == SettingsPage::Action::kImport)
        ImportCatalog();
    else if (action == SettingsPage::Action::kRestore)
        AskRestore(m_settings.RestoreIndex());
    else if (action == SettingsPage::Action::kToggleOpenWith)
    {
        if (open_with::IsOn())
            open_with::TurnOff();
        else if (!open_with::TurnOn())
            MessageBoxW(m_hWnd, strings::kOpenWithFailed, strings::kAppName, MB_OK | MB_ICONERROR);
        m_settings.SetOpenWithOn(open_with::IsOn());
    }
}

// Opens files from outside the catalog in the viewer, where the arrows step through them. They are not added to
// the library; one that is in it keeps its reading progress.
void Window::OpenExternal(const std::vector<std::wstring> &files)
{
    std::vector<std::string> paths;
    for (const std::wstring &file : files)
    {
        std::string path = WideToUtf8(file);
        if (Viewer::CanShow(path) && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES)
            paths.push_back(std::move(path));
    }

    if (IsIconic(m_hWnd))
        ShowWindow(m_hWnd, SW_RESTORE);
    SetForegroundWindow(m_hWnd);
    if (paths.empty())
        return;

    m_contextMenu.Close();
    CloseSheets();
    CloseSettings();
    CloseViewer();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_viewerInSelection = false;
    InvalidateRect(m_hWnd, nullptr, FALSE);
    HandleViewerAction(m_viewer.Open(std::move(paths), 0));
}

void Window::CloseSettings()
{
    if (!m_settings.IsOpen())
        return;
    m_settings.Close();
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
    ResumeHeldResults();
    OfferFolderTags();
}

// Back to the grid, with the file the viewer showed last selected, so stepping through pictures moves the selection.
// Opened among selected files, it keeps them selected and only moves the focus.
void Window::CloseViewer()
{
    if (!m_viewer.IsOpen())
        return;
    std::string path = m_viewer.CurrentPath();
    bool fullscreen = m_viewer.Fullscreen();
    m_viewer.Close();
    if (fullscreen)
        SetMonitorFullscreen(false);
    // Music playing on brings up its strip under the grid.
    LayoutComponents();
    if (m_viewerInSelection)
        m_fileGrid.FocusPath(path);
    else
        m_fileGrid.SelectPath(path);
    SelectionChanged();

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hWnd, &pt);
    OnHover(pt);
    InvalidateRect(m_hWnd, nullptr, FALSE);
    ResumeHeldResults();
    OfferFolderTags();
}

void Window::ReopenViewer()
{
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_viewerInSelection = false;
    InvalidateRect(m_hWnd, nullptr, FALSE);
    HandleViewerAction(m_viewer.Reopen());
}

// Where a book was left, to open it there: the first page for a book never read, or one not in the library.
Viewer::Bookmark Window::LastPage(const std::string &path) const
{
    Viewer::Bookmark bookmark;
    const file *f = m_lib.find_file(path);
    const reading_progress *progress = f ? f->get_progress() : nullptr;
    if (progress)
    {
        bookmark.page = progress->position > 0 ? static_cast<size_t>(progress->position) : 0;
        bookmark.location = progress->location;
    }
    return bookmark;
}

// Keeps the page the viewer reached in a book, with when it was read. Nothing on screen shows it yet, so the
// library doesn't refresh.
void Window::SavePage(const std::string &path, size_t page, size_t count, const std::string &location)
{
    file *f = m_lib.find_file(path);
    if (!f)
        return;
    reading_progress progress;
    progress.position = static_cast<std::int64_t>(page);
    progress.length = static_cast<std::int64_t>(count);
    progress.location = location;
    if (!m_lib.set_progress(f, progress))
        ReportSaveError();
}

void Window::HandleViewerAction(Viewer::Action action)
{
    switch (action)
    {
    case Viewer::Action::kNone:
        break;
    case Viewer::Action::kClose:
        CloseViewer();
        break;
    case Viewer::Action::kFallBack:
    {
        std::string path = m_viewer.CurrentPath();
        CloseViewer();
        OpenWithDefaultProgram(m_hWnd, path);
        break;
    }
    // The tags typed in the viewer's tag row, which stays up for more.
    case Viewer::Action::kAddTag:
    {
        std::vector<std::string> tags = ParseTags(m_lib, m_viewer.TagText());
        m_viewer.ClearTagText();
        std::vector<file*> files = TagSheetFiles();
        if (tags.empty() || files.empty())
            break;
        size_t lacking = FilesLacking(m_lib, files, tags);
        NoticeTagged(ApplyTagEdit({ TagEdit::kAdd, tags }, files), lacking, tags);
        break;
    }
    case Viewer::Action::kReopen:
        ReopenViewer();
        break;
    case Viewer::Action::kStrip:
        LayoutComponents();
        InvalidateRect(m_hWnd, nullptr, FALSE);
        break;
    case Viewer::Action::kRemoveTag:
        ApplyTagEdit({ TagEdit::kRemove, { m_viewer.TagToRemove() } }, TagSheetFiles());
        break;
    case Viewer::Action::kLayout:
    {
        SetMonitorFullscreen(m_viewer.Fullscreen());
        LayoutComponents();
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m_hWnd, &pt);
        OnHover(pt);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        break;
    }
    }
}

// The viewer's fullscreen covers the whole monitor the window is on, taskbar included, and gives the window back
// its place when it ends, maximized or not.
void Window::SetMonitorFullscreen(bool fullscreen)
{
    if (fullscreen == m_monitorFullscreen)
        return;
    MONITORINFO monitor = { sizeof(monitor) };
    if (fullscreen && !GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return;

    // Set first: resizing asks for the frame (WM_NCCALCSIZE), which depends on it.
    m_monitorFullscreen = fullscreen;
    // Without its frame and maximize box for the while: Windows keeps a maximized window that has one where it
    // maximized it, and there is nothing to snap or maximize (Win+Up).
    const LONG frame = WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX;
    LONG style = GetWindowLong(m_hWnd, GWL_STYLE);
    SetWindowLong(m_hWnd, GWL_STYLE, fullscreen ? style & ~frame : style | frame);
    if (fullscreen)
    {
        GetWindowRect(m_hWnd, &m_beforeFullscreen);
        m_titleBar.HideEdgeStrips();
        SetRoundedCorners(m_hWnd, false);
        const RECT &m = monitor.rcMonitor;
        // HWND_TOP: in front of the other windows, so Windows sees a fullscreen program and lowers the taskbar.
        SetWindowPos(m_hWnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED);
    }
    else
    {
        // The monitor it was on may be gone: then it starts over in the middle of the one it is on.
        const RECT &r = m_beforeFullscreen;
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
            SetWindowPos(m_hWnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                SWP_NOZORDER | SWP_FRAMECHANGED);
        else
            CenterOnScreen();
        m_titleBar.FitEdgeStrips();
        SetRoundedCorners(m_hWnd, !m_titleBar.Maximized());
    }
}
