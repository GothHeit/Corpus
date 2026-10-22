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
#include "resource.h"
#include "theme.hpp"
#include "ui_util.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <string>
#include <unordered_set>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

static const wchar_t *kClassName = L"Kafkas Window Class";

// Opening more files than this at once asks first, like Explorer does.
static const size_t kOpenPromptThreshold = 15;

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

Window::Window(library &lib)
    : m_hInstance(GetModuleHandle(nullptr)), m_lib(lib), m_searchBar(L"search by name or tag..."), m_tagPanel(lib)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

    WNDCLASS wndClass = {};
    // CS_DBLCLKS: Windows only sends WM_LBUTTONDBLCLK to windows that ask for it.
    wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wndClass.lpszClassName = kClassName;
    wndClass.hInstance = m_hInstance;
    wndClass.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = CreateSolidBrush(theme::kBackground);
    wndClass.lpfnWndProc = WindowProc;

    RegisterClass(&wndClass);

    // WS_POPUP: no native frame or caption; TitleBar draws and handles our own.
    // WS_EX_ACCEPTFILES: files dragged in from Explorer arrive as WM_DROPFILES.
    m_hWnd = CreateWindowEx(
        WS_EX_ACCEPTFILES,
        kClassName,
        L"Corpus",
        WS_POPUP,
        250, 250,
        640, 480,
        NULL,
        NULL,
        m_hInstance,
        this
    );

    SetRoundedCorners(m_hWnd, true);

    LayoutComponents();
    RefreshResults();

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    SetFocus(m_hWnd);
}

