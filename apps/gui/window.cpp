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
#include "open_with.hpp"
#include "single_instance.hpp"
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

static const wchar_t *kTileSizeValue = L"TileSize";

static int LoadTileSize()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kTileSizeValue, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return FileGrid::kDefaultTile;
    // A value written by hand snaps to the slider's 16-pixel steps, so Ctrl+plus and minus stay on them.
    const int step = 16;
    int snapped = (static_cast<int>(std::min<DWORD>(value, FileGrid::kMaxTile)) - FileGrid::kMinTile + step / 2) / step * step + FileGrid::kMinTile;
    return std::max<int>(FileGrid::kMinTile, std::min<int>(snapped, FileGrid::kMaxTile));
}

// Best effort, like the sidebar's state.
static void SaveTileSize(int size)
{
    DWORD value = static_cast<DWORD>(size);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kTileSizeValue, REG_DWORD, &value, sizeof(value));
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

Window::Window(library &lib, const std::wstring &thumbnailFolder, std::uint64_t instanceKey)
    : m_hInstance(GetModuleHandle(nullptr)), m_lib(lib), m_searchBar(strings::kSearchPlaceholder), m_tagPanel(lib), m_sidebar(lib)
{
    m_searchBar.SetSearchIcon(true);
    m_sidebarOpen = !LoadSidebarHidden();
    m_sidebarLevel = m_slideFrom = m_sidebarOpen ? 1.0f : 0.0f;
    m_settings.SetSidebarShown(m_sidebarOpen);
    m_fileGrid.SetTileSize(LoadTileSize());
    m_settings.SetTileSize(m_fileGrid.TileSize());
    // Corpus moved or updated since it was put in "Open with": the entries follow it.
    open_with::Refresh();
    m_settings.SetOpenWithOn(open_with::IsOn());

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

    single_instance::Mark(m_hWnd, instanceKey);
    SetRoundedCorners(m_hWnd, true);

    if (!m_renderer.Init(m_hWnd))
    {
        MessageBoxW(m_hWnd, strings::kNoDirect2D, strings::kAppName, MB_OK | MB_ICONERROR);
        DestroyWindow(m_hWnd);
        return;
    }

    m_fileGrid.StartThumbnails(m_hWnd, thumbnailFolder);
    m_viewer.Start(m_hWnd);
    m_viewer.SetReading([this](const std::string &path) { return LastPage(path); },
        [this](const std::string &path, size_t page, size_t count, const std::string &location)
        {
            SavePage(path, page, count, location);
        });
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
    // The sidebar button takes the left end of the search row while there is room for a sidebar.
    int searchLeft = margin + (RoomForSidebar() ? Scale(theme::kSidebarToggle) + Scale(theme::kSidebarToggleGap) : 0);
    RECT searchBar = { searchLeft, searchTop, client.right - margin, searchTop + Scale(theme::kSearchHeight) };

    int contentTop = topBlock.bottom + Scale(theme::kContentGap);
    int fullWidth = Scale(theme::kSidebarWidth);
    int sidebarWidth = RoomForSidebar() ? static_cast<int>(fullWidth * m_sidebarLevel + 0.5f) : 0;
    RECT sidebar = { sidebarWidth > 0 ? sidebarWidth - fullWidth : 0, contentTop, sidebarWidth, client.bottom };
    if (sidebar.bottom < sidebar.top)
        sidebar.bottom = sidebar.top;

    // The info pane takes the right edge, from the top of the content to the bottom of the window.
    int infoWidth = m_infoPane.Width(m_hWnd, client.right - sidebar.right);
    RECT infoPane = { client.right - infoWidth, contentTop, client.right, client.bottom };
    if (infoPane.bottom < infoPane.top)
        infoPane.bottom = infoPane.top;
    int right = infoPane.left;

    RECT tagPanel = { sidebar.right, client.bottom - Scale(theme::kTagStripHeight), right, client.bottom };

    // The import bar takes room from the grid only while it is shown.
    RECT importBar = { sidebar.right, tagPanel.top - (m_importBar.Visible() ? m_importBar.Height() : 0), right, tagPanel.top };

    RECT fileGrid = { sidebar.right, contentTop, right, importBar.top };
    if (fileGrid.bottom < fileGrid.top)
        fileGrid.bottom = fileGrid.top;

    m_titleBar.SetBounds(m_hWnd, topBlock);
    m_titleBar.ShowSidebarButton(SidebarButtonShown());
    m_searchBar.SetBounds(m_hWnd, m_renderer, searchBar);
    m_fileGrid.SetBounds(m_hWnd, m_renderer, fileGrid);
    m_tagPanel.SetBounds(m_hWnd, m_renderer, tagPanel);
    m_sidebar.SetBounds(m_hWnd, m_renderer, sidebar);
    m_infoPane.SetBounds(m_hWnd, infoPane);
    m_importBar.SetBounds(m_hWnd, importBar);

    RECT viewer = { 0, m_viewer.Fullscreen() ? 0 : titleRow, client.right, client.bottom };
    if (viewer.bottom < viewer.top)
        viewer.bottom = viewer.top;
    m_viewer.SetBounds(m_hWnd, m_renderer, viewer);

    RECT settings = { 0, titleRow, client.right, client.bottom };
    if (settings.bottom < settings.top)
        settings.bottom = settings.top;
    m_settings.SetBounds(m_hWnd, m_renderer, settings);
    m_rename.SetBounds(client);

    // Whatever moved, a tooltip does not stay behind where its button was.
    UpdateTooltip(CursorInWindow());
}

