#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif


#include "window.hpp"
#include "../../include/library.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include "../../include/database.hpp"
#include "../../include/saving.hpp"
#include "../../include/file_identity.hpp"
#include "resource.h"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <unordered_set>
#pragma comment(lib, "dwmapi.lib")

static const wchar_t *kClassName = L"Kafkas Window Class";

// Opening more files than this at once asks first, like Explorer does.
static const size_t kOpenPromptThreshold = 15;

// Where the pointer is said to be once it has left the window: outside every component.
static const POINT kNoPointer = { -32768, -32768 };

// Posted by the import thread when it is done.
static const UINT kImportDone = WM_APP + 1;
// Where the sidebar being open or hidden is remembered between runs, under HKEY_CURRENT_USER: nothing is written
// next to the .exe.
static const wchar_t *kSettingsKey = L"Software\\Corpus";
static const wchar_t *kSidebarHiddenValue = L"SidebarHidden";

// While an import runs, the import bar is redrawn this often: smooth for the moving bar, or just enough to keep
// the count current when Windows animations are off. The result then stays up for a few seconds.
static const UINT kAnimationMs = 33;
static const UINT kCountMs = 250;
static const UINT kResultMs = 4000;

// Why library::search rejected a query, for the message under the search bar. Empty for a valid query.
static const wchar_t *QueryError(SearchError error)
{
    switch (error)
    {
    case SearchError::UnclosedBrace: return strings::kUnclosedBrace;
    case SearchError::BadExpression: return strings::kInvalidTags;
    case SearchError::None:          break;
    }
    return L"";
}

static bool LoadSidebarHidden()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kSidebarHiddenValue, RRF_RT_REG_DWORD, nullptr, &value,
        &size) == ERROR_SUCCESS && value != 0;
}

// Best effort: if it can't be saved, the sidebar just opens as it was before next time.
static void SaveSidebarHidden(bool hidden)
{
    DWORD value = hidden ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kSidebarHiddenValue, REG_DWORD, &value, sizeof(value));
}

static std::wstring FileCount(size_t count)
{
    return count == 1 ? strings::kOneFile : FormatCount(count) + L" " + strings::kFiles;
}

// The last part of a path: "Pictures" for C:\Users\me\Pictures.
static std::wstring FolderName(const std::string &path)
{
    std::wstring wide = Utf8ToWide(path);
    while (wide.size() > 1 && (wide.back() == L'\\' || wide.back() == L'/'))
        wide.pop_back();
    size_t slash = wide.find_last_of(L"\\/");
    return slash == std::wstring::npos || slash + 1 == wide.size() ? wide : wide.substr(slash + 1);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE)
    {
        const CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT*>(lParam);
        Window *window = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->m_hWnd = hWnd;
    }

    Window *window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (window)
        return window->HandleMessage(uMsg, wParam, lParam);

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
};

Window::Window(library &lib, const std::wstring &thumbnailFolder)
    : m_hInstance(GetModuleHandle(nullptr)), m_lib(lib), m_searchBar(strings::kSearchPlaceholder), m_tagPanel(lib), m_sidebar(lib)
{
    m_searchBar.SetSearchIcon(true);
    m_sidebarOpen = !LoadSidebarHidden();
    m_sidebarLevel = m_slideFrom = m_sidebarOpen ? 1.0f : 0.0f;

    WNDCLASS wndClass = {};
    // CS_DBLCLKS: Windows only sends WM_LBUTTONDBLCLK to windows that ask for it.
    wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wndClass.lpszClassName = kClassName;
    wndClass.hInstance = m_hInstance;
    wndClass.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = nullptr;  // OnPaint draws everything
    wndClass.lpfnWndProc = WindowProc;

    RegisterClass(&wndClass);

    // WS_POPUP: no native frame or caption; TitleBar draws and handles our own.
    // WS_CLIPCHILDREN: painting leaves the video's window alone.
    // WS_EX_ACCEPTFILES: files dragged in from Explorer arrive as WM_DROPFILES.
    m_hWnd = CreateWindowEx(
        WS_EX_ACCEPTFILES,
        kClassName,
        strings::kAppName,
        WS_POPUP | WS_CLIPCHILDREN,
        250, 250,
        640, 480,
        NULL,
        NULL,
        m_hInstance,
        this
    );

    SetRoundedCorners(m_hWnd, true);

    if (!m_renderer.Init(m_hWnd))
    {
        MessageBoxW(m_hWnd, strings::kNoDirect2D, strings::kAppName, MB_OK | MB_ICONERROR);
        DestroyWindow(m_hWnd);
        return;
    }

    m_fileGrid.StartThumbnails(m_hWnd, thumbnailFolder);
    m_viewer.Start(m_hWnd);
    CenterOnScreen();
    LayoutComponents();
    CountViews();
    RefreshResults();
    CheckMissing();

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    SetFocus(m_hWnd);
}

Window::~Window()
{
    StopImport();
    UnregisterClass(kClassName, m_hInstance);
}

// Blocks in GetMessage until there is something to handle, so an idle window uses no CPU.
void Window::Run()
{
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0u, 0u) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

int Window::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

