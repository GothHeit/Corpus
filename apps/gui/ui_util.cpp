#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "ui_util.hpp"
#include <dwmapi.h>

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

int ScaleForWindow(HWND hWnd, int value)
{
    UINT dpi = hWnd ? GetDpiForWindow(hWnd) : 0;
    if (dpi == 0)
        dpi = 96;
    return MulDiv(value, dpi, 96);
}

void SetRoundedCorners(HWND hWnd, bool rounded)
{
    DWM_WINDOW_CORNER_PREFERENCE preference = rounded ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

HFONT CreateUIFont(int heightPx, int weight)
{
    return CreateFont(
        -heightPx, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

Gdiplus::Color ToGdiplusColor(COLORREF color)
{
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

void AddRoundedRect(Gdiplus::GraphicsPath &path, const RECT &rect, int radius)
{
    int d = radius * 2;
    int left = rect.left;
    int top = rect.top;
    int right = rect.right - 1;
    int bottom = rect.bottom - 1;

    path.AddArc(left, top, d, d, 180.0f, 90.0f);
    path.AddArc(right - d, top, d, d, 270.0f, 90.0f);
    path.AddArc(right - d, bottom - d, d, d, 0.0f, 90.0f);
    path.AddArc(left, bottom - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size);
    return out;
}

std::string WideToUtf8(const std::wstring &s)
{
    if (s.empty())
        return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size, nullptr, nullptr);
    return out;
}