Window::~Window()
{
    UnregisterClass(kClassName, m_hInstance);

    Gdiplus::GdiplusShutdown(m_gdiplusToken);
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

// Splits the client area: title bar on top, then the search bar, the tag panel at the bottom and the grid
// filling the rest.
void Window::LayoutComponents()
{
    RECT client;
    GetClientRect(m_hWnd, &client);

    const int margin = Scale(theme::kPageMargin);
    const int gap = Scale(theme::kSectionGap);

    RECT titleBar = { 0, 0, client.right, Scale(theme::kTitleBarHeight) };

    int searchTop = titleBar.bottom + gap;
    RECT searchBar = { margin, searchTop, client.right - margin, searchTop + Scale(theme::kSearchBarHeight) };

    int tagPanelBottom = client.bottom - gap;
    RECT tagPanel = { margin, tagPanelBottom - Scale(theme::kTagPanelHeight), client.right - margin, tagPanelBottom };

    RECT fileGrid = { 0, searchBar.bottom + gap, client.right, tagPanel.top - gap };

    m_titleBar.SetBounds(m_hWnd, titleBar);
    m_searchBar.SetBounds(m_hWnd, searchBar);
    m_fileGrid.SetBounds(m_hWnd, fileGrid);
    m_tagPanel.SetBounds(m_hWnd, tagPanel);
}

// Runs the query typed in the search bar and shows the result in the grid.
// An invalid query marks the search bar and keeps the last valid result on screen.
void Window::RefreshResults()
{
    SearchResult result = m_lib.search(WideToUtf8(m_searchBar.Query()));
    m_searchBar.SetValid(result.valid);
    if (result.valid)
        m_fileGrid.SetFiles(result.files);
    // The selection may have lost the files that left the view, and the tags of the files may have changed.
    SelectionChanged();
}

void Window::SelectionChanged()
{
    m_tagPanel.SetSelection(m_fileGrid.SelectedFiles());
}

// Opens each file with the program Windows associates with it (an .html in the browser, a .pdf in
// the PDF reader, ...). Windows itself reports missing files and asks which program to use when
// there is none.
void Window::OpenFiles(const std::vector<file*> &files)
{
    if (files.size() > kOpenPromptThreshold)
    {
        std::wstring prompt = L"Open " + std::to_wstring(files.size()) + L" files?";
        if (MessageBoxW(m_hWnd, prompt.c_str(), L"Corpus", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
    }

    for (const file *f : files)
    {
        std::wstring path = Utf8ToWide(f->get_path());

        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.hwnd = m_hWnd;     // owner of any dialog Windows shows
        info.lpFile = path.c_str();
        info.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&info);
    }
}

// Adds the files and folders dragged in from Explorer to the library. Only their paths are stored:
// nothing is copied or moved.
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

    // One transaction for the whole drop: a single wait for the disk instead of one per file.
    if (!BeginChange())
        return;
    for (const std::string &path : paths)
        m_lib.add_file(path);
    FinishChange();
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

    std::wstring prompt = (files.size() == 1
        ? L"Remove " + Utf8ToWide(files[0]->get_path())
        : L"Remove " + std::to_wstring(files.size()) + L" files") +
        L" from the library?\n\nThe files themselves stay where they are.";
    if (MessageBoxW(m_hWnd, prompt.c_str(), L"Corpus", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
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
    RefreshResults();
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

    std::wstring message = L"Could not save to the library:\n" + Utf8ToWide(db->last_error());
    db->clear_error();

    // The grid points into the library; it lets go before the library deletes its files.
    m_fileGrid.Clear();
    m_lib.clear();
    if (!open_library(m_lib, *db, ""))
    {
        // Nothing on screen can be trusted, and new changes would no longer be saved.
        message += L"\n\nThe library could not be reloaded either:\n" + Utf8ToWide(db->last_error()) +
            L"\n\nCorpus will close. Open it again to see what was saved.";
        MessageBoxW(m_hWnd, message.c_str(), L"Corpus", MB_OK | MB_ICONERROR);
        DestroyWindow(m_hWnd);
        return;
    }

    RefreshResults();
    message += L"\n\nThe last change was not saved. The library was reloaded to show what is saved.";
    MessageBoxW(m_hWnd, message.c_str(), L"Corpus", MB_OK | MB_ICONERROR);
}

LRESULT Window::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        DestroyWindow(m_hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
    case WM_DPICHANGED:
        LayoutComponents();
        break;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = Scale(theme::kMinWindowWidth);
        mmi->ptMinTrackSize.y = Scale(theme::kMinWindowHeight);
        return 0;
    }

    case WM_NCHITTEST:
        return HitTestResizeBorder(lParam);

    case WM_ERASEBKGND:
        return 1;  // OnPaint fills the whole background itself

    case WM_PAINT:
        OnPaint();
        return 0;

    // ---- Keyboard: the focused text field gets it, the grid otherwise ----

    case WM_CHAR:
        if (m_searchBar.IsFocused() && m_searchBar.OnChar(static_cast<wchar_t>(wParam)))
            RefreshResults();
        else if (m_tagPanel.IsFocused())
            m_tagPanel.OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_KEYDOWN:
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // Ctrl+T: type tags for the selected files, from anywhere.
        if (ctrl && wParam == 'T')
        {
            m_searchBar.Blur();
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

        if (wParam == VK_RETURN)
        {
            OpenFiles(m_fileGrid.SelectedFiles());
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

        // Every click goes to the text fields first so they can drop focus when clicked outside. The tag field
        // goes first: there is only one caret, and the search bar may be about to take it.
        if (!m_tagPanel.Contains(pt))
            m_tagPanel.Blur();
        if (m_searchBar.OnLButtonDown(pt))
            return 0;
        TagEdit edit;
        if (m_tagPanel.OnLButtonDown(pt, edit))
        {
            // A chip changes under the cursor once clicked (it goes away, or moves to the tags every file has),
            // so the second click of a double-click would hit another one.
            if (uMsg == WM_LBUTTONDOWN)
                ApplyTagEdit(edit);
            return 0;
        }
        if (m_titleBar.OnLButtonDown(pt))
            return 0;
        if (m_fileGrid.OnLButtonDown(pt, (wParam & MK_CONTROL) != 0, (wParam & MK_SHIFT) != 0))
            SelectionChanged();

        if (uMsg == WM_LBUTTONDBLCLK)
        {
            if (file *f = m_fileGrid.FileAt(pt))
                OpenFiles({ f });
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (m_titleBar.OnMouseMove() || m_fileGrid.OnMouseMove(pt))
            return 0;
        break;
    }

    case WM_LBUTTONUP:
        m_titleBar.OnLButtonUp();
        m_fileGrid.OnLButtonUp();
        return 0;

    case WM_CAPTURECHANGED:
        m_fileGrid.OnCaptureChanged();
        break;

    case WM_MOUSEWHEEL:
    {
        // Unlike the other mouse messages, the wheel comes with screen coordinates.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hWnd, &pt);
        if (!m_tagPanel.OnMouseWheel(pt, GET_WHEEL_DELTA_WPARAM(wParam)))
            m_fileGrid.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }

    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;
    }

    return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
}

// The window has no native frame, so we tell Windows where the resize edges are.
LRESULT Window::HitTestResizeBorder(LPARAM lParam) const
{
    if (m_titleBar.Maximized())
        return HTCLIENT;

    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

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

// Draws everything into an off-screen bitmap and copies it to the window in one go, so there's no flicker.
void Window::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hWnd, &ps);

    RECT client;
    GetClientRect(m_hWnd, &client);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    HBRUSH background = CreateSolidBrush(theme::kBackground);
    FillRect(memDC, &client, background);
    DeleteObject(background);

    m_fileGrid.Paint(memDC);
    m_searchBar.Paint(memDC);
    m_tagPanel.Paint(memDC);
    m_titleBar.Paint(memDC);

    // The caret is drawn by Windows on top of the window; hide it while we copy over it.
    bool caret = m_searchBar.IsFocused() || m_tagPanel.IsFocused();
    if (caret)
        HideCaret(m_hWnd);

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDC, 0, 0, SRCCOPY);

    if (caret)
        ShowCaret(m_hWnd);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(m_hWnd, &ps);
}