// Gives the new window its starting size, at the scale of its monitor (only known once it exists), and puts it in
// the middle of the screen, made smaller if the screen is.
void Window::CenterOnScreen()
{
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT &work = monitor.rcWork;
    int width = std::min<int>(Scale(theme::kStartWidth), work.right - work.left);
    int height = std::min<int>(Scale(theme::kStartHeight), work.bottom - work.top);
    SetWindowPos(m_hWnd, nullptr, work.left + (work.right - work.left - width) / 2,
        work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Splits the client area: the title and search rows on top, the sidebar on the left, and on the right the tag
// strip at the bottom and the grid filling the rest. A narrow window leaves the sidebar out, and so does the user
// by hiding it. While it slides, the grid gives it the room it shows and it moves in from the left at full width.
void Window::LayoutComponents()
{
    RECT client;
    GetClientRect(m_hWnd, &client);

    const int margin = Scale(theme::kPageMargin);
    const int titleRow = Scale(theme::kTitleRowHeight);
    const int searchRow = Scale(theme::kSearchRowHeight);

    RECT topBlock = { 0, 0, client.right, titleRow + searchRow };

    int searchTop = titleRow + (searchRow - Scale(theme::kSearchHeight)) / 2;
    RECT searchBar = { margin, searchTop, client.right - margin, searchTop + Scale(theme::kSearchHeight) };

    int contentTop = topBlock.bottom + Scale(theme::kContentGap);
    int fullWidth = Scale(theme::kSidebarWidth);
    int sidebarWidth = RoomForSidebar() ? static_cast<int>(fullWidth * m_sidebarLevel + 0.5f) : 0;
    RECT sidebar = { sidebarWidth > 0 ? sidebarWidth - fullWidth : 0, contentTop, sidebarWidth, client.bottom };
    if (sidebar.bottom < sidebar.top)
        sidebar.bottom = sidebar.top;

    RECT tagPanel = { sidebar.right, client.bottom - Scale(theme::kTagStripHeight), client.right, client.bottom };

    // The import bar takes room from the grid only while it is shown.
    RECT importBar = { sidebar.right, tagPanel.top - (m_importBar.Visible() ? m_importBar.Height() : 0), client.right, tagPanel.top };

    RECT fileGrid = { sidebar.right, contentTop, client.right, importBar.top };
    if (fileGrid.bottom < fileGrid.top)
        fileGrid.bottom = fileGrid.top;

    m_titleBar.SetBounds(m_hWnd, topBlock);
    m_titleBar.ShowSidebarButton(RoomForSidebar());
    m_searchBar.SetBounds(m_hWnd, m_renderer, searchBar);
    m_fileGrid.SetBounds(m_hWnd, m_renderer, fileGrid);
    m_tagPanel.SetBounds(m_hWnd, m_renderer, tagPanel);
    m_sidebar.SetBounds(m_hWnd, m_renderer, sidebar);
    m_importBar.SetBounds(m_hWnd, importBar);

    RECT viewer = { 0, m_viewer.Fullscreen() ? 0 : titleRow, client.right, client.bottom };
    if (viewer.bottom < viewer.top)
        viewer.bottom = viewer.top;
    m_viewer.SetBounds(m_hWnd, m_renderer, viewer);
}

bool Window::RoomForSidebar() const
{
    RECT client;
    GetClientRect(m_hWnd, &client);
    return client.right >= Scale(theme::kSidebarMinWindow);
}

// Hides the sidebar, or brings it back, as the button in the title bar and Ctrl+B do. It slides, unless Windows
// animations are off, and stays as left on the next run.
void Window::ToggleSidebar()
{
    if (!RoomForSidebar())
        return;
    m_sidebarOpen = !m_sidebarOpen;
    SaveSidebarHidden(!m_sidebarOpen);
    // The keyboard can't stay in a field that is going away.
    if (!m_sidebarOpen)
        m_sidebar.Blur();

    m_slideFrom = m_sidebarLevel;
    m_slideStart = GetTickCount64();
    if (AnimationsEnabled())
        SetTimer(m_hWnd, kSidebarSlideTimer, theme::kFrameMs, nullptr);
    else
        m_slideStart = 0;
    StepSidebarSlide();
}

// One frame of the slide: fast at first, slowing down as it lands. A slide turned back halfway takes the time
// of the way left. The timer stops once it is there.
void Window::StepSidebarSlide()
{
    float target = m_sidebarOpen ? 1.0f : 0.0f;
    float distance = std::abs(target - m_slideFrom);
    float duration = theme::kPaneMs * distance;
    float progress = m_slideStart && duration > 0.0f ? (GetTickCount64() - m_slideStart) / duration : 1.0f;
    if (progress >= 1.0f)
    {
        progress = 1.0f;
        KillTimer(m_hWnd, kSidebarSlideTimer);
    }
    float eased = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
    m_sidebarLevel = m_slideFrom + (target - m_slideFrom) * eased;

    LayoutComponents();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Runs the query typed in the search bar in the Library view picked in the sidebar and shows the result in the grid.
// An invalid query marks the search bar and keeps the last valid result on screen.
void Window::RefreshResults()
{
    // Missing is the files of the last check of the disk, which the window keeps rather than the library.
    LibraryView view = m_sidebar.View();
    SearchResult result = m_lib.search(WideToUtf8(m_searchBar.Query()), view == LibraryView::Missing ? LibraryView::All : view);
    if (view == LibraryView::Missing)
    {
        result.files.erase(std::remove_if(result.files.begin(), result.files.end(),
            [this](const file *f) { return !m_missingPaths.count(f->get_path()); }), result.files.end());
    }
    m_searchBar.SetError(QueryError(result.error));
    if (result.valid())
        m_fileGrid.SetFiles(result.files);
    m_fileGrid.SetEmpty(m_lib.get_files().empty() ? FileGrid::Empty::kLibrary : FileGrid::Empty::kNoMatches);
    // The grid keeps the last valid results, and lets go of them before files are deleted, so they are safe to count.
    m_sidebar.Update(m_searchBar.Query(), m_fileGrid.Files());
    // The selection may have lost the files that left the view, and the tags of the files may have changed.
    SelectionChanged();
}

// Replaces the search text, as clicking a tag in the sidebar or "Clear filters" does, and shows its results.
void Window::SetQuery(const std::wstring &query)
{
    m_searchBar.SetText(query);
    RefreshResults();
}

// The files or their tags changed: the sidebar counts its views again, which typing in the search doesn't need.
void Window::LibraryChanged()
{
    CountViews();
    RefreshResults();
}

// The Missing count is that of the files the last check found, and unknown until the first one ends.
void Window::CountViews()
{
    std::optional<size_t> missing;
    if (m_missingChecked)
    {
        size_t count = 0;
        if (!m_missingPaths.empty())
        {
            for (const file *f : m_lib.get_files())
                count += m_missingPaths.count(f->get_path());
        }
        missing = count;
    }
    m_sidebar.CountViews(missing);
}

// Checks the disk for every file of the library, on a thread: when the window opens, each time Missing is picked,
// and each time the window comes back to the front while it shows, so files deleted or put back in the meantime
// are counted. Until it ends, Missing shows what the check before found.
void Window::CheckMissing()
{
    std::vector<std::string> paths;
    paths.reserve(m_lib.get_files().size());
    for (const file *f : m_lib.get_files())
        paths.push_back(f->get_path());
    m_missingCheck.Start(m_hWnd, std::move(paths));
}

void Window::OnMissingChecked()
{
    if (!m_missingCheck.Take(m_missingPaths))
        return;
    m_missingChecked = true;
    CountViews();
    if (m_sidebar.View() == LibraryView::Missing)
        RefreshResults();
}

// Shows a Library view, Missing as of the last check of the disk, while a new one runs.
void Window::ShowView(LibraryView view)
{
    if (view == LibraryView::Missing)
        CheckMissing();
    m_sidebar.SetView(view);
    RefreshResults();
}

void Window::SelectionChanged()
{
    m_tagPanel.SetSelection(m_fileGrid.SelectedFiles());
}

// Opens the file with the program Windows associates with it (an .html in the browser, a .pdf in the PDF reader,
// ...). Windows itself reports missing files and asks which program to use when there is none.
static void OpenWithDefaultProgram(HWND hWnd, const std::string &path)
{
    std::wstring wide = Utf8ToWide(path);

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = hWnd;     // owner of any dialog Windows shows
    info.lpFile = wide.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

// Opens each file in its default program.
void Window::OpenFiles(const std::vector<file*> &files)
{
    if (files.size() > kOpenPromptThreshold)
    {
        std::wstring prompt = strings::kOpenBefore + FormatCount(files.size()) + strings::kOpenAfter;
        if (MessageBoxW(m_hWnd, prompt.c_str(), strings::kAppName, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
    }

    for (const file *f : files)
        OpenWithDefaultProgram(m_hWnd, f->get_path());
}

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
    m_tagPanel.Blur();
    m_sidebar.Blur();
    // What was lit under the pointer is behind the viewer now.
    OnHover(kNoPointer);
    m_viewerInSelection = inSelection;
    InvalidateRect(m_hWnd, nullptr, FALSE);
    HandleViewerAction(m_viewer.Open(std::move(paths), index));
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
    {
        SetMonitorFullscreen(false);
        LayoutComponents();
    }
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
    case Viewer::Action::kOpenExternally:
        OpenWithDefaultProgram(m_hWnd, m_viewer.CurrentPath());
        break;
    case Viewer::Action::kFallBack:
    {
        std::string path = m_viewer.CurrentPath();
        CloseViewer();
        OpenWithDefaultProgram(m_hWnd, path);
        break;
    }
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

// Adds the files and folders dragged in from Explorer to the library, with everything inside the folders. Only
// their paths are stored: nothing is copied or moved.
void Window::OnDropFiles(HDROP drop)
{
    std::vector<std::string> paths;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; i++)
    {
        UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(drop, i, &path[0], length + 1);
        paths.push_back(WideToUtf8(path));
    }
    DragFinish(drop);
    AddPaths(std::move(paths));
}

// "Add folder..." under an empty library: the folders picked are added as if they had been dropped.
void Window::ChooseFolders()
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))))
        return;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM);
    ComPtr<IShellItemArray> items;
    if (dialog->Show(m_hWnd) != S_OK || FAILED(dialog->GetResults(items.GetAddressOf())))
        return;

    std::vector<std::string> paths;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; i++)
    {
        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, item.GetAddressOf())) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        {
            paths.push_back(WideToUtf8(path));
            CoTaskMemFree(path);
        }
    }
    AddPaths(std::move(paths));
}