// The viewer and the settings cover the search row, and with it the sidebar button.
bool Window::SidebarButtonShown() const
{
    return RoomForSidebar() && !m_viewer.IsOpen() && !m_settings.IsOpen();
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
    m_settings.SetSidebarShown(m_sidebarOpen);
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
    FileGrid::Empty empty = FileGrid::Empty::kNoMatches;
    if (m_lib.get_files().empty())
        empty = FileGrid::Empty::kLibrary;
    else if (view == LibraryView::Missing && !m_missingChecked)
        empty = FileGrid::Empty::kChecking;
    m_fileGrid.SetEmpty(empty);
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
    for (const std::string &path : m_foundSinceCheck)
        m_missingPaths.erase(path);
    m_foundSinceCheck.clear();
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
    InvalidateRect(m_hWnd, nullptr, FALSE);  // the sidebar button's mark
}

void Window::SelectionChanged()
{
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    m_tagPanel.SetSelection(selected);
    if (m_infoPane.IsOpen())
        m_infoPane.SetSelection(selected);
}

// Shows or hides the info pane. It reads the selection from the disk when it opens, not while it is closed.
void Window::ToggleInfo()
{
    m_infoPane.SetOpen(!m_infoPane.IsOpen());
    LayoutComponents();
    SelectionChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Enter and "Open": like a double-click, pictures, videos and readable documents go to the viewer and the rest
// to their default program.
void Window::OpenSelected()
{
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    std::vector<file*> viewable, others;
    for (file *f : selected)
        (Viewer::CanShow(f->get_path()) ? viewable : others).push_back(f);
    if (!others.empty())
        OpenFiles(others);
    if (viewable.size() == 1)
        ViewFile(viewable[0], selected.size() == 1 ? m_fileGrid.Files() : viewable, selected.size() > 1);
    else if (!viewable.empty())
        ViewFile(viewable[0], viewable, true);
}

// A right click on a file: it joins the selection if it isn't in it (alone, as in the Finder), and the menu then
// works on the selection. A click on empty room opens nothing.
void Window::OpenContextMenu(POINT pt)
{
    file *f = m_fileGrid.FileAt(pt);
    if (!f)
        return;
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (std::find(selected.begin(), selected.end(), f) == selected.end())
    {
        m_fileGrid.SelectPath(f->get_path());
        SelectionChanged();
    }
    m_searchBar.Blur();
    m_tagPanel.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, pt);
    m_contextMenu.OnHover(pt);
}

// Ctrl+T, "Add tag" and the info pane's button: the tag field takes the keyboard, to type tags for the selection.
void Window::FocusTagField()
{
    m_searchBar.Blur();
    m_sidebar.Blur();
    m_tagPanel.Focus();
}

void Window::RunMenuItem(ContextMenu::Item item)
{
    if (item == ContextMenu::Item::kOpen)
        OpenSelected();
    else if (item == ContextMenu::Item::kAddTag)
        FocusTagField();
    else if (item == ContextMenu::Item::kInfo)
    {
        // Info always shows the pane; Ctrl+I is what hides it.
        if (!m_infoPane.IsOpen())
            ToggleInfo();
    }
    else if (item == ContextMenu::Item::kRename)
        StartRename();
    else if (item == ContextMenu::Item::kRemove)
        RemoveSelectedFiles();
    else if (item == ContextMenu::Item::kDelete)
        DeleteSelectedFiles();
}

