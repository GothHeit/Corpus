#ifndef UI_UTIL_HPP
#define UI_UTIL_HPP

#include <windows.h>
#include <d2d1.h>
#include <string>

// Converts a size in 96-DPI pixels to the DPI of the monitor the window is on.
int ScaleForWindow(HWND hWnd, int value);

// Windows 11 rounds the corners of our frameless window through DWM; maximized windows shouldn't be rounded.
void SetRoundedCorners(HWND hWnd, bool rounded);

// The same rectangle, for Direct2D.
D2D1_RECT_F ToRectF(const RECT &rect);

std::wstring Utf8ToWide(const std::string &s);
std::string WideToUtf8(const std::wstring &s);

// A count with the user's thousands separator: 1,240 (or 1.240, depending on the region set in Windows).
std::wstring FormatCount(size_t count);

#endif