// Imports dropped or chosen files and folders, or queues them behind the import that is running.
void Window::AddPaths(std::vector<std::string> paths)
{
    if (paths.empty())
        return;
    if (m_importThread.joinable())
        m_queuedDrops.insert(m_queuedDrops.end(), paths.begin(), paths.end());
    else
        StartImport(std::move(paths));
}

// Reading a big folder can take minutes (every file is opened for its identity), so it happens on another thread
// while the window stays responsive. The import bar shows how far it got and can cancel it. When the thread is
// done, FinishImport adds what it found to the library in one transaction.
void Window::StartImport(std::vector<std::string> paths)
{
    KillTimer(m_hWnd, kResultTimer);
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        m_importFound = 0;
        m_importFolder.clear();
        m_importCurrent.clear();
    }
    m_cancelImport = false;

    bool wasVisible = m_importBar.Visible();
    m_importBar.ShowProgress(strings::kImporting, L"");
    if (!wasVisible)
    {
        LayoutComponents();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }

    SetTimer(m_hWnd, kImportTimer, AnimationsEnabled() ? kAnimationMs : kCountMs, nullptr);

    m_importThread = std::thread(&Window::ScanDropped, this, std::move(paths));
}

// Runs on the import thread: lists the dropped folders and reads the identity of every file found. Never touches
// the library or the window, only the members shared under m_importMutex.
void Window::ScanDropped(std::vector<std::string> paths)
{
    folder_scan found;
    std::vector<std::string> unopened;
    for (const std::string &path : paths)
    {
        if (m_cancelImport)
        {
            found.cancelled = true;
            break;
        }

        size_t before = found.files.size();
        DWORD attributes = GetFileAttributesW(Utf8ToWide(path).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            {
                std::lock_guard<std::mutex> lock(m_importMutex);
                m_importFolder = path;
            }
            folder_scan scan = scan_folder(path, &m_cancelImport, [&](size_t count, const std::string &current)
            {
                std::lock_guard<std::mutex> lock(m_importMutex);
                m_importFound = before + count;
                m_importCurrent = current;
            });
            if (scan.error)
                unopened.push_back(path);
            found.files.insert(found.files.end(), std::make_move_iterator(scan.files.begin()),
                std::make_move_iterator(scan.files.end()));
            found.unreadable += scan.unreadable;
            found.skipped += scan.skipped;
            if (scan.cancelled)
            {
                found.cancelled = true;
                break;
            }
        }
        else
        {
            found.files.push_back({ path, file_identity(path) });
            std::lock_guard<std::mutex> lock(m_importMutex);
            m_importFound = found.files.size();
            m_importFolder.clear();
            m_importCurrent = path;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        m_importResult = std::move(found);
        m_importUnopened = std::move(unopened);
    }
    PostMessage(m_hWnd, kImportDone, 0, 0);
}

// Shows how many files the import thread found so far, and in which folder: the path from the dropped folder
// down, like Pictures\2023.
void Window::ShowImportProgress()
{
    if (m_cancelImport)
    {
        m_importBar.ShowProgress(strings::kCancelling, L"");
        m_importBar.Animate();
        return;
    }

    size_t found;
    std::string folder, current;
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        found = m_importFound;
        folder = m_importFolder;
        current = m_importCurrent;
    }

    std::wstring text = strings::kImporting;
    if (found > 0)
        text += L" \u00B7 " + FormatCount(found) + L" " + strings::kFound;

    std::wstring detail;
    if (!folder.empty() && !current.empty())
    {
        std::wstring root = Utf8ToWide(folder);
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
            root.pop_back();
        std::wstring path = Utf8ToWide(current);
        size_t name = path.find_last_of(L"\\/");
        if (name != std::wstring::npos)
            path.erase(name);
        // The scan returns the paths starting from the dropped folder, made absolute.
        size_t parent = root.find_last_of(L"\\/");
        if (parent != std::wstring::npos && path.compare(0, parent + 1, root, 0, parent + 1) == 0)
            path.erase(0, parent + 1);
        detail = path;
    }

    m_importBar.ShowProgress(text, detail);
    m_importBar.Animate();
}