// F2 and "Rename": asks for a new name for the focused file, or the first selected one. The sheet takes the keyboard
// until it is answered.
void Window::StartRename()
{
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (selected.empty())
        return;
    file *f = m_fileGrid.FocusedFile();
    if (!f || std::find(selected.begin(), selected.end(), f) == selected.end())
        f = selected[0];

    m_searchBar.Blur();
    m_tagPanel.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renamePath = f->get_path();
    std::wstring wide = Utf8ToWide(m_renamePath);
    size_t slash = wide.find_last_of(L"\\/");
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_rename.Open(m_hWnd, m_renderer, client, slash == std::wstring::npos ? wide : wide.substr(slash + 1));
    m_rename.OnHover(CursorInWindow());
}

void Window::HandleRenameAction(RenameSheet::Action action)
{
    if (action == RenameSheet::Action::kCancel)
        m_rename.Close();
    else if (action == RenameSheet::Action::kConfirm)
    {
        std::wstring error;
        if (RenameFile(m_renamePath, m_rename.Name(), error))
            m_rename.Close();
        else
            m_rename.SetError(error);
    }
    else
        return;
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Why Windows can't have 'name' as a file name, or null if it can: characters it forbids, a trailing space or dot
// (which it silently drops, so the file would not get the name typed) and the names of devices.
static const wchar_t *BadFileName(const std::wstring &name)
{
    if (name.empty())
        return strings::kRenameEmpty;
    for (wchar_t ch : name)
    {
        if (ch < L' ' || wcschr(L"\\/:*?\"<>|", ch))
            return strings::kRenameBadChars;
    }
    if (name.back() == L' ' || name.back() == L'.')
        return strings::kRenameBadEnd;

    // CON, NUL, COM1... are devices with or without an extension ("nul.txt").
    std::wstring stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ')
        stem.pop_back();
    static const wchar_t *const kDevices[] = { L"CON", L"PRN", L"AUX", L"NUL" };
    for (const wchar_t *device : kDevices)
    {
        if (_wcsicmp(stem.c_str(), device) == 0)
            return strings::kRenameReserved;
    }
    if (stem.size() == 4 && (_wcsnicmp(stem.c_str(), L"COM", 3) == 0 || _wcsnicmp(stem.c_str(), L"LPT", 3) == 0) &&
        stem[3] >= L'1' && stem[3] <= L'9')
        return strings::kRenameReserved;
    return nullptr;
}

// Renames the file on disk and points the library to the new path, keeping its tags. Nothing is replaced: a name
// taken in the folder is refused. If the library can't be saved, the file gets its old name back.
// On failure 'error' says why, to show in the sheet.
bool Window::RenameFile(const std::string &path, const std::wstring &name, std::wstring &error)
{
    if (const wchar_t *bad = BadFileName(name))
    {
        error = bad;
        return false;
    }

    std::wstring from = Utf8ToWide(path);
    size_t slash = from.find_last_of(L"\\/");
    std::wstring to = (slash == std::wstring::npos ? L"" : from.substr(0, slash + 1)) + name;
    if (to == from)
        return true;

    file *f = m_lib.find_file(path);
    if (!f || GetFileAttributesW(from.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        error = strings::kRenameMissing;
        return false;
    }
    // A change of case only is the same file to Windows, and fine; anything else already there is refused.
    std::wstring oldName = slash == std::wstring::npos ? from : from.substr(slash + 1);
    bool caseOnly = _wcsicmp(oldName.c_str(), name.c_str()) == 0;
    if (!caseOnly && GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        error = strings::kRenameExists;
        return false;
    }
    if (!MoveFileExW(from.c_str(), to.c_str(), 0))
    {
        error = GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS
            ? strings::kRenameExists : strings::kRenameFailed;
        return false;
    }

    std::string newPath = WideToUtf8(to);
    if (!m_lib.relink(f, newPath))
    {
        MoveFileExW(to.c_str(), from.c_str(), 0);
        ReportSaveError();
        error = strings::kRenameFailed;
        return false;
    }
    m_fileGrid.Clear();
    LibraryChanged();
    m_fileGrid.SelectPath(newPath);
    SelectionChanged();
    return true;
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

// The settings cover the window like the viewer, from the foot of the sidebar or Ctrl+comma.
void Window::OpenSettings()
{
    m_searchBar.Blur();
    m_tagPanel.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_settings.Open();
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

POINT Window::CursorInWindow() const
{
    POINT pt;
    if (!GetCursorPos(&pt) || !ScreenToClient(m_hWnd, &pt))
        return kNoPointer;
    return pt;
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
    m_rename.Close();
    CloseSettings();
    CloseViewer();
    m_searchBar.Blur();
    m_tagPanel.Blur();
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
            // A check under way may have read the disk before then, so its result must not bring them back.
            for (const scanned_file &s : found.files)
            {
                m_missingPaths.erase(s.path);
                m_foundSinceCheck.insert(s.path);
            }
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
// A click in the sidebar: a tag cycles through required, excluded and off (it keeps its place once clicked, so
// the second click of a double-click cycles it on), a Library view is shown, the foot opens the settings.
bool Window::HandleSidebarPress(POINT pt, bool doubleClick)
{
    std::wstring query = m_searchBar.Query();
    std::optional<LibraryView> view;
    bool openSettings = false;
    if (!m_sidebar.OnLButtonDown(pt, query, view, openSettings))
        return false;
    if (openSettings && !doubleClick)
        OpenSettings();
    else if (view && !doubleClick)
        ShowView(*view);
    else if (query != m_searchBar.Query())
        SetQuery(query);
    return true;
}

// The files a tag dropped at 'pt' goes to: the selection if the file under the pointer is in it, else that file alone.
std::vector<file*> Window::DropFilesAt(POINT pt) const
{
    file *f = m_fileGrid.FileAt(pt);
    if (!f)
        return {};
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (std::find(selected.begin(), selected.end(), f) != selected.end())
        return selected;
    return { f };
}

void Window::EndTagDrag()
{
    m_dragTag.clear();
    m_dragActive = false;
    m_fileGrid.SetDropTargets({});
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void Window::ApplyTagEdit(const TagEdit &edit)
{
    ApplyTagEdit(edit, m_fileGrid.SelectedFiles());
}

void Window::ApplyTagEdit(const TagEdit &edit, const std::vector<file*> &files)
{
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
    RemoveFromLibrary(files);
}

// Takes 'files' out of the library, and the tags only they had. The files on disk are not touched.
void Window::RemoveFromLibrary(const std::vector<file*> &files)
{
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

// Sends files to the Recycle Bin, never deleting them for good: Windows asks first when it can't recycle one (a
// network share, say). Returns the paths that are still on disk afterwards, because the user said no or Windows
// failed.
static std::vector<std::wstring> RecycleFiles(HWND hWnd, const std::vector<std::wstring> &paths, std::wstring &error)
{
    // Not in every MinGW header: adds the deletion to the Explorer's undo list (Ctrl+Z there).
    const DWORD addUndoRecord = 0x20000000;

    IFileOperation *operation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr))
    {
        operation->SetOwnerWindow(hWnd);
        operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_WANTNUKEWARNING | addUndoRecord);
        for (const std::wstring &path : paths)
        {
            IShellItem *item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))))
            {
                operation->DeleteItem(item, nullptr);
                item->Release();
            }
        }
        hr = operation->PerformOperations();
        operation->Release();
    }
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        wchar_t* text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        error = text ? text : L"";
        LocalFree(text);
    }

    std::vector<std::wstring> left;
    for (const std::wstring &path : paths)
    {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            left.push_back(path);
    }
    return left;
}

