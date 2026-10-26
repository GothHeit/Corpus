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
#include "../../include/text.hpp"
#include "filter_query.hpp"
#include "file_kind.hpp"
#include "shell_files.hpp"
#include "resource.h"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "open_with.hpp"
#include "single_instance.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#pragma comment(lib, "dwmapi.lib")

static const wchar_t *kClassName = L"Kafkas Window Class";

// Opening more files than this at once asks first, like Explorer does.
static const size_t kOpenPromptThreshold = 15;

// Where the pointer is said to be once it has left the window: outside every component.
static const POINT kNoPointer = { -32768, -32768 };

// Posted by the import thread when it is done.
static const UINT kImportDone = WM_APP + 1;
// Posted to itself once a drag of files out of the window ended, behind any drop it made on the window.
static const UINT kDragOutDone = WM_APP + 10;
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

static const wchar_t *kSortByValue = L"SortBy";
static const wchar_t *kSortDescendingValue = L"SortDescending";

// The grid's order as left last time: newest first unless the user picked another.
static void LoadSort(int &key, bool &descending)
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kSortByValue, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value <= 3)
        key = static_cast<int>(value);
    size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kSortDescendingValue, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        descending = value != 0;
}

// Best effort, like the sidebar's state.
static void SaveSort(int key, bool descending)
{
    DWORD value = static_cast<DWORD>(key);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kSortByValue, REG_DWORD, &value, sizeof(value));
    value = descending ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kSortDescendingValue, REG_DWORD, &value, sizeof(value));
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

// Asks for folders with the system's folder picker: several, or just one. Empty if none was picked.
static std::vector<std::string> PickFolders(HWND hWnd, bool several)
{
    std::vector<std::string> paths;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))))
        return paths;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | (several ? static_cast<DWORD>(FOS_ALLOWMULTISELECT) : 0));
    ComPtr<IShellItemArray> items;
    if (dialog->Show(hWnd) != S_OK || FAILED(dialog->GetResults(items.GetAddressOf())))
        return paths;

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
    return paths;
}

// How many of 'files' lack at least one of the tags named: the files that adding them changes.
static size_t FilesLacking(const library &lib, const std::vector<file*> &files, const std::vector<std::string> &names)
{
    std::vector<const tag*> tags;
    for (const std::string &name : names)
        tags.push_back(lib.find_tag(name));
    return static_cast<size_t>(std::count_if(files.begin(), files.end(), [&](const file *f)
    {
        return std::any_of(tags.begin(), tags.end(), [f](const tag *t) { return !t || !f->tagged(t); });
    }));
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
    : m_hInstance(GetModuleHandle(nullptr)), m_lib(lib), m_searchBar(strings::kSearchPlaceholder), m_sidebar(lib), m_tagSheet(lib)
{
    m_searchBar.SetSearchIcon(true);
    m_sidebarOpen = !LoadSidebarHidden();
    m_sidebarLevel = m_slideFrom = m_sidebarOpen ? 1.0f : 0.0f;
    m_settings.SetSidebarShown(m_sidebarOpen);
    m_fileGrid.SetTileSize(LoadTileSize());
    m_settings.SetTileSize(m_fileGrid.TileSize());
    m_fileGrid.SetMissing(&m_missingPaths);
    int sortKey = static_cast<int>(m_sortKey);
    LoadSort(sortKey, m_sortDescending);
    m_sortKey = static_cast<SortKey>(sortKey);
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
    m_viewer.SetTags([this](const std::string &path, std::vector<std::string> &tags)
        {
            const file *f = m_lib.find_file(path);
            if (!f)
                return false;
            for (const tag *t : f->get_tags())
                tags.push_back(t->get_id());
            return true;
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
    m_titleBar.SetBounds(m_hWnd, topBlock);

    int searchTop = titleRow + (searchRow - Scale(theme::kSearchHeight)) / 2;
    // The sidebar button takes the left end of the search row while there is room for a sidebar, and the sort and Add
    // folder buttons its right end.
    int searchLeft = margin + (RoomForSidebar() ? Scale(theme::kSidebarToggle) + Scale(theme::kSidebarToggleGap) : 0);
    UpdateSortButton();
    RECT searchBar = { searchLeft, searchTop, std::max<LONG>(searchLeft, client.right - margin - m_titleBar.RowButtonsWidth()),
        searchTop + Scale(theme::kSearchHeight) };

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

    m_titleBar.ShowSidebarButton(SidebarButtonShown());
    m_titleBar.ShowRowButtons(!m_viewer.IsOpen() && !m_settings.IsOpen());
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
    m_tagSheet.SetBounds(client);
    m_folderTags.SetBounds(client);
    m_confirm.SetBounds(client);

    // Whatever moved, a tooltip does not stay behind where its button was.
    UpdateTooltip(CursorInWindow());
}

// Ctrl+F and Tab: the keyboard goes to the search, with its text selected so typing replaces it.
void Window::FocusSearch()
{
    m_sidebar.Blur();
    m_searchBar.Focus();
    m_searchBar.Select(0, m_searchBar.Query().size());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// The keyboard leaves the search for the grid. 'toFirstResult' (Enter, Down) selects the first file found, so the
// arrows, Space and Enter go on from there; with no result the search keeps the keyboard.
void Window::LeaveSearch(bool toFirstResult)
{
    if (toFirstResult)
    {
        const std::vector<file*> &files = m_fileGrid.Files();
        if (files.empty())
            return;
        m_fileGrid.SelectPath(files[0]->get_path());
        SelectionChanged();
    }
    m_searchBar.Blur();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// The tag filter takes the keyboard only while the sidebar shows.
bool Window::SidebarFieldReachable() const
{
    return m_sidebarOpen && RoomForSidebar();
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
    // Missing is the files of the last check of the disk, which the window keeps rather than the library. Pictures,
    // Videos and Books are the files of that kind.
    SidebarView view = m_sidebar.View();
    SearchResult result = m_lib.search(WideToUtf8(m_searchBar.Query()), view == SidebarView::kUntagged ? LibraryView::Untagged : LibraryView::All);
    if (view == SidebarView::kMissing)
    {
        result.files.erase(std::remove_if(result.files.begin(), result.files.end(),
            [this](const file *f) { return !m_missingPaths.count(f->get_path()); }), result.files.end());
    }
    else if (view == SidebarView::kPictures || view == SidebarView::kVideos || view == SidebarView::kBooks)
    {
        FileKind kind = view == SidebarView::kPictures ? FileKind::kPicture : view == SidebarView::kVideos ? FileKind::kVideo : FileKind::kBook;
        result.files.erase(std::remove_if(result.files.begin(), result.files.end(),
            [kind](const file *f) { return KindOf(f->get_path()) != kind; }), result.files.end());
    }
    m_searchBar.SetError(QueryError(result.error));
    if (result.valid())
    {
        SortFiles(result.files);
        m_fileGrid.SetFiles(result.files);
    }
    FileGrid::Empty empty = FileGrid::Empty::kNoMatches;
    if (m_lib.get_files().empty())
        empty = FileGrid::Empty::kLibrary;
    else if (view == SidebarView::kMissing && !m_missingChecked)
        empty = FileGrid::Empty::kChecking;
    m_fileGrid.SetEmpty(empty);
    UpdateBanner();
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
    m_viewer.RefreshTags();
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
    if (!m_missingCheck.Take(m_missingPaths, m_sizes))
        return;
    for (const std::string &path : m_foundSinceCheck)
        m_missingPaths.erase(path);
    m_foundSinceCheck.clear();
    m_missingChecked = true;
    CountViews();
    // The sizes came with the check.
    if (m_sortKey == SortKey::kSize)
        m_sortDirty = true;
    if (m_sidebar.View() == SidebarView::kMissing || m_sortKey == SortKey::kSize)
        RefreshResults();
    // Missing files are faded in every view.
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Shows a Library view, Missing as of the last check of the disk, while a new one runs.
void Window::ShowView(SidebarView view)
{
    if (view == SidebarView::kMissing)
        CheckMissing();
    m_sidebar.SetView(view);
    RefreshResults();
    InvalidateRect(m_hWnd, nullptr, FALSE);  // the sidebar button's mark
}

// The grid's files in the order picked with the sort button, by the rank RankFiles gave every file.
void Window::SortFiles(std::vector<file*> &files)
{
    if (m_sortDirty)
        RankFiles();
    std::vector<std::pair<size_t, file*>> ranked;
    ranked.reserve(files.size());
    bool fresh = false;
    for (file *f : files)
    {
        auto rank = m_sortRank.find(f);
        // A file the ranking doesn't know came since: rank them all again, once.
        if (rank == m_sortRank.end() && !fresh)
        {
            RankFiles();
            fresh = true;
            rank = m_sortRank.find(f);
        }
        ranked.push_back({ rank == m_sortRank.end() ? m_sortRank.size() : rank->second, f });
    }
    if (m_sortDescending)
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    else
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < files.size(); i++)
        files[i] = ranked[i].second;
}

// Ranks every file of the library by the sort key, from the first in ascending order: by when it was added (the
// order of its row in the database), its name as Explorer sorts names (2.jpg before 10.jpg), its size (as of the
// last check of the disk; unknown counts as 0) or its type (the extension), the name breaking ties.
void Window::RankFiles()
{
    struct Entry
    {
        file *f;
        std::int64_t added;
        std::wstring name;
        std::wstring type;
        std::uint64_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(m_lib.get_files().size());
    bool names = m_sortKey != SortKey::kAdded;
    for (file *f : m_lib.get_files())
    {
        Entry e = { f, f->get_db_id(), {}, {}, 0 };
        if (names)
        {
            std::string path = f->get_path();
            size_t slash = path.find_last_of("\\/");
            e.name = Utf8ToWide(slash == std::string::npos ? path : path.substr(slash + 1));
            if (m_sortKey == SortKey::kType)
                e.type = Utf8ToWide(FileExtension(path));
            else if (m_sortKey == SortKey::kSize)
            {
                auto size = m_sizes.find(path);
                e.size = size == m_sizes.end() ? 0 : size->second;
            }
        }
        entries.push_back(std::move(e));
    }

    auto byName = [](const Entry &a, const Entry &b)
    {
        int order = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
        return order != 0 ? order < 0 : a.added < b.added;
    };
    switch (m_sortKey)
    {
    case SortKey::kAdded:
        // Files are given ids as they are added; the library's order breaks ties between unsaved ones.
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.added < b.added; });
        break;
    case SortKey::kName:
        std::sort(entries.begin(), entries.end(), byName);
        break;
    case SortKey::kSize:
        std::sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b)
        {
            return a.size != b.size ? a.size < b.size : byName(a, b);
        });
        break;
    case SortKey::kType:
        std::sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b)
        {
            int order = a.type.compare(b.type);
            return order != 0 ? order < 0 : byName(a, b);
        });
        break;
    }

    m_sortRank.clear();
    m_sortRank.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); i++)
        m_sortRank[entries[i].f] = i;
    m_sortDirty = false;
}

