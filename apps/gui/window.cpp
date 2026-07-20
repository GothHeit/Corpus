#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif


#include "window.hpp"
#include "../../include/library.hpp"
#include "../../include/file.hpp"
#include "resource.h"
#include "ui_font.hpp"
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <cctype>
#include <cstdlib>
#include <vector>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

enum { BTN_CLOSE = 0, BTN_MIN = 1, BTN_MAX = 2 };

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
enum DWM_WINDOW_CORNER_PREFERENCE
{
    DWMWCP_DEFAULT = 0,
    DWMWCP_DONOTROUND = 1,
    DWMWCP_ROUND = 2,
    DWMWCP_ROUNDSMALL = 3
};
#endif


static std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size);
    return out;
}

static std::string WideToUtf8(const std::wstring &s)
{
    if (s.empty())
        return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size, nullptr, nullptr);
    return out;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE)
    {
        const CREATESTRUCT *create = reinterpret_cast<CREATESTRUCT*>(lParam);
        Window *window = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }

    Window *window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (window)
        return window->HandleMessage(hWnd, uMsg, wParam, lParam);

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
};

Window::Window(const library &lib)
    : m_hInstance(GetModuleHandle(nullptr)), m_lib(lib)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

    const wchar_t* CLASS_NAME = L"Kafkas Window Class";

    WNDCLASS wndClass = {};
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpszClassName = CLASS_NAME;
    wndClass.hInstance = m_hInstance;
    wndClass.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.lpfnWndProc = WindowProc;

    HBRUSH grey59Brush = CreateSolidBrush(RGB(28, 28, 28));
    wndClass.hbrBackground = grey59Brush;

    RegisterClass(&wndClass);

    DWORD style = WS_POPUP;

    int width = 640;
    int height = 480;
    int x = 250;
    int y = 250;

    RECT rect;
    rect.left = 250;
    rect.top = 250;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;

    AdjustWindowRect(&rect, style, false);

    m_hWnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Corpus",
        style,
        x,
        y,
        width,
        height,
        NULL,
        NULL,
        m_hInstance,
        this
    );
    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;

    DwmSetWindowAttribute(
        m_hWnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference,
        sizeof(preference)
    );

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    SetFocus(m_hWnd);
}

Window::~Window()
{
    const wchar_t* CLASS_NAME = L"Kafkas Window Class";

    UnregisterClass(CLASS_NAME, m_hInstance);

    Gdiplus::GdiplusShutdown(m_gdiplusToken);
}