// Shift+Del and "Delete from disk": after asking, moves the selected files to the Recycle Bin and takes the ones
// that went out of the library. A file that is still there (the user declined, or Windows could not) stays.
void Window::DeleteSelectedFiles()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;

    std::wstring prompt = strings::kDeleteBefore +
        (files.size() == 1 ? Utf8ToWide(files[0]->get_path()) : FileCount(files.size())) + strings::kDeleteAfter;
    if (MessageBoxW(m_hWnd, prompt.c_str(), strings::kAppName, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;

    std::vector<std::wstring> paths;
    for (const file *f : files)
        paths.push_back(Utf8ToWide(f->get_path()));
    std::wstring error;
    std::unordered_set<std::wstring> left;
    for (std::wstring &path : RecycleFiles(m_hWnd, paths, error))
        left.insert(std::move(path));

    std::vector<file*> gone;
    for (size_t i = 0; i < files.size(); i++)
    {
        if (!left.count(paths[i]))
            gone.push_back(files[i]);
    }
    if (!gone.empty())
        RemoveFromLibrary(gone);
    if (!error.empty())
        MessageBoxW(m_hWnd, (strings::kDeleteFailed + error).c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
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
        // The page reached in a book is saved while the window can still tell if that failed.
        CloseViewer();
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
        m_contextMenu.Close();
        m_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
        LayoutComponents();
        if (wParam == SIZE_RESTORED && !m_monitorFullscreen)
            m_titleBar.FitMaximized();
        break;

    // The window moved to a monitor with another scale. Windows suggests a rectangle that keeps it as big as it
    // looked on the old one.
    case WM_DPICHANGED:
    {
        const RECT *suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        m_settings.OnDpiChanged();
        FitToMonitor();
        LayoutComponents();
        m_searchBar.OnDpiChanged();
        m_tagPanel.OnDpiChanged();
        m_sidebar.OnDpiChanged();
        m_infoPane.OnDpiChanged();
        m_contextMenu.Close();
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_COPYDATA:
    {
        const COPYDATASTRUCT *data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!data || data->dwData != single_instance::kCopyDataFiles)
            return FALSE;
        OpenExternal(single_instance::ReadFiles(*data));
        return TRUE;
    }

    case WM_ACTIVATE:
    {
        bool active = LOWORD(wParam) != WA_INACTIVE;
        if (!active)
            m_contextMenu.Close();
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

    case kInfoStats:
        m_infoPane.OnStats();
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

    // The whole window is client area.
    case WM_NCCALCSIZE:
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
        if (m_rename.IsOpen())
        {
            m_rename.OnChar(static_cast<wchar_t>(wParam));
            return 0;
        }
        if (m_viewer.IsOpen() || m_settings.IsOpen() || !m_dragTag.empty())
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
        m_tooltip.Dismiss(m_hWnd);
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool repeat = (lParam & 0x40000000) != 0;  // the key was already down: it is held

        // Esc drops a tag being dragged.
        if (!m_dragTag.empty())
        {
            if (wParam == VK_ESCAPE)
            {
                EndTagDrag();
                ReleaseCapture();
            }
            return 0;
        }

        // The rename sheet takes every key while it is open.
        if (m_rename.IsOpen())
        {
            HandleRenameAction(m_rename.OnKeyDown(wParam, ctrl));
            return 0;
        }

        // An open context menu takes every key.
        if (m_contextMenu.IsOpen())
        {
            RunMenuItem(m_contextMenu.OnKeyDown(wParam));
            return 0;
        }

        // The viewer takes every key while it is open.
        if (m_viewer.IsOpen())
        {
            HandleViewerAction(m_viewer.OnKeyDown(wParam, repeat));
            return 0;
        }

        // Ctrl+comma: the settings, from anywhere. They take every key while they are open.
        if (m_settings.IsOpen())
        {
            if (ctrl && wParam == VK_OEM_COMMA)
                CloseSettings();
            else if (ctrl && wParam == 'B')
                ToggleSidebar();
            else
                HandleSettingsAction(m_settings.OnKeyDown(wParam));
            return 0;
        }
        if (ctrl && wParam == VK_OEM_COMMA)
        {
            OpenSettings();
            return 0;
        }

        // Ctrl+B: hide or show the sidebar, from anywhere.
        if (ctrl && wParam == 'B')
        {
            ToggleSidebar();
            return 0;
        }

        // Ctrl+I: show or hide the info pane, from anywhere.
        if (ctrl && wParam == 'I')
        {
            ToggleInfo();
            return 0;
        }

        // Ctrl+plus and Ctrl+minus: bigger or smaller thumbnails.
        if (ctrl && (wParam == VK_OEM_PLUS || wParam == VK_ADD))
        {
            SetTileSize(m_fileGrid.TileSize() + 16);
            return 0;
        }
        if (ctrl && (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT))
        {
            SetTileSize(m_fileGrid.TileSize() - 16);
            return 0;
        }

        // Ctrl+T: type tags for the selected files, from anywhere.
        if (ctrl && wParam == 'T')
        {
            FocusTagField();
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

        // Shift+F10 and the Menu key: the context menu of the focused file.
        if (wParam == VK_APPS || (shift && wParam == VK_F10))
        {
            POINT pt;
            if (m_fileGrid.MenuPoint(pt))
                OpenContextMenu(pt);
            return 0;
        }

        // Enter opens like a double-click: pictures and videos in the viewer, the rest in their default program.
        if (wParam == VK_RETURN)
        {
            OpenSelected();
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

        // F2 renames. Del takes files out of the library; Shift+Del also sends them to the Recycle Bin.
        if (wParam == VK_F2)
        {
            StartRename();
            return 0;
        }

        if (wParam == VK_DELETE)
        {
            if (shift)
                DeleteSelectedFiles();
            else
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
        m_tooltip.Dismiss(m_hWnd);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // The second click of a double-click on the viewer's close button or a menu row is dropped: it would
        // open the tile now under the pointer.
        bool afterOverlay = uMsg == WM_LBUTTONDBLCLK && m_closedOverlay;
        m_closedOverlay = false;
        if (afterOverlay)
            return 0;

        if (m_rename.IsOpen())
        {
            HandleRenameAction(m_rename.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK));
            return 0;
        }

        // A click closes the context menu, and picks the item it lands on.
        if (m_contextMenu.IsOpen())
        {
            RunMenuItem(m_contextMenu.OnLButtonDown(pt));
            OnHover(pt);
            // A quick second click would reach the file under the menu as a double-click.
            m_closedOverlay = true;
            return 0;
        }

        if (m_viewer.IsOpen())
        {
            if (m_viewer.Contains(pt))
            {
                HandleViewerAction(m_viewer.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK));
                m_closedOverlay = !m_viewer.IsOpen();
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

        if (m_settings.IsOpen())
        {
            if (m_settings.Contains(pt))
            {
                HandleSettingsAction(m_settings.OnLButtonDown(pt));
                if (m_settings.Dragging())
                    SetCapture(m_hWnd);
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
        // A press on a tag waits to learn whether it is a click or the start of a drag onto files.
        if (!m_sidebar.TagNameAt(pt).empty())
        {
            m_dragTag = m_sidebar.TagNameAt(pt);
            m_dragStart = m_dragPoint = pt;
            m_dragDouble = uMsg == WM_LBUTTONDBLCLK;
            m_dragActive = false;
            SetCapture(m_hWnd);
            return 0;
        }
        if (HandleSidebarPress(pt, uMsg == WM_LBUTTONDBLCLK))
            return 0;
        bool closeInfo = false, addTag = false;
        if (m_infoPane.OnLButtonDown(pt, closeInfo, addTag))
        {
            if (closeInfo && uMsg == WM_LBUTTONDOWN)
                ToggleInfo();
            else if (addTag && uMsg == WM_LBUTTONDOWN)
                FocusTagField();
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
                InvalidateRect(m_hWnd, nullptr, FALSE);
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

    // The menu opens on the press, as on macOS.
    case WM_RBUTTONDOWN:
        m_tooltip.Dismiss(m_hWnd);
        // A tag being dragged waits for its drop: Esc is the way out.
        if (!m_dragTag.empty())
            return 0;
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool wasOpen = m_contextMenu.IsOpen();
        m_contextMenu.Close();
        if (!wasOpen && !m_viewer.IsOpen() && !m_settings.IsOpen() && !m_rename.IsOpen())
            OpenContextMenu(pt);
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
        if (m_rename.IsOpen())
        {
            m_rename.OnMouseMove(pt);
            return 0;
        }
        if (m_settings.Dragging())
        {
            HandleSettingsAction(m_settings.OnMouseMove(pt));
            return 0;
        }
        if (!m_dragTag.empty())
        {
            if (!m_dragActive && (abs(pt.x - m_dragStart.x) >= Scale(5) || abs(pt.y - m_dragStart.y) >= Scale(5)))
                m_dragActive = true;
            if (m_dragActive)
            {
                m_dragPoint = pt;
                m_fileGrid.SetDropTargets(DropFilesAt(pt));
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;
        }
        if (m_titleBar.OnMouseMove() || (!m_viewer.IsOpen() && !m_settings.IsOpen() && m_fileGrid.OnMouseMove(pt)))
            return 0;
        break;
    }

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        OnHover(kNoPointer);
        return 0;

    case WM_LBUTTONUP:
        if (!m_dragTag.empty())
        {
            std::string name = m_dragTag;
            bool active = m_dragActive;
            bool doubleClick = m_dragDouble;
            POINT start = m_dragStart;
            std::vector<file*> files = active ? DropFilesAt(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }) : std::vector<file*>();
            EndTagDrag();
            ReleaseCapture();
            if (!active)
                HandleSidebarPress(start, doubleClick);
            else if (!files.empty())
            {
                TagEdit edit;
                edit.kind = TagEdit::kAdd;
                edit.tags.push_back(name);
                ApplyTagEdit(edit, files);
            }
            return 0;
        }
        if (m_settings.Dragging())
            ReleaseCapture();
        m_settings.OnLButtonUp();
        m_rename.OnLButtonUp();
        m_titleBar.OnLButtonUp();
        m_fileGrid.OnLButtonUp();
        m_viewer.OnLButtonUp();
        return 0;

    case WM_CAPTURECHANGED:
        if (!m_dragTag.empty())
            EndTagDrag();
        m_settings.OnCaptureChanged();
        m_rename.OnCaptureChanged();
        m_fileGrid.OnCaptureChanged();
        m_viewer.OnCaptureChanged();
        break;

    case WM_MOUSEWHEEL:
    {
        // Unlike the other mouse messages, the wheel comes with screen coordinates.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hWnd, &pt);
        m_contextMenu.Close();
        m_tooltip.Dismiss(m_hWnd);
        if (m_rename.IsOpen() || !m_dragTag.empty())
            return 0;
        if (m_viewer.IsOpen())
            m_viewer.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam));
        else if (m_settings.IsOpen())
            m_settings.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        else if (!m_infoPane.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam)) &&
            !m_tagPanel.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam)) &&
            !m_sidebar.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam)))
            m_fileGrid.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }

    case WM_DROPFILES:
        // The import bar and its outcome are under the viewer: it closes first, so the drop doesn't go unseen.
        CloseViewer();
        CloseSettings();
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

    case kPageReady:
        HandleViewerAction(m_viewer.OnPageReady());
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
        else if (wParam == kSidebarHoverTimer || wParam == kSidebarViewHoverTimer || wParam == kSidebarSettingsHoverTimer)
            m_sidebar.OnTimer(wParam);
        else if (wParam == kSidebarSlideTimer)
            StepSidebarSlide();
        else if (wParam == kTitleHoverTimer)
            m_titleBar.OnTimer(wParam);
        else if (wParam == kSettingsHoverTimer)
            m_settings.OnTimer();
        else if (wParam == kMenuHoverTimer)
            m_contextMenu.OnTimer();
        else if (wParam == kInfoHoverTimer)
            m_infoPane.OnTimer();
        else if (wParam == kTooltipTimer)
            m_tooltip.OnTimer(m_hWnd);
        else if (wParam == kViewerIdleTimer || wParam == kViewerFadeTimer || wParam == kViewerHoverTimer ||
                 wParam == kViewerFrameTimer || wParam == kViewerLoadingTimer || wParam == kViewerVideoTimer ||
                 wParam == kViewerProgressTimer || wParam == kViewerReflowTimer)
            m_viewer.OnTimer(wParam);
        return 0;
    }

    return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
}