// Called on the window's thread once the import thread is done: adds the files it found, unless the import was
// cancelled, and shows the outcome for a few seconds.
void Window::FinishImport()
{
    if (!m_importThread.joinable())
        return;
    m_importThread.join();
    KillTimer(m_hWnd, kImportTimer);

    folder_scan found;
    std::vector<std::string> unopened;
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        found = std::move(m_importResult);
        unopened = std::move(m_importUnopened);
        m_importResult = folder_scan();
    }

    std::wstring outcome;
    bool error = false;
    if (found.cancelled)
        outcome = strings::kImportCancelled;
    else
    {
        // Counted before adding, which makes them part of the library. A file dropped twice counts once.
        std::unordered_set<std::string> already;
        for (const scanned_file &s : found.files)
        {
            if (m_lib.find_file(s.path))
                already.insert(s.path);
        }

        // add_files saves them in a transaction of its own, so this isn't a BeginChange/FinishChange change.
        int added = m_lib.add_files(found.files);
        if (added < 0)
        {
            outcome = strings::kNotSaved;
            error = true;
            ReportSaveError();
            if (!IsWindow(m_hWnd))
                return;
        }
        else
        {
            std::wstring separator = L" \u00B7 ";
            if (added > 0)
                outcome = std::wstring(strings::kAdded) + L" " + FileCount(static_cast<size_t>(added));
            else if (!found.files.empty() || unopened.empty())
                outcome = strings::kNothingNew;
            if (!already.empty())
                outcome += separator + FormatCount(already.size()) + L" " + strings::kAlreadyThere;
            if (found.skipped > 0)
                outcome += separator + FormatCount(static_cast<size_t>(found.skipped)) + L" " + strings::kSkipped;
            if (found.unreadable == 1)
                outcome += separator + strings::kOneUnreadable;
            else if (found.unreadable > 1)
                outcome += separator + FormatCount(static_cast<size_t>(found.unreadable)) + L" " + strings::kUnreadableFolders;
            error = found.unreadable > 0 || !unopened.empty();
            for (const std::string &folder : unopened)
                outcome += (outcome.empty() ? L"" : separator) + strings::kCouldNotOpen + L" " + FolderName(folder);
            // The scan just found them, so they aren't missing, even those already in the library.
            for (const scanned_file &s : found.files)
                m_missingPaths.erase(s.path);
            LibraryChanged();
        }
    }

    if (!m_queuedDrops.empty())
    {
        std::vector<std::string> next;
        next.swap(m_queuedDrops);
        StartImport(std::move(next));
        return;
    }

    m_importBar.ShowResult(outcome, error);
    SetTimer(m_hWnd, kResultTimer, kResultMs, nullptr);
}

// The thread stops at the next file it reads; FinishImport then adds nothing.
void Window::CancelImport()
{
    m_cancelImport = true;
    m_queuedDrops.clear();
    ShowImportProgress();
}

