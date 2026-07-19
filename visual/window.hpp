#ifndef WINDOWS_HPP
#define WINDOWS_HPP

#include <windows.h>
#include <string>
#include <vector>

class library;
class file;

namespace Gdiplus { class Graphics; }

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

class Window
{
public:
    explicit Window(const library &lib);
    ~Window();

    bool ProcessMessages();
private:

RECT GetButtonRect(int index) const;
void ToggleMaximize(HWND hWnd);
int Scale(int value) const;

RECT GetSearchBarRect(const RECT &client) const;
HFONT CreateSearchFont() const;
int SearchFontHeight(HDC dc) const;
size_t CharIndexFromX(HDC dc, int targetX) const;
size_t PrevWordBoundary(size_t pos) const;
size_t NextWordBoundary(size_t pos) const;
void UpdateCaretPos(HWND hWnd);

friend LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hWnd);
void DrawFileGrid(HDC dc, const RECT &client) const;
void DrawSearchBar(HDC dc, const RECT &client) const;
void DrawTitleBarButtons(HDC dc) const;
void DrawCircle(Gdiplus::Graphics &graphics, RECT rect, COLORREF color) const;

    const int kTitleBarHeight = 20;
    const int kSearchBarHeight = 28;
    const int kSearchBarGap = 10;
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    const library &m_lib;
    bool m_maximized = false;
    RECT m_restoreRect;
    ULONG_PTR m_gdiplusToken;
    bool m_awaitingDragThreshold = false;
    POINT m_dragAnchorScreen;
    int m_dragAnchorClientY;
    std::wstring m_searchQuery;
    std::vector<file*> m_lastGoodResults;
    bool m_searchValid = true;
    bool m_searchFocused = false;
    size_t m_cursorPos = 0;
};

#endif