// The window has no native frame, so we tell Windows where the resize edges are.
LRESULT Window::HitTestResizeBorder(LPARAM lParam) const
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    // A window that fills its monitor has no edges to drag.
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
    SetWindowPos(m_hWnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
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
        // The taskbar may have changed meanwhile.
        m_titleBar.FitMaximized();
        SetRoundedCorners(m_hWnd, !m_titleBar.Maximized());
    }
}

// Asks the components for the label of the icon-only button under the pointer, if there is one. The rename sheet,
// the menu, the viewer and the settings hide what is under them, as they do from the hover fades.
void Window::UpdateTooltip(POINT pt)
{
    RECT anchor = {};
    const wchar_t *text = nullptr;
    bool found = false;
    if (!m_rename.IsOpen() && !m_contextMenu.IsOpen())
    {
        if (m_viewer.IsOpen())
            found = m_viewer.TipAt(pt, anchor, text);
        else if (!m_settings.IsOpen())
            found = m_sidebar.TipAt(pt, anchor, text) || m_infoPane.TipAt(pt, anchor, text);
        if (!found && !m_viewer.Fullscreen())
            found = m_titleBar.TipAt(pt, anchor, text);
    }
    m_tooltip.Update(m_hWnd, found ? text : nullptr, anchor);
}