// Waits for the import thread to stop, without adding what it found: the window is closing.
void Window::StopImport()
{
    m_queuedDrops.clear();
    if (!m_importThread.joinable())
        return;
    m_cancelImport = true;
    m_importThread.join();
    if (m_hWnd)
        KillTimer(m_hWnd, kImportTimer);
}

void Window::HideImportBar()
{
    KillTimer(m_hWnd, kResultTimer);
    m_importBar.Hide();
    LayoutComponents();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Adds or removes tags on every selected file. A tag that a removal leaves on no file at all is deleted, so the
// library doesn't fill up with tags nothing has, which autocomplete would keep suggesting.
void Window::ApplyTagEdit(const TagEdit &edit)
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (edit.kind == TagEdit::kNone || edit.tags.empty() || files.empty())
        return;

    if (!BeginChange())
        return;
    for (const std::string &name : edit.tags)
    {
        if (edit.kind == TagEdit::kAdd)
        {
            tag *t = m_lib.retrieve_tag(name);
            for (file *f : files)
                m_lib.edit_file(f, t, true);
        }
        else if (tag *t = m_lib.find_tag(name))
        {
            for (file *f : files)
                m_lib.edit_file(f, t, false);
            if (t->get_files().empty())
                m_lib.delete_tag(t);
        }
    }
    FinishChange();
}

