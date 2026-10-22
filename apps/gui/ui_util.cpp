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

D2D1_RECT_F ToRectF(const RECT &rect)
{
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                       static_cast<float>(rect.right), static_cast<float>(rect.bottom));
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