// Tells every component where the pointer is, so what is under it can light up.
void Window::OnHover(POINT pt)
{
    m_titleBar.ShowSidebarButton(SidebarButtonShown());
    UpdateTooltip(pt);
    // A viewer covering the whole window covers the title bar too.
    m_titleBar.OnHover(m_viewer.Fullscreen() ? kNoPointer : pt);
    // Under the rename sheet, nothing lights up.
    if (m_rename.IsOpen())
    {
        m_rename.OnHover(pt);
        pt = kNoPointer;
    }
    // Under an open menu, nothing lights up.
    if (m_contextMenu.IsOpen())
    {
        m_contextMenu.OnHover(pt);
        pt = kNoPointer;
    }
    // Behind the viewer or the settings, nothing lights up.
    if (m_viewer.IsOpen())
    {
        m_viewer.OnMouseMove(pt);
        pt = kNoPointer;
    }
    if (m_settings.IsOpen())
    {
        m_settings.OnHover(pt);
        pt = kNoPointer;
    }
    // The search error hangs over the top of the grid; the tiles under it aren't reachable.
    m_fileGrid.OnHover(m_searchBar.OverError(pt) ? kNoPointer : pt);
    m_tagPanel.OnHover(pt);
    m_sidebar.OnHover(pt);
    m_infoPane.OnHover(pt);
    m_importBar.OnHover(pt);
}