bool Window::ProcessMessages()
{
    MSG msg = {};

    while(PeekMessage(&msg, nullptr, 0u, 0u, PM_REMOVE))
    {
        if(msg.message == WM_QUIT)
        {
            return false;
        }
        

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}

LRESULT Window::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CHAR:
    {
        if (!m_searchBar.IsFocused())
            return 0;

        RECT client;
        GetClientRect(hWnd, &client);

        if (m_searchBar.OnChar(hWnd, static_cast<wchar_t>(wParam), client, SearchBarTop()))
            InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (!m_searchBar.IsFocused())
            break;

        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        RECT client;
        GetClientRect(hWnd, &client);

        if (m_searchBar.OnKeyDown(hWnd, wParam, ctrl, client, SearchBarTop()))
            InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = Scale(320);
        mmi->ptMinTrackSize.y = Scale(240);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };

        if (HandleTitleBarButtonClick(hWnd, pt))
            return 0;

        RECT client;
        GetClientRect(hWnd, &client);
        int searchTop = SearchBarTop();

        bool wasFocused = m_searchBar.IsFocused();
        if (m_searchBar.OnLButtonDown(hWnd, client, searchTop, pt))
        {
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (wasFocused)
            InvalidateRect(hWnd, nullptr, FALSE);

        if (pt.y < Scale(kTitleBarHeight))
            BeginTitleBarDrag(hWnd, pt);

        return 0;
    }
    case WM_LBUTTONUP:
        if (m_awaitingDragThreshold)
        {
            m_awaitingDragThreshold = false;
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEMOVE:
        if (m_awaitingDragThreshold)
        {
            HandleMaximizedDragMove(hWnd);
            return 0;
        }
        break;
    case WM_NCHITTEST:
    {
        if (m_maximized)
            return HTCLIENT;

        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

        RECT wr;
        GetWindowRect(hWnd, &wr);

        const int margin = Scale(6);

        bool left   = pt.x < wr.left + margin;
        bool right  = pt.x >= wr.right - margin;
        bool top    = pt.y < wr.top + margin;
        bool bottom = pt.y >= wr.bottom - margin;

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
    case WM_PAINT:
        OnPaint(hWnd);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int Window::Scale(int value) const
{
    return MulDiv(value, GetDpiForWindow(m_hWnd), 96);
}

int Window::SearchBarTop() const
{
    return Scale(kTitleBarHeight) + Scale(kSearchBarGap);
}

bool Window::HandleTitleBarButtonClick(HWND hWnd, POINT pt)
{
    RECT closeRect = GetButtonRect(BTN_CLOSE);
    RECT minRect    = GetButtonRect(BTN_MIN);
    RECT maxRect    = GetButtonRect(BTN_MAX);

    if (PtInRect(&closeRect, pt))
    {
        DestroyWindow(hWnd);
        return true;
    }
    if (PtInRect(&minRect, pt))
    {
        ShowWindow(hWnd, SW_MINIMIZE);
        return true;
    }
    if (PtInRect(&maxRect, pt))
    {
        ToggleMaximize(hWnd);
        return true;
    }
    return false;
}

void Window::BeginTitleBarDrag(HWND hWnd, POINT pt)
{
    if (m_maximized)
    {
        m_dragAnchorScreen = pt;
        ClientToScreen(hWnd, &m_dragAnchorScreen);
        m_dragAnchorClientY = pt.y;
        m_awaitingDragThreshold = true;
        SetCapture(hWnd);
    }
    else
    {
        ReleaseCapture();
        SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
}

void Window::HandleMaximizedDragMove(HWND hWnd)
{
    POINT screenPt;
    GetCursorPos(&screenPt);

    int dx = screenPt.x - m_dragAnchorScreen.x;
    int dy = screenPt.y - m_dragAnchorScreen.y;

    if (std::abs(dx) <= GetSystemMetrics(SM_CXDRAG) && std::abs(dy) <= GetSystemMetrics(SM_CYDRAG))
        return;

    m_awaitingDragThreshold = false;
    ReleaseCapture();

    RECT beforeRect;
    GetWindowRect(hWnd, &beforeRect);
    int beforeWidth = beforeRect.right - beforeRect.left;
    float ratioX = static_cast<float>(m_dragAnchorScreen.x - beforeRect.left) / beforeWidth;

    ToggleMaximize(hWnd);

    RECT afterRect;
    GetWindowRect(hWnd, &afterRect);
    int afterWidth = afterRect.right - afterRect.left;
    int afterHeight = afterRect.bottom - afterRect.top;

    int newLeft = static_cast<int>(m_dragAnchorScreen.x - ratioX * afterWidth);
    int newTop = m_dragAnchorScreen.y - m_dragAnchorClientY;

    SetWindowPos(hWnd, nullptr, newLeft, newTop, afterWidth, afterHeight, SWP_NOZORDER);

    SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

RECT Window::GetButtonRect(int index) const
{
    const int radius = Scale(6);
    const int spacing = Scale(20);
    const int startX = Scale(12);
    const int centerY = Scale(kTitleBarHeight) / 2;

    int centerX = startX + index * spacing;

    RECT r;
    r.left   = centerX - radius;
    r.right  = centerX + radius;
    r.top    = centerY - radius;
    r.bottom = centerY + radius;
    return r;
}

void Window::ToggleMaximize(HWND hWnd)
{
    if (!m_maximized)
    {
        GetWindowRect(hWnd, &m_restoreRect);   // save current position/size

        HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(mon, &mi);

        SetWindowPos(hWnd, nullptr,
            mi.rcWork.left, mi.rcWork.top,
            mi.rcWork.right - mi.rcWork.left,
            mi.rcWork.bottom - mi.rcWork.top - 1,
            SWP_NOZORDER);

        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }
    else
    {
        SetWindowPos(hWnd, nullptr,
            m_restoreRect.left, m_restoreRect.top,
            m_restoreRect.right - m_restoreRect.left,
            m_restoreRect.bottom - m_restoreRect.top,
            SWP_NOZORDER);

        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }

    m_maximized = !m_maximized;
}

static std::string FileExtension(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return std::string();

    std::string ext = path.substr(dot + 1);
    for (char &c : ext)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return ext;
}

static std::string FileName(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1);
}

void Window::DrawFileGrid(HDC dc, HWND hWnd, const RECT &client) const
{
    const int margin = Scale(20);
    const int cellWidth = Scale(96);
    const int cellHeight = Scale(96);
    const int iconSize = Scale(56);
    RECT searchRect = m_searchBar.GetRect(hWnd, client, SearchBarTop());
    const int gridTop = searchRect.bottom + Scale(kSearchBarGap);

    int usableWidth = client.right - 2 * margin;
    int columns = usableWidth / cellWidth;
    if (columns < 1)
        columns = 1;

    int gridLeft = margin + (usableWidth - columns * cellWidth) / 2;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(230, 230, 230));

    HFONT extFont = CreateUIFont(Scale(16), FW_BOLD);
    HFONT nameFont = CreateUIFont(Scale(12));

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush iconBrush(Gdiplus::Color(255, 60, 60, 66));

    int index = 0;
    for (const file *f : m_lastGoodResults)
    {
        int col = index % columns;
        int row = index / columns;

        int cellX = gridLeft + col * cellWidth;
        int cellY = gridTop + row * cellHeight;
        int iconX = cellX + (cellWidth - iconSize) / 2;

        graphics.FillRectangle(&iconBrush, iconX, cellY, iconSize, iconSize);

        SelectObject(dc, extFont);
        std::wstring ext = Utf8ToWide(FileExtension(f->get_path()));
        RECT extRect = { iconX, cellY, iconX + iconSize, cellY + iconSize };
        DrawTextW(dc, ext.c_str(), -1, &extRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(dc, nameFont);
        std::wstring name = Utf8ToWide(FileName(f->get_path()));
        RECT nameRect = { cellX, cellY + iconSize + Scale(4), cellX + cellWidth, cellY + cellHeight };
        DrawTextW(dc, name.c_str(), -1, &nameRect, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        index++;
    }

    DeleteObject(extFont);
    DeleteObject(nameFont);
}

void Window::DrawCircle(Gdiplus::Graphics &graphics, RECT rect, COLORREF color) const
{
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillEllipse(&brush,
        static_cast<int>(rect.left), static_cast<int>(rect.top),
        static_cast<int>(rect.right - rect.left), static_cast<int>(rect.bottom - rect.top));
}

void Window::DrawTitleBarButtons(HDC dc) const
{
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    DrawCircle(graphics, GetButtonRect(BTN_CLOSE), RGB(255, 95, 86));
    DrawCircle(graphics, GetButtonRect(BTN_MIN), RGB(255, 189, 46));
    DrawCircle(graphics, GetButtonRect(BTN_MAX), RGB(39, 201, 63));
}

void Window::OnPaint(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT client;
    GetClientRect(hWnd, &client);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    HBRUSH background = CreateSolidBrush(RGB(28, 28, 28));
    FillRect(memDC, &client, background);
    DeleteObject(background);

    SearchResult result = m_lib.search(WideToUtf8(m_searchBar.Query()));
    m_searchBar.SetValid(result.valid);
    if (result.valid)
        m_lastGoodResults = result.files;

    DrawFileGrid(memDC, hWnd, client);
    m_searchBar.Draw(memDC, hWnd, client, SearchBarTop());
    DrawTitleBarButtons(memDC);

    if (m_searchBar.IsFocused())
        HideCaret(hWnd);

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDC, 0, 0, SRCCOPY);

    if (m_searchBar.IsFocused())
        ShowCaret(hWnd);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hWnd, &ps);
}