// Takes the selected files out of the library, after asking. The files on disk are not touched. Like a tag
// removal, it deletes the tags that only these files had.
void Window::RemoveSelectedFiles()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;

    std::wstring prompt = strings::kRemoveBefore +
        (files.size() == 1 ? Utf8ToWide(files[0]->get_path()) : FileCount(files.size())) + strings::kRemoveAfter;
    if (MessageBoxW(m_hWnd, prompt.c_str(), strings::kAppName, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;

    std::unordered_set<tag*> tags;
    for (const file *f : files)
    {
        for (tag *t : f->get_tags())
            tags.insert(t);
    }

    if (!BeginChange())
        return;
    // The grid points to the files about to be deleted; it lets go of them first.
    m_fileGrid.Clear();
    for (file *f : files)
        m_lib.remove_file(f);
    for (tag *t : tags)
    {
        if (t->get_files().empty())
            m_lib.delete_tag(t);
    }
    FinishChange();
}

// Starts a change made of several writes as one transaction, so it is saved all or none: one wait for the disk
// instead of one per write, and no half-saved change if a write fails. If the transaction can't start (the
// database is locked, say), the change must not be made: says so and returns false.
bool Window::BeginChange()
{
    database *db = m_lib.attached();
    if (db && !db->begin())
    {
        ReportSaveError();
        return false;
    }
    return true;
}

// Saves the change started by BeginChange, or undoes it in the database if any write failed (ReportSaveError
// then reloads the library to match), and shows the result.
void Window::FinishChange()
{
    database *db = m_lib.attached();
    if (db && (!db->last_error().empty() || !db->commit()))
        db->rollback();

    ReportSaveError();
    LibraryChanged();
}

// Every change is written to the database as it happens. If a write failed, the library in memory no
// longer matches the database: a file that wasn't saved has no row of its own, and the id it was given
// may later belong to another file. So the library is reloaded from the database, the screen shows what
// is really saved, and the user is told the change didn't stick.
void Window::ReportSaveError()
{
    database *db = m_lib.attached();
    if (!db || db->last_error().empty())
        return;

    std::wstring message = strings::kSaveFailed + Utf8ToWide(db->last_error());
    db->clear_error();

    // The grid points into the library; it lets go before the library deletes its files.
    m_fileGrid.Clear();
    m_lib.clear();
    if (!open_library(m_lib, *db, ""))
    {
        // Nothing on screen can be trusted, and new changes would no longer be saved.
        message += strings::kReloadFailed + Utf8ToWide(db->last_error()) + strings::kWillClose;
        MessageBoxW(m_hWnd, message.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
        DestroyWindow(m_hWnd);
        return;
    }

    LibraryChanged();
    message += strings::kReloaded;
    MessageBoxW(m_hWnd, message.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
}

LRESULT Window::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        DestroyWindow(m_hWnd);
        return 0;

    case WM_DESTROY:
        StopImport();
        m_missingCheck.Stop();
        m_fileGrid.StopThumbnails();
        m_viewer.Stop();
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        m_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
        LayoutComponents();
        break;

    // The window moved to a monitor with another scale. Windows suggests a rectangle that keeps it as big as it
    // looked on the old one.
    case WM_DPICHANGED:
    {
        const RECT *suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        FitToMonitor();
        LayoutComponents();
        m_searchBar.OnDpiChanged();
        m_tagPanel.OnDpiChanged();
        m_sidebar.OnDpiChanged();
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_ACTIVATE:
    {
        bool active = LOWORD(wParam) != WA_INACTIVE;
        m_titleBar.SetActive(active);
        m_fileGrid.SetActive(active);
        m_sidebar.SetActive(active);
        if (active && m_sidebar.View() == LibraryView::Missing)
            CheckMissing();
        break;
    }

    // Like kImportDone, the result waits while a dialog is up, so the grid doesn't change under the code that
    // showed it.
    case kMissingChecked:
        if (!IsWindowEnabled(m_hWnd))
            m_missingPending = true;
        else
            OnMissingChecked();
        return 0;

    case WM_SETCURSOR:
    {
        // An I-beam over the text fields, like any text box.
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m_hWnd, &pt);
        if (LOWORD(lParam) == HTCLIENT && m_viewer.Contains(pt))
        {
            SetCursor(m_viewer.CursorHidden() ? nullptr : LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (LOWORD(lParam) == HTCLIENT && (m_searchBar.Contains(pt) || m_tagPanel.OverField(pt) || m_sidebar.OverField(pt)))
        {
            SetCursor(LoadCursor(nullptr, IDC_IBEAM));
            return TRUE;
        }
        break;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = Scale(theme::kMinWindowWidth);
        mmi->ptMinTrackSize.y = Scale(theme::kMinWindowHeight);
        return 0;
    }

    // The whole window is client area, but for a pixel along an auto-hidden taskbar (see FrameInsets).
    case WM_NCCALCSIZE:
    {
        RECT *rect = wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0] : reinterpret_cast<RECT*>(lParam);
        RECT insets = FrameInsets(*rect);
        rect->left += insets.left;
        rect->top += insets.top;
        rect->right -= insets.right;
        rect->bottom -= insets.bottom;
        return 0;
    }

    case WM_NCPAINT:
        PaintFrame();
        return 0;

    // The taskbar moved, appeared or started hiding itself, or a screen changed: a maximized window follows.
    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA)
            FitToMonitor();
        break;
    case WM_DISPLAYCHANGE:
        FitToMonitor();
        break;

    case WM_NCHITTEST:
        return HitTestResizeBorder(lParam);

    case WM_ERASEBKGND:
        return 1;  // OnPaint fills the whole background itself

    case WM_PAINT:
        OnPaint();
        return 0;

    // ---- Keyboard: the focused text field gets it, the grid otherwise ----

    case WM_CHAR:
        if (m_viewer.IsOpen())
            return 0;
        if (m_searchBar.IsFocused() && m_searchBar.OnChar(static_cast<wchar_t>(wParam)))
            RefreshResults();
        else if (m_tagPanel.IsFocused())
            m_tagPanel.OnChar(static_cast<wchar_t>(wParam));
        else if (m_sidebar.IsFocused())
            m_sidebar.OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_KEYDOWN:
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool repeat = (lParam & 0x40000000) != 0;  // the key was already down: it is held

        // The viewer takes every key while it is open.
        if (m_viewer.IsOpen())
        {
            HandleViewerAction(m_viewer.OnKeyDown(wParam, repeat));
            return 0;
        }

        // Ctrl+B: hide or show the sidebar, from anywhere.
        if (ctrl && wParam == 'B')
        {
            ToggleSidebar();
            return 0;
        }

        // Ctrl+T: type tags for the selected files, from anywhere.
        if (ctrl && wParam == 'T')
        {
            m_searchBar.Blur();
            m_sidebar.Blur();
            m_tagPanel.Focus();
            return 0;
        }

        if (m_searchBar.IsFocused())
        {
            if (m_searchBar.OnKeyDown(wParam, ctrl))
                RefreshResults();
            return 0;
        }

        if (m_tagPanel.IsFocused())
        {
            TagEdit edit;
            m_tagPanel.OnKeyDown(wParam, ctrl, edit);
            ApplyTagEdit(edit);
            return 0;
        }

        if (m_sidebar.IsFocused())
        {
            m_sidebar.OnKeyDown(wParam, ctrl);
            return 0;
        }

        if (wParam == VK_RETURN)
        {
            OpenFiles(m_fileGrid.SelectedFiles());
            return 0;
        }

        // Space: a quick look at the picture, among the selected files if there are several, or all on screen.
        if (wParam == VK_SPACE)
        {
            std::vector<file*> selected = m_fileGrid.SelectedFiles();
            file *f = m_fileGrid.FocusedFile();
            if (!f || std::find(selected.begin(), selected.end(), f) == selected.end())
                f = selected.empty() ? nullptr : selected[0];
            if (f && !repeat && Viewer::CanShow(f->get_path()))
            {
                bool inSelection = selected.size() > 1;
                ViewFile(f, inSelection ? selected : m_fileGrid.Files(), inSelection);
            }
            return 0;
        }

        if (wParam == VK_DELETE)
        {
            RemoveSelectedFiles();
            return 0;
        }

        if (m_fileGrid.OnKeyDown(wParam, ctrl, shift))
        {
            SelectionChanged();
            return 0;
        }
        break;
    }

    // ---- Mouse ----

    // The second press of a double-click arrives as WM_LBUTTONDBLCLK instead of WM_LBUTTONDOWN.
    // It's still a press for every component; on a grid item it also opens the file.
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // The second click of a double-click on the viewer's close button is dropped: it would open the tile now
        // under the pointer.
        bool afterViewer = uMsg == WM_LBUTTONDBLCLK && m_closedViewer;
        m_closedViewer = false;
        if (afterViewer)
            return 0;

        if (m_viewer.IsOpen())
        {
            if (m_viewer.Contains(pt))
            {
                HandleViewerAction(m_viewer.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK));
                m_closedViewer = !m_viewer.IsOpen();
            }
            else
            {
                bool toggleSidebar = false;
                m_titleBar.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK, toggleSidebar);
                if (toggleSidebar)
                    ToggleSidebar();
            }
            return 0;
        }

        // The second click of a double-click on a grid button is dropped: the first one may have filled the grid
        // ("Clear filters"), and it would select or open the file now under the pointer.
        bool afterGridButton = uMsg == WM_LBUTTONDBLCLK && m_pressedGridButton;
        m_pressedGridButton = false;

        // Every click goes to the text fields first so they can drop focus when clicked outside. The tag field and
        // the sidebar's go first: there is only one caret, and the search bar may be about to take it.
        if (!m_tagPanel.Contains(pt))
            m_tagPanel.Blur();
        if (!m_sidebar.Contains(pt))
            m_sidebar.Blur();
        if (m_searchBar.OnLButtonDown(pt))
            return 0;
        // A tag keeps its place in the sidebar once clicked, so the second click of a double-click cycles it on.
        std::wstring query = m_searchBar.Query();
        std::optional<LibraryView> view;
        if (m_sidebar.OnLButtonDown(pt, query, view))
        {
            if (view && uMsg == WM_LBUTTONDOWN)
                ShowView(*view);
            else if (query != m_searchBar.Query())
                SetQuery(query);
            return 0;
        }
        TagEdit edit;
        if (m_tagPanel.OnLButtonDown(pt, edit))
        {
            // A chip changes under the cursor once clicked (it goes away, or moves to the tags every file has),
            // so the second click of a double-click would hit another one.
            if (uMsg == WM_LBUTTONDOWN)
                ApplyTagEdit(edit);
            return 0;
        }
        bool cancel = false;
        if (m_importBar.OnLButtonDown(pt, cancel))
        {
            if (cancel && uMsg == WM_LBUTTONDOWN)
                CancelImport();
            return 0;
        }
        bool toggleSidebar = false;
        if (m_titleBar.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK, toggleSidebar))
        {
            if (toggleSidebar)
                ToggleSidebar();
            return 0;
        }
        if (afterGridButton)
            return 0;
        FileGrid::Button button = m_fileGrid.ButtonAt(pt);
        if (button != FileGrid::Button::kNone)
        {
            if (uMsg == WM_LBUTTONDBLCLK)
                return 0;
            m_pressedGridButton = true;
            if (button == FileGrid::Button::kAddFolder)
                ChooseFolders();
            else
            {
                // Back to every file, as a filtered view can hold none.
                m_sidebar.SetView(LibraryView::All);
                SetQuery(L"");
            }
            return 0;
        }
        if (m_fileGrid.OnLButtonDown(pt, (wParam & MK_CONTROL) != 0, (wParam & MK_SHIFT) != 0))
            SelectionChanged();

        if (uMsg == WM_LBUTTONDBLCLK)
        {
            if (file *f = m_fileGrid.FileAt(pt))
                ViewFile(f, m_fileGrid.Files());
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!m_trackingMouse)
        {
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
            m_trackingMouse = TrackMouseEvent(&track) != FALSE;
        }
        OnHover(pt);
        if (m_titleBar.OnMouseMove() || (!m_viewer.IsOpen() && m_fileGrid.OnMouseMove(pt)))
            return 0;
        break;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        OnHover(kNoPointer);
        return 0;

    case WM_LBUTTONUP:
        m_titleBar.OnLButtonUp();
        m_fileGrid.OnLButtonUp();
        m_viewer.OnLButtonUp();
        return 0;

    case WM_CAPTURECHANGED:
        m_fileGrid.OnCaptureChanged();
        m_viewer.OnCaptureChanged();
        break;

    case WM_MOUSEWHEEL:
    {
        // Unlike the other mouse messages, the wheel comes with screen coordinates.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hWnd, &pt);
        if (m_viewer.IsOpen())
            m_viewer.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam));
        else if (!m_tagPanel.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam)) &&
            !m_sidebar.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam)))
            m_fileGrid.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }

    case WM_DROPFILES:
        // The import bar and its outcome are under the viewer: it closes first, so the drop doesn't go unseen.
        CloseViewer();
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    // A dialog (a MessageBox asking to remove or open files, say) disables the window and runs its own message
    // loop, which would hand us this message while the code that showed the dialog is paused. Adding the files
    // then could reload the library under that code's feet, so it waits until the window is enabled again.
    case kImportDone:
        if (!IsWindowEnabled(m_hWnd))
            m_importDonePending = true;
        else
            FinishImport();
        return 0;

    // WM_ENABLE arrives while the dialog is still closing, before the code that showed it resumes: posting puts
    // the import behind it.
    case WM_ENABLE:
        if (wParam && m_importDonePending)
        {
            m_importDonePending = false;
            PostMessage(m_hWnd, kImportDone, 0, 0);
        }
        if (wParam && m_missingPending)
        {
            m_missingPending = false;
            PostMessage(m_hWnd, kMissingChecked, 0, 0);
        }
        break;

    case kImageDecoded:
        HandleViewerAction(m_viewer.OnDecoded());
        return 0;

    case kVideoEvent:
        HandleViewerAction(m_viewer.OnVideoEvent());
        return 0;

    case kViewerOverlays:
        m_viewer.OnOverlays();
        return 0;

    case kThumbnailsReady:
        m_fileGrid.OnThumbnailsReady();
        return 0;

    case WM_TIMER:
        if (wParam == kImportTimer)
            ShowImportProgress();
        else if (wParam == kResultTimer)
            HideImportBar();
        else if (wParam == kGridHoverTimer || wParam == kGridButtonTimer || wParam == kThumbnailTimer)
            m_fileGrid.OnTimer(wParam);
        else if (wParam == kChipHoverTimer)
            m_tagPanel.OnTimer();
        else if (wParam == kCancelHoverTimer)
            m_importBar.OnTimer();
        else if (wParam == kSidebarHoverTimer || wParam == kSidebarViewHoverTimer)
            m_sidebar.OnTimer(wParam);
        else if (wParam == kSidebarSlideTimer)
            StepSidebarSlide();
        else if (wParam == kTitleHoverTimer)
            m_titleBar.OnTimer();
        else if (wParam == kViewerIdleTimer || wParam == kViewerFadeTimer || wParam == kViewerHoverTimer ||
                 wParam == kViewerFrameTimer || wParam == kViewerLoadingTimer || wParam == kViewerVideoTimer)
            m_viewer.OnTimer(wParam);
        return 0;
    }

    return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
}