// From the sort menu: the key and the order, kept for the next run.
void Window::SetSort(SortKey key, bool descending)
{
    if (key == m_sortKey && descending == m_sortDescending)
        return;
    m_sortDirty = m_sortDirty || key != m_sortKey;
    m_sortKey = key;
    m_sortDescending = descending;
    SaveSort(static_cast<int>(key), descending);
    LayoutComponents();
    RefreshResults();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

static const wchar_t *SortLabel(int key)
{
    static const wchar_t *const labels[] = { strings::kSortAdded, strings::kSortName, strings::kSortSize, strings::kSortType };
    return labels[key];
}

// The sort button names the key, unless the window is too narrow for words.
void Window::UpdateSortButton()
{
    RECT client;
    GetClientRect(m_hWnd, &client);
    std::wstring label = client.right >= Scale(theme::kSidebarMinWindow) ? SortLabel(static_cast<int>(m_sortKey)) : L"";
    m_titleBar.SetSort(label, m_sortDescending, TitleBar::SortButtonWidth(m_hWnd, m_renderer, label));
}

// The sort button's menu, under it: the keys, then the two orders named for the key in use.
void Window::OpenSortMenu()
{
    static const ContextMenu::Item keys[] = { ContextMenu::Item::kSortAdded, ContextMenu::Item::kSortName,
        ContextMenu::Item::kSortSize, ContextMenu::Item::kSortType };
    static const wchar_t *const ascending[] = { strings::kSortOldest, strings::kSortAToZ, strings::kSortSmallest, strings::kSortAToZ };
    static const wchar_t *const descending[] = { strings::kSortNewest, strings::kSortZToA, strings::kSortLargest, strings::kSortZToA };
    int key = static_cast<int>(m_sortKey);
    m_contextMenu.SetSortMarks(keys[key], m_sortDescending ? ContextMenu::Item::kSortDescending : ContextMenu::Item::kSortAscending,
        ascending[key], descending[key]);

    RECT button = m_titleBar.SortButtonRect();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, POINT{ button.right, button.bottom + Scale(4) }, ContextMenu::Kind::kSort, true);
    m_contextMenu.OnHover(CursorInWindow());
}

void Window::HandleTitleAction(TitleBar::Action action)
{
    if (action == TitleBar::Action::kToggleSidebar)
        ToggleSidebar();
    else if (action == TitleBar::Action::kSort)
        OpenSortMenu();
    else if (action == TitleBar::Action::kAddFolder)
        ChooseFolders();
}

// Missing says what it means over its files and offers to find them, or how far that got.
void Window::UpdateBanner()
{
    if (m_sidebar.View() != SidebarView::kMissing)
        m_fileGrid.SetBanner(L"", nullptr);
    else if (m_movedSearch.Running())
        m_fileGrid.SetBanner(std::wstring(strings::kLookingIn) + L" " + FolderName(m_movedSearch.Folder()) + L"…", strings::kCancel);
    else
        m_fileGrid.SetBanner(strings::kMissingBanner, strings::kFindMoved);
}

// "Find moved files..." in the Missing view: asks for a folder (the one the files were moved to, or one above it) and
// looks there for the missing files on a thread of its own (see MovedSearch). While it looks, the button cancels it.
void Window::FindMovedFiles()
{
    if (m_movedSearch.Running())
    {
        m_movedSearch.Stop();
        UpdateBanner();
        return;
    }
    std::vector<std::string> folders = PickFolders(m_hWnd, false);
    if (folders.empty())
        return;

    std::vector<MovedSearch::Missing> missing;
    for (const file *f : m_lib.get_files())
    {
        std::string path = f->get_path();
        if (m_missingPaths.count(path))
            missing.push_back({ path, f->get_identity() });
    }
    if (missing.empty())
        return;
    m_movedSearch.Start(m_hWnd, folders[0], std::move(missing));
    UpdateBanner();
}

