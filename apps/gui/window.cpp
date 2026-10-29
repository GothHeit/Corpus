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
#include "resource.h"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "open_with.hpp"
#include "single_instance.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#pragma comment(lib, "dwmapi.lib")

static const wchar_t *kClassName = L"Kafkas Window Class";

// Where the sidebar being open or hidden is remembered between runs, under HKEY_CURRENT_USER: nothing is written
// next to the .exe.
static const wchar_t *kSettingsKey = L"Software\\Corpus";
static const wchar_t *kSidebarHiddenValue = L"SidebarHidden";

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
void SaveTileSize(int size)
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
void SaveSort(int key, bool descending)
{
    DWORD value = static_cast<DWORD>(key);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kSortByValue, REG_DWORD, &value, sizeof(value));
    value = descending ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kSortDescendingValue, REG_DWORD, &value, sizeof(value));
}

std::wstring FileCount(size_t count)
{
    return count == 1 ? strings::kOneFile : FormatCount(count) + L" " + strings::kFiles;
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

    // WS_OVERLAPPEDWINDOW: a sizable frame with a caption, so Windows snaps, maximizes and restores the window
    // natively (Win+arrows, dragging to the screen's edges). WM_NCCALCSIZE hides that frame and TitleBar draws
    // our own.
    // WS_CLIPCHILDREN: painting leaves the video's window alone.
    // WS_EX_ACCEPTFILES: files dragged in from Explorer arrive as WM_DROPFILES.
    m_hWnd = CreateWindowEx(
        WS_EX_ACCEPTFILES,
        kClassName,
        strings::kAppName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
    {
        std::wstring folder = thumbnailFolder;
        size_t slash = folder.find_last_of(L"\\/");
        m_libraryFolder = WideToUtf8(slash == std::wstring::npos ? folder : folder.substr(0, slash));
    }
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
    StartWatching();
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

void Window::HandleTitleAction(TitleBar::Action action)
{
    if (action == TitleBar::Action::kToggleSidebar)
        ToggleSidebar();
    else if (action == TitleBar::Action::kSort)
        OpenSortMenu();
    else if (action == TitleBar::Action::kAddFolder)
        ChooseFolders();
}

POINT Window::CursorInWindow() const
{
    POINT pt;
    if (!GetCursorPos(&pt) || !ScreenToClient(m_hWnd, &pt))
        return kNoPointer;
    return pt;
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
        m_folderWatch.Stop();
        m_fileGrid.StopThumbnails();
        m_viewer.Stop();
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        m_contextMenu.Close();
        m_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
        LayoutComponents();
        if (wParam != SIZE_MINIMIZED && !m_monitorFullscreen)
        {
            m_titleBar.FitEdgeStrips();
            SetRoundedCorners(m_hWnd, wParam != SIZE_MAXIMIZED);
        }
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
        if (active)
            m_folderWatch.Rescan();
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

    case kWatchFound:
        if (!IsWindowEnabled(m_hWnd))
            m_watchPending = true;
        else
            OnWatchFound();
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
        if (OnSetCursor(lParam))
            return TRUE;
        break;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = Scale(theme::kMinWindowWidth);
        mmi->ptMinTrackSize.y = Scale(theme::kMinWindowHeight);
        return 0;
    }

    // The whole window is client area. Windows makes a maximized window larger than its monitor by the frame it
    // would have, so there the client keeps to the area TitleBar::MaximizedArea gives.
    case WM_NCCALCSIZE:
        if (wParam && IsZoomed(m_hWnd) && !m_monitorFullscreen)
        {
            RECT &r = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0];
            RECT strips[4];
            RECT area = TitleBar::MaximizedArea(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), strips);
            IntersectRect(&r, &r, &area);
        }
        return 0;

    // Without a visible frame there is nothing to repaint; -1 keeps Windows from drawing its own caption.
    case WM_NCACTIVATE:
        return DefWindowProc(m_hWnd, uMsg, wParam, -1);

    // A tap of Alt alone would enter the menu of the hidden caption and eat the next key. Alt+Space still opens it.
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == 0)
            return 0;
        // The viewer's fullscreen covers the monitor until it ends; the window behind it keeps its state.
        if (m_monitorFullscreen && ((wParam & 0xFFF0) == SC_MAXIMIZE || (wParam & 0xFFF0) == SC_RESTORE))
            return 0;
        break;

    // The taskbar moved, appeared or started hiding itself, or a screen changed: a maximized window follows.
    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA)
            FitToMonitor();
        break;
    case WM_DISPLAYCHANGE:
        FitToMonitor();
        break;

    case WM_NCHITTEST:
        return HitTest(lParam);

    case WM_ERASEBKGND:
        return 1;  // OnPaint fills the whole background itself

    case WM_PAINT:
        OnPaint();
        return 0;

    // ---- Keyboard: the focused text field gets it, the grid otherwise ----

    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_KEYDOWN:
        if (OnKeyDown(wParam, lParam))
            return 0;
        break;

    // ---- Mouse ----

    // The second press of a double-click arrives as WM_LBUTTONDBLCLK instead of WM_LBUTTONDOWN.
    // It's still a press for every component; on a grid item it also opens the file.
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        OnLButtonDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, wParam, uMsg == WM_LBUTTONDBLCLK);
        return 0;

    // The menu opens on the press, as on macOS.
    case WM_RBUTTONDOWN:
        OnRButtonDown(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_MOUSEMOVE:
        if (OnMouseMove(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
            return 0;
        break;

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        OnHover(HoverAfterLeave());
        return 0;

    // Over the maximize button, which Windows takes for a caption button, the pointer and the clicks arrive as
    // nonclient messages. They go where the client ones do; Windows' own handling would draw a native button.
    case WM_NCMOUSEMOVE:
        if (wParam != HTMAXBUTTON)
            break;
        if (!m_trackingMaximize)
        {
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE | TME_NONCLIENT, m_hWnd, 0 };
            m_trackingMaximize = TrackMouseEvent(&track) != FALSE;
        }
        OnHover(CursorInWindow());
        return 0;

    case WM_NCMOUSELEAVE:
        m_trackingMaximize = false;
        OnHover(HoverAfterLeave());
        return 0;

    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (wParam != HTMAXBUTTON)
            break;
        OnLButtonDown(CursorInWindow(), MK_LBUTTON, uMsg == WM_NCLBUTTONDBLCLK);
        return 0;

    case WM_NCLBUTTONUP:
        if (wParam != HTMAXBUTTON)
            break;
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_CAPTURECHANGED:
        OnCaptureChanged();
        break;

    case WM_MOUSEWHEEL:
        OnMouseWheel(POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

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
        if (wParam && m_watchPending)
        {
            m_watchPending = false;
            PostMessage(m_hWnd, kWatchFound, 0, 0);
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
        OnTimer(wParam);
        return 0;
    }

    return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
}

// The pointer's shape over the window, where it isn't the arrow: none over a video playing. False leaves it to
// Windows.
bool Window::OnSetCursor(LPARAM lParam)
{
    // An I-beam over the text fields, like any text box.
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(m_hWnd, &pt);
    if (LOWORD(lParam) == HTCLIENT && m_viewer.Contains(pt))
    {
        SetCursor(m_viewer.CursorHidden() && !OpenSheet() ? nullptr : LoadCursor(nullptr, IDC_ARROW));
        return true;
    }
    if (LOWORD(lParam) == HTCLIENT && (m_searchBar.Contains(pt) || m_sidebar.OverField(pt)))
    {
        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
        return true;
    }
    return false;
}

void Window::OnTimer(UINT_PTR timer)
{
    if (timer == kImportTimer)
        ShowImportProgress();
    else if (timer == kResultTimer)
        HideImportBar();
    else if (timer == kGridHoverTimer || timer == kGridButtonTimer || timer == kThumbnailTimer)
        m_fileGrid.OnTimer(timer);
    else if (timer == kChipHoverTimer)
        m_tagPanel.OnTimer();
    else if (timer == kCancelHoverTimer)
        m_importBar.OnTimer();
    else if (timer == kSidebarHoverTimer || timer == kSidebarViewHoverTimer || timer == kSidebarSettingsHoverTimer)
        m_sidebar.OnTimer(timer);
    else if (timer == kSidebarSlideTimer)
        StepSidebarSlide();
    else if (timer == kTitleHoverTimer)
        m_titleBar.OnTimer(timer);
    else if (timer == kSettingsHoverTimer)
        m_settings.OnTimer();
    else if (timer == kMenuHoverTimer)
        m_contextMenu.OnTimer();
    else if (timer == kInfoHoverTimer)
        m_infoPane.OnTimer();
    else if (timer == kTooltipTimer)
        m_tooltip.OnTimer(m_hWnd);
    else if (timer == kDragScrollTimer)
    {
        if (m_fileDragActive && m_sidebar.DragScroll(m_fileDragPoint))
            m_sidebar.SetDropTag(m_sidebar.TagNameAt(m_fileDragPoint));
        else
            KillTimer(m_hWnd, kDragScrollTimer);
    }
    else if (timer == kViewerIdleTimer || timer == kViewerFadeTimer || timer == kViewerHoverTimer ||
             timer == kViewerFrameTimer || timer == kViewerLoadingTimer || timer == kViewerVideoTimer ||
             timer == kViewerProgressTimer || timer == kViewerReflowTimer || timer == kViewerToastTimer)
        m_viewer.OnTimer(timer);
}

// The window hides its native frame, so we tell Windows where the resize edges are, and where the maximize button is:
// hovering it then offers the snap layouts of Windows 11, as a native one does.
LRESULT Window::HitTest(LPARAM lParam)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    // Only while the button takes clicks: no sheet or menu above it, no fullscreen viewer over it.
    if (!m_monitorFullscreen && !OpenSheet() && !m_contextMenu.IsOpen())
    {
        POINT client = pt;
        ScreenToClient(m_hWnd, &client);
        if (m_titleBar.OverMaximizeButton(client))
            return HTMAXBUTTON;
    }

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

// The pointer left the client area or the maximize button. It may have gone from one to the other, and the message
// for leaving can come after the one for arriving, so the hover goes by where the pointer is now.
POINT Window::HoverAfterLeave()
{
    POINT pt;
    if (!GetCursorPos(&pt) || GetAncestor(WindowFromPoint(pt), GA_ROOT) != m_hWnd)
        return kNoPointer;
    LRESULT hit = HitTest(MAKELPARAM(pt.x, pt.y));
    return hit == HTCLIENT || hit == HTMAXBUTTON ? CursorInWindow() : kNoPointer;
}

// After the taskbar, the screen or its scale changed: a fullscreen window covers its monitor again, a maximized one
// its work area.
void Window::FitToMonitor()
{
    if (!m_monitorFullscreen)
    {
        // A maximized window asks for its client area again, which leaves a pixel along a taskbar that now hides.
        if (m_titleBar.Maximized())
            SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        m_titleBar.FitEdgeStrips();
        return;
    }
    MONITORINFO monitor = { sizeof(monitor) };
    if (!GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT &m = monitor.rcMonitor;
    SetWindowPos(m_hWnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
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