// The window has no native frame, so we tell Windows where the resize edges are.
LRESULT Window::HitTestResizeBorder(LPARAM lParam) const
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    // A window that fills its monitor has no edges to drag; the pixel left for an auto-hidden taskbar is no part of it.
    if (m_titleBar.Maximized() || m_monitorFullscreen)
    {
        RECT client;
        GetClientRect(m_hWnd, &client);
        MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
        return PtInRect(&client, pt) ? HTCLIENT : HTNOWHERE;
    }

    RECT wr;
    GetWindowRect(m_hWnd, &wr);

    const int border = Scale(theme::kResizeBorder);

    bool left   = pt.x < wr.left + border;
    bool right  = pt.x >= wr.right - border;
    bool top    = pt.y < wr.top + border;
    bool bottom = pt.y >= wr.bottom - border;

    if (top && left)     return HTTOPLEFT;
    if (top && right)    return HTTOPRIGHT;
    if (bottom && left)  return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left)            return HTLEFT;
    if (right)           return HTRIGHT;
    if (top)             return HTTOP;
    if (bottom)          return HTBOTTOM;

    return HTCLIENT;
}

// A window covering the whole monitor counts as a fullscreen program, and Windows keeps the taskbar under it. A
// maximized window covers it all when the taskbar hides itself, so it keeps a pixel free along that taskbar, where
// the pointer brings it back. The pixel stays part of the window, painted by PaintFrame, so no desktop shows there.
RECT Window::FrameInsets(const RECT &windowRect) const
{
    RECT insets = {};
    if (!m_titleBar.Maximized() || m_monitorFullscreen)
        return insets;
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfo(MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST), &monitor))
        return insets;
    const RECT &m = monitor.rcMonitor;

    struct { UINT edge; LONG RECT::*side; bool touches; } edges[] = {
        { ABE_LEFT,   &RECT::left,   windowRect.left <= m.left },
        { ABE_TOP,    &RECT::top,    windowRect.top <= m.top },
        { ABE_RIGHT,  &RECT::right,  windowRect.right >= m.right },
        { ABE_BOTTOM, &RECT::bottom, windowRect.bottom >= m.bottom },
    };
    for (const auto &e : edges)
    {
        if (!e.touches)
            continue;
        APPBARDATA bar = { sizeof(bar) };
        bar.uEdge = e.edge;
        bar.rc = m;
        if (SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar))
            insets.*e.side = 1;
    }
    return insets;
}