// The search ended: what it found is shown, and only relinked if the user says so, since a file on a drive that
// reuses its file IDs could be taken for another.
void Window::OnMovedFound()
{
    std::string folder = m_movedSearch.Folder();
    std::vector<MovedSearch::Found> found;
    std::error_code error;
    if (!m_movedSearch.Take(found, error))
        return;
    UpdateBanner();

    // Only files still missing, going where no file of the library is. One found where the library already has a
    // file was imported again after it moved: said, rather than dropped without a word.
    std::vector<MovedSearch::Found> moves;
    size_t already = 0;
    for (MovedSearch::Found &f : found)
    {
        if (!m_lib.find_file(f.path) || !m_missingPaths.count(f.path))
            continue;
        if (m_lib.find_file(f.newPath))
            already++;
        else
            moves.push_back(std::move(f));
    }
    if (moves.empty())
    {
        if (error == std::errc::cross_device_link)
            ShowNotice(strings::kOtherDrive, true);
        else
        {
            std::wstring text = std::wstring(strings::kNoneMovedIn) + L" " + FolderName(folder);
            if (already > 0)
                text += L" \u00B7 " + FormatCount(already) + L" " + strings::kAlreadyThere;
            ShowNotice(text);
        }
        return;
    }

    size_t missing = 0;
    for (const file *f : m_lib.get_files())
        missing += m_missingPaths.count(f->get_path());
    ConfirmSheet::Question question;
    question.title = strings::kRelinkBefore + FormatCount(moves.size()) + strings::kRelinkOf + FormatCount(missing) +
        strings::kRelinkIn + FolderName(folder);
    question.body = strings::kRelinkBody;
    const size_t kShown = 5;
    for (size_t i = 0; i < moves.size() && i < kShown; i++)
        question.details.push_back(FolderName(moves[i].path) + L"  \u2192  " + Utf8ToWide(moves[i].newPath));
    if (moves.size() > kShown)
        question.details.push_back(std::wstring(strings::kRelinkMore) + L" " + FormatCount(moves.size() - kShown) + L" " + strings::kRelinkMoreAfter);
    question.confirm = strings::kRelinkConfirm;

    Ask(std::move(question), [this, moves]()
    {
        if (!BeginChange())
            return;
        for (const MovedSearch::Found &move : moves)
        {
            // Looked up again: the library may have changed while the question was up.
            file *f = m_lib.find_file(move.path);
            if (f && !m_lib.find_file(move.newPath))
                m_lib.relink(f, move.newPath);
        }
        m_sortDirty = true;
        FinishChange();
        if (!IsWindow(m_hWnd))
            return;

        // What the database kept: a failed save reloaded the library with the old paths, which stay missing.
        size_t relinked = 0;
        for (const MovedSearch::Found &move : moves)
        {
            if (m_lib.find_file(move.newPath))
            {
                m_missingPaths.erase(move.path);
                MoveSize(move.path, move.newPath);
                relinked++;
            }
        }
        LibraryChanged();
        if (relinked > 0)
            ShowNotice(std::wstring(strings::kRelinked) + L" " + FileCount(relinked));
    });
}

// A file now at another path keeps the size the last check of the disk read, for sorting by size.
void Window::MoveSize(const std::string &from, const std::string &to)
{
    auto size = m_sizes.find(from);
    if (size == m_sizes.end())
        return;
    std::uint64_t bytes = size->second;
    m_sizes.erase(size);
    m_sizes[to] = bytes;
}

static std::vector<std::string> PathsOf(const std::vector<file*> &files)
{
    std::vector<std::string> paths;
    paths.reserve(files.size());
    for (const file *f : files)
        paths.push_back(f->get_path());
    return paths;
}

// Ctrl+C and "Copy": the selected files go on the clipboard, to paste in Explorer or a chat.
void Window::CopySelected()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;
    if (shell_files::CopyToClipboard(m_hWnd, PathsOf(files)))
        ShowNotice(std::wstring(strings::kCopied) + L" " + FileCount(files.size()));
    else
        ShowNotice(strings::kCopyFailed, true);
}

// "Show in Explorer": a window on each folder the selected files are in, with them selected. Many folders ask first,
// like opening many files.
void Window::ShowSelectedInExplorer()
{
    std::vector<std::string> paths = PathsOf(m_fileGrid.SelectedFiles());
    size_t folders = shell_files::FolderCount(paths);
    if (folders == 0)
        return;
    if (folders > kOpenPromptThreshold)
    {
        ConfirmSheet::Question question;
        question.title = strings::kOpenBefore + FormatCount(folders) + strings::kShowFoldersAfter;
        question.confirm = strings::kOpenConfirm;
        Ask(std::move(question), [paths]() { shell_files::ShowInExplorer(paths); });
        return;
    }
    shell_files::ShowInExplorer(paths);
}

// Files dragged past the edge of the window leave it: Windows takes the drag over, and they can be dropped in Explorer,
// a chat or any program that takes files from Explorer, as copies. The call returns once they are dropped.
void Window::DragFilesOut()
{
    std::vector<std::string> paths = PathsOf(m_fileGrid.SelectedFiles());
    EndFileDrag();
    ReleaseCapture();
    if (paths.empty())
        return;
    OnHover(kNoPointer);
    m_draggingOut = true;
    shell_files::DragOut(m_hWnd, paths);
    PostMessage(m_hWnd, kDragOutDone, 0, 0);
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
// works on the selection. One on the grid's empty room offers to add a folder.
void Window::OpenContextMenu(POINT pt)
{
    file *f = m_fileGrid.FileAt(pt);
    if (!f)
    {
        if (!m_fileGrid.Contains(pt))
            return;
        m_searchBar.Blur();
        m_sidebar.Blur();
        OnHover(kNoPointer);
        m_contextMenu.Open(m_hWnd, pt, ContextMenu::Kind::kGrid);
        m_contextMenu.OnHover(pt);
        return;
    }
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (std::find(selected.begin(), selected.end(), f) == selected.end())
    {
        m_fileGrid.SelectPath(f->get_path());
        SelectionChanged();
    }
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, pt);
    m_contextMenu.OnHover(pt);
}

std::vector<file*> Window::TagSheetFiles() const
{
    if (!m_viewer.IsOpen())
        return m_fileGrid.SelectedFiles();
    file *f = m_lib.find_file(m_viewer.CurrentPath());
    return f ? std::vector<file*>{ f } : std::vector<file*>();
}

// Ctrl+T, "Add tag", the info pane's button and the strip's: the tag sheet asks for the tags to add to the selection.
// In the viewer, Ctrl+T and its add button ask for tags for the file shown.
void Window::OpenTagSheet()
{
    std::vector<file*> selected = TagSheetFiles();
    if (selected.empty())
    {
        if (!m_viewer.IsOpen())
            ShowNotice(strings::kTagNoSelection);
        return;
    }
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_tagSheet.Open(m_hWnd, m_renderer, client, selected);
    m_tagSheet.OnHover(CursorInWindow());
    m_viewer.SetCovered(true);
}

// The sheet that is open, if any. Only one is at a time, and it takes the mouse and the keyboard.
Sheet *Window::OpenSheet()
{
    Sheet *sheets[] = { &m_rename, &m_tagSheet, &m_folderTags, &m_confirm };
    for (Sheet *sheet : sheets)
    {
        if (sheet->IsOpen())
            return sheet;
    }
    return nullptr;
}

void Window::CloseSheets()
{
    m_rename.Close();
    m_tagSheet.Close();
    m_folderTags.Close();
    m_confirm.Close();
    m_confirmed = nullptr;
    m_viewer.SetCovered(false);
}

void Window::HandleSheetAction(Sheet *sheet, Sheet::Action action)
{
    if (sheet == &m_rename)
        HandleRenameAction(action);
    else if (sheet == &m_tagSheet)
        HandleTagSheetAction(action);
    else if (sheet == &m_folderTags)
        HandleFolderTagsAction(action);
    else if (sheet == &m_confirm)
        HandleConfirmAction(action);
    if (!IsWindow(m_hWnd))
        return;
    ResumeHeldResults();
    m_viewer.SetCovered(OpenSheet() != nullptr);
    OfferFolderTags();
}

// Asks before a change, in a sheet over the window: 'then' runs if the user confirms. The window keeps running while
// it is up, so 'then' finds what it changes again (by path or name) rather than holding pointers into the library.
void Window::Ask(ConfirmSheet::Question question, std::function<void()> then)
{
    m_contextMenu.Close();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_confirmed = std::move(then);
    m_confirm.Open(m_hWnd, m_renderer, client, std::move(question));
    m_confirm.OnHover(CursorInWindow());
}

