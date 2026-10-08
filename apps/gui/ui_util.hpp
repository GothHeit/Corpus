#ifndef UI_UTIL_HPP
#define UI_UTIL_HPP

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>

// Converts a size in 96-DPI pixels to the DPI of the monitor the window is on.
int ScaleForWindow(HWND hWnd, int value);

// Windows 11 rounds the corners of our frameless window through DWM; maximized windows shouldn't be rounded.
void SetRoundedCorners(HWND hWnd, bool rounded);

HFONT CreateUIFont(int heightPx, int weight = FW_NORMAL);

Gdiplus::Color ToGdiplusColor(COLORREF color);
void AddRoundedRect(Gdiplus::GraphicsPath &path, const RECT &rect, int radius);

std::wstring Utf8ToWide(const std::string &s);
std::string WideToUtf8(const std::wstring &s);

#endif