// Fills what the window has outside its client area, the pixels FrameInsets keeps, with the window's colour.
void Window::PaintFrame() const
{
    RECT window, client;
    GetWindowRect(m_hWnd, &window);
    GetClientRect(m_hWnd, &client);
    MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
    OffsetRect(&client, -window.left, -window.top);
    OffsetRect(&window, -window.left, -window.top);
    if (EqualRect(&window, &client))
        return;

    HDC dc = GetWindowDC(m_hWnd);
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    auto channel = [](float c) { return static_cast<BYTE>(c * 255.0f + 0.5f); };
    HBRUSH brush = CreateSolidBrush(RGB(channel(theme::kBase.r), channel(theme::kBase.g), channel(theme::kBase.b)));
    FillRect(dc, &window, brush);
    DeleteObject(brush);
    ReleaseDC(m_hWnd, dc);
}

// After the taskbar, the screen or its scale changed: a fullscreen window covers its monitor again, a maximized one
// its work area.
void Window::FitToMonitor()
{
    if (!m_monitorFullscreen)
    {
        m_titleBar.FitMaximized();
        return;
    }
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT &m = monitor.rcMonitor;
    SetWindowPos(m_hWnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED);
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
    if (fullscreen)
    {
        GetWindowRect(m_hWnd, &m_beforeFullscreen);
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
        // The taskbar may have changed meanwhile.
        m_titleBar.FitMaximized();
        SetRoundedCorners(m_hWnd, !m_titleBar.Maximized());
    }
}

// Tells every component where the pointer is, so what is under it can light up.
void Window::OnHover(POINT pt)
{
    // A viewer covering the whole window covers the title bar too.
    m_titleBar.OnHover(m_viewer.Fullscreen() ? kNoPointer : pt);
    // Behind the viewer, nothing lights up.
    if (m_viewer.IsOpen())
    {
        m_viewer.OnMouseMove(pt);
        pt = kNoPointer;
    }
    // The search error hangs over the top of the grid; the tiles under it aren't reachable.
    m_fileGrid.OnHover(m_searchBar.OverError(pt) ? kNoPointer : pt);
    m_tagPanel.OnHover(pt);
    m_sidebar.OnHover(pt);
    m_importBar.OnHover(pt);
}

// Direct2D draws into a back buffer and shows it in one go when drawing ends, so there's no flicker.
void Window::OnPaint()
{
    PAINTSTRUCT ps;
    BeginPaint(m_hWnd, &ps);

    if (m_renderer.BeginDraw())
    {
        m_renderer.Clear(theme::kBase);

        if (m_viewer.IsOpen())
            m_viewer.Paint(m_renderer);
        else
        {
            m_fileGrid.Paint(m_renderer);
            m_sidebar.Paint(m_renderer);
            m_tagPanel.Paint(m_renderer);
            m_importBar.Paint(m_renderer);
            m_searchBar.Paint(m_renderer);
        }
        if (!m_viewer.Fullscreen())
            m_titleBar.Paint(m_renderer);

        // The caret is drawn by Windows on top of the window; hide it while the frame is shown over it.
        bool caret = m_searchBar.IsFocused() || m_tagPanel.IsFocused() || m_sidebar.IsFocused();
        if (caret)
            HideCaret(m_hWnd);
        m_renderer.EndDraw();
        if (caret)
            ShowCaret(m_hWnd);
    }

    EndPaint(m_hWnd, &ps);
}