// Direct2D draws into a back buffer and shows it in one go when drawing ends, so there's no flicker.
void Window::OnPaint()
{
    PAINTSTRUCT ps;
    BeginPaint(m_hWnd, &ps);

    m_titleBar.ShowSidebarButton(SidebarButtonShown());
    LibraryView view = m_sidebar.View();
    m_titleBar.SetSidebarMark(!m_sidebarOpen && view != LibraryView::All, view == LibraryView::Missing);
    if (m_renderer.BeginDraw())
    {
        m_renderer.Clear(theme::kBase);

        if (m_viewer.IsOpen())
            m_viewer.Paint(m_renderer);
        else if (m_settings.IsOpen())
            m_settings.Paint(m_renderer);
        else
        {
            m_fileGrid.Paint(m_renderer);
            m_sidebar.Paint(m_renderer);
            m_tagPanel.Paint(m_renderer);
            m_importBar.Paint(m_renderer);
            m_infoPane.Paint(m_renderer);
            m_searchBar.Paint(m_renderer);
        }
        if (!m_viewer.Fullscreen())
            m_titleBar.Paint(m_renderer);
        m_contextMenu.Paint(m_renderer);
        m_rename.Paint(m_renderer);
        m_tooltip.Paint(m_renderer);
        if (m_dragActive)
        {
            // The tag being dragged, as a chip at the pointer.
            std::wstring label = Utf8ToWide(m_dragTag);
            float width = m_renderer.TextWidth(label, Font::kCaption) + 2.0f * Scale(8);
            float height = static_cast<float>(Scale(22));
            float x = static_cast<float>(m_dragPoint.x + Scale(12)), y = static_cast<float>(m_dragPoint.y + Scale(12));
            D2D1_RECT_F chip = { x, y, x + width, y + height };
            const float radius = static_cast<float>(Scale(theme::kChipRadius));
            m_renderer.FillRoundedRect(chip, radius, theme::kChip);
            m_renderer.DrawRoundedRect(chip, radius, theme::kYuri, 1.0f);
            RECT text = { static_cast<LONG>(x) + Scale(8), static_cast<LONG>(y), static_cast<LONG>(x + width) - Scale(8), static_cast<LONG>(y + height) };
            m_renderer.DrawTextLine(label, Font::kCaption, text, theme::kText1);
        }

        // The caret is drawn by Windows on top of the window; hide it while the frame is shown over it.
        bool caret = m_rename.IsFocused() || m_searchBar.IsFocused() || m_tagPanel.IsFocused() || m_sidebar.IsFocused();
        if (caret)
            HideCaret(m_hWnd);
        m_renderer.EndDraw();
        if (caret)
            ShowCaret(m_hWnd);
    }

    EndPaint(m_hWnd, &ps);
}