void Window::HandleConfirmAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    std::function<void()> then = std::move(m_confirmed);
    m_confirmed = nullptr;
    m_confirm.Close();
    if (action == Sheet::Action::kConfirm && then)
        then();
    if (!IsWindow(m_hWnd))
        return;
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// While a sheet, the viewer, the settings, the menu or a drag is up, a moved-file search that ends waits to ask its
// question: under the viewer a video would cover it, and the others take the mouse and keys it needs.
bool Window::HoldMovedResult()
{
    return OpenSheet() || m_viewer.IsOpen() || m_settings.IsOpen() || m_contextMenu.IsOpen() || Dragging();
}

// A moved-file search held by HoldMovedResult asks its question once nothing holds it.
void Window::ResumeHeldResults()
{
    if (!m_movedPending || HoldMovedResult())
        return;
    m_movedPending = false;
    PostMessage(m_hWnd, kMovedFound, 0, 0);
}

// The files of the library at these paths, skipping the ones it no longer has.
std::vector<file*> Window::FilesAt(const std::vector<std::string> &paths) const
{
    std::vector<file*> files;
    files.reserve(paths.size());
    for (const std::string &path : paths)
    {
        if (file *f = m_lib.find_file(path))
            files.push_back(f);
    }
    return files;
}

void Window::RunMenuItem(ContextMenu::Item item)
{
    RunMenuItemNow(item);
    ResumeHeldResults();
    OfferFolderTags();
}

void Window::RunMenuItemNow(ContextMenu::Item item)
{
    if (item == ContextMenu::Item::kOpen)
        OpenSelected();
    else if (item == ContextMenu::Item::kShowInExplorer)
        ShowSelectedInExplorer();
    else if (item == ContextMenu::Item::kCopy)
        CopySelected();
    else if (item == ContextMenu::Item::kAddFolder)
        ChooseFolders();
    else if (item == ContextMenu::Item::kSortAdded)
        SetSort(SortKey::kAdded, true);
    else if (item == ContextMenu::Item::kSortName)
        SetSort(SortKey::kName, false);
    else if (item == ContextMenu::Item::kSortSize)
        SetSort(SortKey::kSize, true);
    else if (item == ContextMenu::Item::kSortType)
        SetSort(SortKey::kType, false);
    else if (item == ContextMenu::Item::kSortAscending || item == ContextMenu::Item::kSortDescending)
        SetSort(m_sortKey, item == ContextMenu::Item::kSortDescending);
    else if (item == ContextMenu::Item::kAddTag)
        OpenTagSheet();
    else if (item == ContextMenu::Item::kRenameTag)
        StartTagRename(m_menuTag);
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
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renamePath = f->get_path();
    m_renameTag.clear();
    std::wstring wide = Utf8ToWide(m_renamePath);
    size_t slash = wide.find_last_of(L"\\/");
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_rename.Open(m_hWnd, m_renderer, client, slash == std::wstring::npos ? wide : wide.substr(slash + 1));
    m_rename.OnHover(CursorInWindow());
}

// The tag sheet's answer: Enter adds the typed tags to the files it was opened for (still the selection: nothing can
// change it while the sheet is up).
void Window::HandleTagSheetAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    TagEdit edit;
    if (action == Sheet::Action::kConfirm)
    {
        edit.kind = TagEdit::kAdd;
        edit.tags = m_tagSheet.Tags();
    }
    m_tagSheet.Close();

    std::vector<file*> files = TagSheetFiles();
    size_t lacking = edit.kind == TagEdit::kAdd ? FilesLacking(m_lib, files, edit.tags) : 0;
    int changed = ApplyTagEdit(edit, files);
    if (edit.kind == TagEdit::kAdd)
        NoticeTagged(changed, lacking, edit.tags);
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Sidebar tag menu and F2 over a tag: the rename sheet asks for the tag's new name.
void Window::StartTagRename(const std::string &name)
{
    if (!m_lib.find_tag(name))
        return;
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renameTag = name;
    m_renamePath.clear();
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_rename.Open(m_hWnd, m_renderer, client, Utf8ToWide(name), true, strings::kTagRenameTitle);
    m_rename.OnHover(CursorInWindow());
}

// Renames the tag m_renameTag to what the sheet holds. Another tag with that name (case aside) takes this one's files
// and keeps its own spelling: the sheet closes and a question asks first. If the search names the old tag, it follows
// the tag. On failure 'error' says why, to show in the sheet.
bool Window::RenameTag(std::wstring &error)
{
    tag *t = m_lib.find_tag(m_renameTag);
    if (!t)
        return true;  // gone meanwhile: nothing to rename

    // Like the folder names of an import, a name with spaces becomes one word.
    std::string name = WideToUtf8(m_rename.Name());
    if (!is_valid_tag_name(name))
        name = normalize_tag_name(name);
    if (name.empty())
    {
        error = strings::kTagRenameInvalid;
        return false;
    }

    if (t->get_id() == name)
        return true;
    tag *other = m_lib.find_tag(name);
    if (!other || other == t)
    {
        RenameTagNow(t, name);
        return true;
    }

    std::string from = t->get_id(), into = other->get_id();
    ConfirmSheet::Question question;
    question.title = strings::kMergeBefore + Utf8ToWide(from) + strings::kMergeInto + Utf8ToWide(into) + L"?";
    question.body = Utf8ToWide(into) + strings::kMergeExists + Utf8ToWide(from) + strings::kMergeGetIt;
    question.confirm = strings::kMergeConfirm;
    m_rename.Close();
    Ask(std::move(question), [this, from, into]()
    {
        if (tag *source = m_lib.find_tag(from))
            RenameTagNow(source, into);
    });
    return true;
}

// Renames 't' to 'name', or merges it into the tag that has that name (case aside), as one change Ctrl+Z takes back.
// A merge is kept as the tags it adds and takes off, so undoing it puts each file's tags back as they were.
void Window::RenameTagNow(tag *t, const std::string &name)
{
    std::string oldName = t->get_id();
    UndoStep step;
    step.tagFrom = oldName;
    tag *other = m_lib.find_tag(name);
    if (other && other != t)
    {
        step.merge = true;
        UndoStep::Links gain{ other->get_id(), {}, true }, loss{ oldName, {}, false };
        for (const file *f : t->get_files())
        {
            loss.paths.push_back(f->get_path());
            if (!f->tagged(other))
                gain.paths.push_back(f->get_path());
        }
        step.links = { std::move(gain), std::move(loss) };
    }

    tag *result = nullptr;
    if (!BeginChange())
        return;
    rename_result outcome = m_lib.rename_tag(t, name, &result);
    bool saved = FinishChange();
    if (!IsWindow(m_hWnd))
        return;
    // A failed commit makes FinishChange reload the library: 't' and 'result' are gone, so look the tag up again.
    result = m_lib.find_tag(name);
    if (!saved || !result || (outcome != rename_result::Renamed && outcome != rename_result::Merged))
    {
        if (saved && (outcome == rename_result::Invalid || outcome == rename_result::Failed))
            ShowNotice(outcome == rename_result::Invalid ? strings::kTagRenameInvalid : strings::kTagRenameFailed, true);
        return;
    }

    step.followed = FollowTagInQuery(oldName, result->get_id());
    std::wstring shown = Utf8ToWide(result->get_id());
    step.tagTo = result->get_id();
    if (outcome == rename_result::Merged)
        step.label = std::wstring(strings::kTagMerged) + L" " + shown + L" \u00B7 " + FileCount(result->get_files().size());
    else
        step.label = std::wstring(strings::kTagRenamed) + L" " + shown;
    ShowNotice(step.label);
    Remember(std::move(step));
}

// The search followed the sidebar: a tag it asked for under the old name is asked for under the new one. Returns
// whether the search named it.
bool Window::FollowTagInQuery(const std::string &from, const std::string &to)
{
    std::string query = WideToUtf8(m_searchBar.Query());
    auto states = TagStates(query);
    auto state = states.find(fold_case(from));
    if (state == states.end())
        return false;
    query = WithTagState(query, from, TagState::kOff);
    if (CanFilterBy(to))
        query = WithTagState(query, to, state->second);
    SetQuery(Utf8ToWide(query));
    return true;
}

void Window::HandleRenameAction(Sheet::Action action)
{
    if (action == Sheet::Action::kCancel)
        m_rename.Close();
    else if (action == Sheet::Action::kConfirm)
    {
        std::wstring error;
        bool done = m_renameTag.empty() ? RenameFile(m_renamePath, m_rename.Name(), error) : RenameTag(error);
        if (done)
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
    MoveSize(path, newPath);
    m_sortDirty = true;
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
    std::vector<std::string> paths = PathsOf(files);
    if (paths.size() > kOpenPromptThreshold)
    {
        ConfirmSheet::Question question;
        question.title = strings::kOpenBefore + FormatCount(paths.size()) + strings::kOpenAfter;
        question.body = strings::kOpenBody;
        question.confirm = strings::kOpenConfirm;
        Ask(std::move(question), [this, paths]()
        {
            for (const std::string &path : paths)
                OpenWithDefaultProgram(m_hWnd, path);
        });
        return;
    }

    for (const std::string &path : paths)
        OpenWithDefaultProgram(m_hWnd, path);
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
    ResumeHeldResults();
    OfferFolderTags();
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
    case Viewer::Action::kAddTag:
        OpenTagSheet();
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

// "Add folder..." (under an empty library, the + of the search row, the grid's menu, Ctrl+O): the folders picked are
// added as if they had been dropped.
void Window::ChooseFolders()
{
    AddPaths(PickFolders(m_hWnd, true));
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
    std::vector<std::string> unopened, roots;
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
            else
                roots.push_back(path);
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
        m_importRoots = std::move(roots);
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
    std::vector<std::string> unopened, roots;
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        found = std::move(m_importResult);
        unopened = std::move(m_importUnopened);
        roots = std::move(m_importRoots);
        m_importResult = folder_scan();
        m_importRoots.clear();
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
            m_sortDirty = true;
            LibraryChanged();
            // The new files' sizes, for sorting by size, come with a check of the disk.
            if (added > 0 && m_sortKey == SortKey::kSize)
                CheckMissing();

            // The folder names of the files that are new, for the user to tick (see OfferFolderTags).
            std::vector<scanned_file> fresh;
            for (const scanned_file &s : found.files)
            {
                if (!already.count(s.path))
                    fresh.push_back(s);
            }
            std::unordered_map<std::string, size_t> offered;  // by folded name
            for (size_t i = 0; i < m_folderTagOffer.size(); i++)
                offered[fold_case(m_folderTagOffer[i].name)] = i;
            for (const std::string &root : roots)
            {
                for (const folder_tag_suggestion &suggestion : suggest_folder_tags(root, fresh))
                {
                    auto slot = offered.emplace(fold_case(suggestion.name), m_folderTagOffer.size());
                    if (slot.second)
                    {
                        // A tag that exists keeps its spelling: Beach goes on beach.
                        tag *existing = m_lib.find_tag(suggestion.name);
                        m_folderTagOffer.push_back({ existing ? existing->get_id() : suggestion.name, {}, true });
                    }
                    std::vector<std::string> &paths = m_folderTagOffer[slot.first->second].paths;
                    for (size_t index : suggestion.files)
                        paths.push_back(fresh[index].path);
                }
            }
            for (FolderTag &offer : m_folderTagOffer)
            {
                std::sort(offer.paths.begin(), offer.paths.end());
                offer.paths.erase(std::unique(offer.paths.begin(), offer.paths.end()), offer.paths.end());
            }
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
    OfferFolderTags();
}

// After the last of the imports under way: asks which folder names to use as tags on the files they added. Nothing is
// tagged unless the user ticks it (and no rule picks tags by file type). A cancelled import offers nothing.
void Window::OfferFolderTags()
{
    if (m_folderTagOffer.empty())
        return;
    // Busy: the offer waits, and is made again when the sheet, viewer, settings, menu or drag closes.
    if (OpenSheet() || m_viewer.IsOpen() || m_settings.IsOpen() || m_contextMenu.IsOpen() || Dragging())
        return;
    std::vector<FolderTag> offer = std::move(m_folderTagOffer);
    m_folderTagOffer.clear();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_folderTags.Open(m_hWnd, m_renderer, client, std::move(offer));
    m_folderTags.OnHover(CursorInWindow());
}

// Tags the files of every ticked folder name, as one change.
void Window::HandleFolderTagsAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    std::vector<FolderTag> tags;
    if (action == Sheet::Action::kConfirm)
        tags = m_folderTags.Tags();
    m_folderTags.Close();

    std::unordered_set<file*> tagged;
    if (std::any_of(tags.begin(), tags.end(), [](const FolderTag &t) { return t.on; }) && BeginChange())
    {
        bool failed = false;
        UndoStep step;
        for (const FolderTag &entry : tags)
        {
            if (!entry.on)
                continue;
            std::vector<file*> files = FilesAt(entry.paths);
            tag *t = m_lib.find_tag(entry.name);
            if (!t)
                t = m_lib.retrieve_tag(entry.name);
            UndoStep::Links links{ t ? t->get_id() : entry.name, {}, true };
            for (const file *f : files)
            {
                if (t && !f->tagged(t))
                    links.paths.push_back(f->get_path());
            }
            if (!t || m_lib.edit_files(files, { t }, true) < 0)
            {
                failed = true;
                break;
            }
            tagged.insert(files.begin(), files.end());
            if (!links.paths.empty())
                step.links.push_back(std::move(links));
        }
        bool saved = FinishChange();
        if (!IsWindow(m_hWnd))
            return;
        if (!failed && saved && !tagged.empty())
        {
            step.label = std::wstring(strings::kTaggedFiles) + L" " + FileCount(tagged.size());
            ShowNotice(step.label);
            if (!step.links.empty())
                Remember(std::move(step));
        }
    }

    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
    // An import that ended while the sheet was up waits for it.
    if (m_importDonePending)
    {
        m_importDonePending = false;
        PostMessage(m_hWnd, kImportDone, 0, 0);
    }
}

// A line in the import bar for a few seconds, the way an import reports. Not while one is running.
void Window::ShowNotice(const std::wstring &text, bool error)
{
    if (m_importThread.joinable())
        return;
    bool wasVisible = m_importBar.Visible();
    m_importBar.ShowResult(text, error);
    if (!wasVisible)
    {
        LayoutComponents();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
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
    std::optional<SidebarView> view;
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
    ResumeHeldResults();
    OfferFolderTags();
}

void Window::EndFileDrag()
{
    KillTimer(m_hWnd, kDragScrollTimer);
    m_fileDragPending = m_fileDragDeferred = m_fileDragActive = false;
    m_sidebar.SetDropTag(std::string());
    InvalidateRect(m_hWnd, nullptr, FALSE);
    ResumeHeldResults();
    OfferFolderTags();
}

void Window::ApplyTagEdit(const TagEdit &edit)
{
    ApplyTagEdit(edit, m_fileGrid.SelectedFiles());
}

// Says what an addition by the sheet or a drop did: "Tagged 12 files · anime", counting the files that lacked a tag,
// or that every file had the tags.
void Window::NoticeTagged(int changed, size_t files, const std::vector<std::string> &tags)
{
    if (changed < 0)
        return;
    std::wstring names;
    for (const std::string &name : tags)
        names += (names.empty() ? L"" : L" ") + Utf8ToWide(name);
    ShowNotice(changed > 0 ? std::wstring(strings::kTaggedFiles) + L" " + FileCount(files) + L" · " + names
                           : std::wstring(strings::kAlreadyTagged));
}

// Adds or removes the tags on every file of 'files' as one change, saved all or none. A tag a removal leaves on no
// file at all is deleted. Returns how many file-tag links changed, or -1 if nothing was.
int Window::ApplyTagEdit(const TagEdit &edit, const std::vector<file*> &files)
{
    if (edit.kind == TagEdit::kNone || edit.tags.empty() || files.empty())
        return -1;

    if (!BeginChange())
        return -1;
    bool add = edit.kind == TagEdit::kAdd;
    std::vector<tag*> tags;
    for (const std::string &name : edit.tags)
    {
        if (tag *t = add ? m_lib.retrieve_tag(name) : m_lib.find_tag(name))
            tags.push_back(t);
    }

    // What changes, for Ctrl+Z: per tag, the files that didn't have it (or did).
    UndoStep step;
    std::unordered_set<const file*> touched;
    std::wstring names;
    for (const tag *t : tags)
    {
        UndoStep::Links links{ t->get_id(), {}, add };
        for (const file *f : files)
        {
            if (f->tagged(t) != add)
            {
                links.paths.push_back(f->get_path());
                touched.insert(f);
            }
        }
        if (!links.paths.empty())
        {
            names += (names.empty() ? L"" : L" ") + Utf8ToWide(t->get_id());
            step.links.push_back(std::move(links));
        }
    }

    int changed = m_lib.edit_files(files, tags, add);
    if (changed >= 0 && !add)
    {
        for (tag *t : tags)
        {
            if (t->get_files().empty())
                m_lib.delete_tag(t);
        }
    }
    bool saved = FinishChange();
    if (saved && changed > 0 && IsWindow(m_hWnd))
    {
        step.label = add ? std::wstring(strings::kTaggedFiles) + L" " + FileCount(touched.size()) + L" \u00B7 " + names
                         : std::wstring(strings::kUntaggedBefore) + L" " + names + L" " + strings::kUntaggedFrom + L" " + FileCount(touched.size());
        Remember(std::move(step));
    }
    return saved ? changed : -1;
}

// Keeps a change for Ctrl+Z. A new change can't be redone over: the steps undone before it are dropped.
void Window::Remember(UndoStep step)
{
    const size_t kUndoLimit = 100;
    m_undo.push_back(std::move(step));
    if (m_undo.size() > kUndoLimit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

// Ctrl+Z takes back the last change, Ctrl+Y makes the last change undone again, and the notice says which.
void Window::Undo(bool redo)
{
    std::vector<UndoStep> &from = redo ? m_redo : m_undo;
    if (from.empty())
    {
        ShowNotice(redo ? strings::kNothingToRedo : strings::kNothingToUndo);
        return;
    }
    UndoStep step = std::move(from.back());
    from.pop_back();
    if (!RunStep(step, redo))
    {
        if (IsWindow(m_hWnd))
            ShowNotice(strings::kUndoFailed, true);
        return;
    }
    if (!IsWindow(m_hWnd))
        return;
    ShowNotice(std::wstring(redo ? strings::kRedone : strings::kUndone) + L" \u00B7 " + step.label);
    (redo ? m_undo : m_redo).push_back(std::move(step));
}

// Makes a change again ('forward') or takes it back, as one change saved all or none. Files and tags gone since are
// skipped. Returns false if the change could not be saved, or a tag rename would now merge with another tag.
bool Window::RunStep(const UndoStep &step, bool forward)
{
    if (!step.tagFrom.empty() && !step.merge)
    {
        const std::string &from = forward ? step.tagFrom : step.tagTo;
        const std::string &to = forward ? step.tagTo : step.tagFrom;
        tag *t = m_lib.find_tag(from);
        tag *other = m_lib.find_tag(to);
        if (!t || (other && other != t) || !BeginChange())
            return false;
        rename_result outcome = m_lib.rename_tag(t, to);
        bool saved = FinishChange();
        if (!IsWindow(m_hWnd) || !saved || (outcome != rename_result::Renamed && outcome != rename_result::Unchanged))
            return false;
        FollowTagInQuery(from, to);
        return true;
    }

    if (!BeginChange())
        return false;
    if (forward)
    {
        for (const UndoStep::Links &links : step.links)
            ApplyLinks(links, links.add);
        if (!step.removed.empty())
        {
            std::vector<std::string> paths;
            for (const file_snapshot &f : step.removed)
                paths.push_back(f.path);
            TakeOutOfLibrary(FilesAt(paths));
        }
    }
    else
    {
        if (!step.removed.empty())
        {
            m_lib.restore_files(step.removed);
            m_sortDirty = true;
        }
        for (auto links = step.links.rbegin(); links != step.links.rend(); ++links)
            ApplyLinks(*links, !links->add);
    }
    bool saved = FinishChange();
    // A merge moves the search's tag back only if doing it moved it: the target may have been in the search before.
    if (saved && step.merge && step.followed && IsWindow(m_hWnd))
        FollowTagInQuery(forward ? step.tagFrom : step.tagTo, forward ? step.tagTo : step.tagFrom);
    return saved;
}

// Adds the tag to the files at the paths, or takes it off them (and deletes it if no file has it any more, as the
// chips do). Part of a change: the caller begins and finishes it.
void Window::ApplyLinks(const UndoStep::Links &links, bool add)
{
    std::vector<file*> files = FilesAt(links.paths);
    tag *t = add ? m_lib.retrieve_tag(links.tag) : m_lib.find_tag(links.tag);
    if (!t || files.empty())
        return;
    if (m_lib.edit_files(files, { t }, add) >= 0 && !add && t->get_files().empty())
        m_lib.delete_tag(t);
}

// Takes the selected files out of the library, after asking. The files on disk are not touched. Like a tag
// removal, it deletes the tags that only these files had.
void Window::RemoveSelectedFiles()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;

    std::vector<std::string> paths = PathsOf(files);
    ConfirmSheet::Question question;
    question.title = strings::kRemoveBefore + (paths.size() == 1 ? FolderName(paths[0]) : FileCount(paths.size())) + strings::kRemoveAfter;
    question.body = strings::kRemoveBody;
    question.confirm = strings::kRemoveConfirm;
    Ask(std::move(question), [this, paths]()
    {
        std::vector<file*> files = FilesAt(paths);
        if (!files.empty())
            RemoveFromLibrary(files, true);
    });
}

// Takes 'files' out of the library, and the tags only they had, as one change. The files on disk are not touched.
// 'undoable': Ctrl+Z puts them back with their tags (not after they went to the Recycle Bin).
void Window::RemoveFromLibrary(const std::vector<file*> &files, bool undoable)
{
    UndoStep step;
    if (undoable)
        step.removed = m_lib.snapshot_files(files);
    if (!BeginChange())
        return;
    TakeOutOfLibrary(files);
    bool saved = FinishChange();
    if (!undoable || !saved || !IsWindow(m_hWnd) || step.removed.empty())
        return;
    step.label = std::wstring(strings::kRemovedFiles) + L" " + FileCount(step.removed.size()) + L" " + strings::kFromLibrary;
    ShowNotice(step.label);
    Remember(std::move(step));
}

// Takes 'files' out of the library, and the tags only they had. Part of a change: the caller begins and finishes it.
void Window::TakeOutOfLibrary(const std::vector<file*> &files)
{
    std::unordered_set<tag*> tags;
    for (const file *f : files)
    {
        for (tag *t : f->get_tags())
            tags.insert(t);
    }
    // The grid points to the files about to be deleted; it lets go of them first, and so does the ranking.
    m_fileGrid.Clear();
    m_sortRank.clear();
    m_sortDirty = true;
    if (m_lib.remove_files(files) < 0)
        return;
    for (tag *t : tags)
    {
        if (t->get_files().empty())
            m_lib.delete_tag(t);
    }
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

    std::vector<std::string> selected = PathsOf(files);
    ConfirmSheet::Question question;
    question.title = strings::kDeleteBefore + (selected.size() == 1 ? FolderName(selected[0]) : FileCount(selected.size())) +
        strings::kDeleteAfter;
    question.body = strings::kDeleteBody;
    question.confirm = strings::kDeleteConfirm;
    question.danger = true;
    Ask(std::move(question), [this, selected]()
    {
        std::vector<std::wstring> paths;
        for (const std::string &path : selected)
            paths.push_back(Utf8ToWide(path));
        std::wstring error;
        std::unordered_set<std::wstring> left;
        for (std::wstring &path : RecycleFiles(m_hWnd, paths, error))
            left.insert(std::move(path));

        // Looked up again: the library may have changed while the Recycle Bin was busy.
        std::vector<std::string> gone;
        for (size_t i = 0; i < paths.size(); i++)
        {
            if (!left.count(paths[i]))
                gone.push_back(selected[i]);
        }
        std::vector<file*> goneFiles = FilesAt(gone);
        if (!goneFiles.empty())
            RemoveFromLibrary(goneFiles, false);
        if (!error.empty() && IsWindow(m_hWnd))
        {
            while (!error.empty() && (error.back() == L'\n' || error.back() == L'\r' || error.back() == L' '))
                error.pop_back();
            ShowNotice(std::wstring(strings::kDeleteFailed) + L" " + error, true);
        }
    });
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
// then reloads the library to match), and shows the result. Returns whether it was saved.
bool Window::FinishChange()
{
    database *db = m_lib.attached();
    bool saved = true;
    if (db && (!db->last_error().empty() || !db->commit()))
    {
        db->rollback();
        saved = false;
    }

    ReportSaveError();
    if (!IsWindow(m_hWnd))
        return false;
    LibraryChanged();
    return saved;
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

    // The grid points into the library; it lets go before the library deletes its files, and so does the ranking.
    m_fileGrid.Clear();
    m_sortRank.clear();
    m_sortDirty = true;
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
        m_movedSearch.Stop();
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
        if (active && m_sidebar.View() == SidebarView::kMissing)
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

    case kMovedFound:
        if (!IsWindowEnabled(m_hWnd) || HoldMovedResult())
            m_movedPending = true;
        else
            OnMovedFound();
        return 0;

    case kDragOutDone:
        m_draggingOut = false;
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
            SetCursor(m_viewer.CursorHidden() && !OpenSheet() ? nullptr : LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (LOWORD(lParam) == HTCLIENT && (m_searchBar.Contains(pt) || m_sidebar.OverField(pt)))
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
        if (Sheet *sheet = OpenSheet())
        {
            sheet->OnChar(static_cast<wchar_t>(wParam));
            return 0;
        }
        if (m_viewer.IsOpen() || m_settings.IsOpen() || Dragging())
            return 0;
        if (m_searchBar.IsFocused() && m_searchBar.OnChar(static_cast<wchar_t>(wParam)))
            RefreshResults();
        else if (m_sidebar.IsFocused())
            m_sidebar.OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_KEYDOWN:
    {
        m_tooltip.Dismiss(m_hWnd);
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool repeat = (lParam & 0x40000000) != 0;  // the key was already down: it is held

        // Esc drops a tag or files being dragged.
        if (Dragging())
        {
            if (wParam == VK_ESCAPE)
            {
                EndTagDrag();
                EndFileDrag();
                ReleaseCapture();
            }
            return 0;
        }

        // A sheet takes every key while it is open.
        if (Sheet *sheet = OpenSheet())
        {
            HandleSheetAction(sheet, sheet->OnKeyDown(wParam, ctrl));
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

        // Ctrl+T: add tags to the selected files, from anywhere.
        if (ctrl && wParam == 'T')
        {
            OpenTagSheet();
            return 0;
        }

        // Ctrl+O: add a folder, from anywhere.
        if (ctrl && wParam == 'O')
        {
            ChooseFolders();
            return 0;
        }

        // Ctrl+F: to the search, its text selected to type over.
        if (ctrl && wParam == 'F')
        {
            FocusSearch();
            return 0;
        }

        // Tab goes round the search, the grid and the tag filter; Shift+Tab the other way.
        if (m_searchBar.IsFocused())
        {
            // Esc empties the search, then leaves it. Enter and Down go to the first result.
            if (wParam == VK_ESCAPE)
            {
                if (m_searchBar.Query().empty())
                    LeaveSearch(false);
                else
                    SetQuery(L"");
            }
            else if (wParam == VK_RETURN || wParam == VK_DOWN)
                LeaveSearch(true);
            else if (wParam == VK_TAB)
            {
                LeaveSearch(false);
                if (shift && SidebarFieldReachable())
                    m_sidebar.Focus();
            }
            else if (m_searchBar.OnKeyDown(wParam, ctrl))
                RefreshResults();
            return 0;
        }

        if (m_sidebar.IsFocused())
        {
            if (wParam == VK_TAB)
            {
                m_sidebar.Blur();
                if (!shift)
                    FocusSearch();
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            else
                m_sidebar.OnKeyDown(wParam, ctrl);
            return 0;
        }

        if (wParam == VK_TAB)
        {
            if (!shift && SidebarFieldReachable())
            {
                m_sidebar.Focus();
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            else
                FocusSearch();
            return 0;
        }

        // Ctrl+Z takes the last change to tags or the library back; Ctrl+Y (or Ctrl+Shift+Z) makes it again.
        if (ctrl && (wParam == 'Z' || wParam == 'Y'))
        {
            Undo(wParam == 'Y' || shift);
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

        // Ctrl+C: the selected files to the clipboard. In a text field it copies the text instead (see above).
        if (ctrl && wParam == 'C')
        {
            CopySelected();
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

        // F2 renames: the tag under the pointer in the sidebar, else the file. Del takes files out of the library;
        // Shift+Del also sends them to the Recycle Bin.
        if (wParam == VK_F2)
        {
            std::string overTag = m_sidebar.TagNameAt(CursorInWindow());
            if (!overTag.empty())
                StartTagRename(overTag);
            else
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

        if (Sheet *sheet = OpenSheet())
        {
            HandleSheetAction(sheet, sheet->OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK));
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
                TitleBar::Action action = TitleBar::Action::kNone;
                m_titleBar.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK, action);
                HandleTitleAction(action);
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
                TitleBar::Action action = TitleBar::Action::kNone;
                m_titleBar.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK, action);
                HandleTitleAction(action);
            }
            return 0;
        }

        // The second click of a double-click on a grid button is dropped: the first one may have filled the grid
        // ("Clear filters"), and it would select or open the file now under the pointer.
        bool afterGridButton = uMsg == WM_LBUTTONDBLCLK && m_pressedGridButton;
        m_pressedGridButton = false;

        // Every click goes to the text fields first so they can drop focus when clicked outside. The sidebar's goes
        // first: there is only one caret, and the search bar may be about to take it.
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
                OpenTagSheet();
            return 0;
        }
        TagEdit edit;
        bool openTagSheet = false;
        if (m_tagPanel.OnLButtonDown(pt, edit, openTagSheet))
        {
            // A chip changes under the cursor once clicked (it goes away, or moves to the tags every file has),
            // so the second click of a double-click would hit another one.
            if (uMsg == WM_LBUTTONDOWN)
            {
                if (openTagSheet)
                    OpenTagSheet();
                else
                    ApplyTagEdit(edit);
            }
            return 0;
        }
        bool cancel = false;
        if (m_importBar.OnLButtonDown(pt, cancel))
        {
            if (cancel && uMsg == WM_LBUTTONDOWN)
                CancelImport();
            return 0;
        }
        TitleBar::Action titleAction = TitleBar::Action::kNone;
        if (m_titleBar.OnLButtonDown(pt, uMsg == WM_LBUTTONDBLCLK, titleAction))
        {
            HandleTitleAction(titleAction);
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
            else if (button == FileGrid::Button::kBanner)
                FindMovedFiles();
            else
            {
                // Back to every file, as a filtered view can hold none.
                m_sidebar.SetView(SidebarView::kAll);
                SetQuery(L"");
                InvalidateRect(m_hWnd, nullptr, FALSE);
            }
            return 0;
        }
        // A press on a file starts a drag of the selection onto a tag, or out of the window. One on a file that is
        // already selected leaves the selection alone until the release, so that all of it can be dragged.
        bool ctrlDown = (wParam & MK_CONTROL) != 0, shiftDown = (wParam & MK_SHIFT) != 0;
        file *pressed = uMsg == WM_LBUTTONDOWN && !ctrlDown && !shiftDown ? m_fileGrid.FileAt(pt) : nullptr;
        bool deferred = false;
        if (pressed)
        {
            std::vector<file*> selected = m_fileGrid.SelectedFiles();
            deferred = std::find(selected.begin(), selected.end(), pressed) != selected.end();
        }
        if (!deferred && m_fileGrid.OnLButtonDown(pt, ctrlDown, shiftDown))
            SelectionChanged();
        if (pressed)
        {
            m_fileDragPending = true;
            m_fileDragDeferred = deferred;
            m_fileDragActive = false;
            m_fileDragStart = m_fileDragPoint = pt;
            SetCapture(m_hWnd);
        }

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
        // A drag waits for its drop: Esc is the way out.
        if (Dragging())
            return 0;
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool wasOpen = m_contextMenu.IsOpen();
        m_contextMenu.Close();
        if (!wasOpen && !m_viewer.IsOpen() && !m_settings.IsOpen() && !OpenSheet())
        {
            // A tag of the sidebar has a menu of its own.
            std::string overTag = m_sidebar.TagNameAt(pt);
            if (overTag.empty())
                OpenContextMenu(pt);
            else
            {
                m_menuTag = overTag;
                m_searchBar.Blur();
                m_sidebar.Blur();
                OnHover(kNoPointer);
                m_contextMenu.Open(m_hWnd, pt, ContextMenu::Kind::kTag);
                m_contextMenu.OnHover(pt);
            }
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
        if (Sheet *sheet = OpenSheet())
        {
            sheet->OnMouseMove(pt);
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
        if (m_fileDragPending)
        {
            if (!m_fileDragActive && (abs(pt.x - m_fileDragStart.x) >= Scale(5) || abs(pt.y - m_fileDragStart.y) >= Scale(5)))
                m_fileDragActive = true;
            if (m_fileDragActive)
            {
                // Past the edge of the window, other programs take the files.
                RECT client;
                GetClientRect(m_hWnd, &client);
                if (!PtInRect(&client, pt))
                {
                    DragFilesOut();
                    return 0;
                }
                m_fileDragPoint = pt;
                // Resting at the top or bottom edge of the tags keeps scrolling them.
                if (m_sidebar.DragScroll(pt))
                    SetTimer(m_hWnd, kDragScrollTimer, theme::kFrameMs * 2, nullptr);
                else
                    KillTimer(m_hWnd, kDragScrollTimer);
                m_sidebar.SetDropTag(m_sidebar.TagNameAt(pt));
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
        if (m_fileDragPending)
        {
            bool active = m_fileDragActive, deferred = m_fileDragDeferred;
            POINT start = m_fileDragStart;
            std::string target = active ? m_sidebar.TagNameAt(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }) : std::string();
            EndFileDrag();
            ReleaseCapture();
            if (!active)
            {
                // It was a click after all: the one the press put off.
                if (deferred && m_fileGrid.OnLButtonDown(start, false, false))
                    SelectionChanged();
            }
            else if (!target.empty())
            {
                std::vector<file*> files = m_fileGrid.SelectedFiles();
                TagEdit edit;
                edit.kind = TagEdit::kAdd;
                edit.tags.push_back(target);
                size_t lacking = FilesLacking(m_lib, files, edit.tags);
                NoticeTagged(ApplyTagEdit(edit, files), lacking, edit.tags);
            }
            return 0;
        }
        if (m_settings.Dragging())
            ReleaseCapture();
        m_settings.OnLButtonUp();
        m_rename.OnLButtonUp();
        m_tagSheet.OnLButtonUp();
        m_titleBar.OnLButtonUp();
        m_fileGrid.OnLButtonUp();
        m_viewer.OnLButtonUp();
        return 0;

    case WM_CAPTURECHANGED:
        if (!m_dragTag.empty())
            EndTagDrag();
        if (m_fileDragPending)
            EndFileDrag();
        m_settings.OnCaptureChanged();
        m_rename.OnCaptureChanged();
        m_tagSheet.OnCaptureChanged();
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
        if (Sheet *sheet = OpenSheet())
        {
            sheet->OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        if (Dragging())
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
        // Files of the library dragged out and dropped back in are not imported again.
        if (m_draggingOut)
        {
            DragFinish(reinterpret_cast<HDROP>(wParam));
            return 0;
        }
        // The import bar and its outcome are under the viewer: it closes first, so the drop doesn't go unseen.
        CloseViewer();
        CloseSettings();
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    // A dialog (a MessageBox asking to remove or open files, say) disables the window and runs its own message
    // loop, which would hand us this message while the code that showed the dialog is paused. Adding the files
    // then could reload the library under that code's feet, so it waits until the window is enabled again.
    case kImportDone:
        if (!IsWindowEnabled(m_hWnd) || m_folderTags.IsOpen())
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
        if (wParam && m_movedPending)
        {
            m_movedPending = false;
            PostMessage(m_hWnd, kMovedFound, 0, 0);
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
        else if (wParam == kDragScrollTimer)
        {
            if (m_fileDragActive && m_sidebar.DragScroll(m_fileDragPoint))
                m_sidebar.SetDropTag(m_sidebar.TagNameAt(m_fileDragPoint));
            else
                KillTimer(m_hWnd, kDragScrollTimer);
        }
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
    if (!OpenSheet() && !m_contextMenu.IsOpen())
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
    // Under a sheet, nothing lights up.
    if (Sheet *sheet = OpenSheet())
    {
        sheet->OnHover(pt);
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
    SidebarView view = m_sidebar.View();
    m_titleBar.SetSidebarMark(!m_sidebarOpen && view != SidebarView::kAll, view == SidebarView::kMissing);
    m_titleBar.ShowRowButtons(!m_viewer.IsOpen() && !m_settings.IsOpen());
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
        m_tagSheet.Paint(m_renderer);
        m_folderTags.Paint(m_renderer);
        m_confirm.Paint(m_renderer);
        m_tooltip.Paint(m_renderer);
        if (m_dragActive || m_fileDragActive)
        {
            // The tag being dragged, or the number of files, as a chip at the pointer.
            std::wstring label = m_dragActive ? Utf8ToWide(m_dragTag) : FileCount(m_fileGrid.SelectedFiles().size());
            POINT at = m_dragActive ? m_dragPoint : m_fileDragPoint;
            // Whole pixels, so the label's own box isn't a fraction too narrow for it, which would cut it short.
            float width = std::ceil(m_renderer.TextWidth(label, Font::kCaption)) + 2.0f * Scale(8);
            float height = static_cast<float>(Scale(22));
            float x = static_cast<float>(at.x + Scale(12)), y = static_cast<float>(at.y + Scale(12));
            D2D1_RECT_F chip = { x, y, x + width, y + height };
            const float radius = static_cast<float>(Scale(theme::kChipRadius));
            m_renderer.FillRoundedRect(chip, radius, theme::kChip);
            m_renderer.DrawRoundedRect(chip, radius, theme::kYuri, 1.0f);
            RECT text = { static_cast<LONG>(x) + Scale(8), static_cast<LONG>(y), static_cast<LONG>(x + width) - Scale(8), static_cast<LONG>(y + height) };
            m_renderer.DrawTextLine(label, Font::kCaption, text, theme::kText1);
        }

        // The caret is drawn by Windows on top of the window; hide it while the frame is shown over it.
        Sheet *sheet = OpenSheet();
        bool caret = (sheet && sheet->IsFocused()) || m_searchBar.IsFocused() || m_sidebar.IsFocused();
        if (caret)
            HideCaret(m_hWnd);
        m_renderer.EndDraw();
        if (caret)
            ShowCaret(m_hWnd);
    }

    EndPaint(m_hWnd, &ps);
}
