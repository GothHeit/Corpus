#ifndef SEARCH_BAR_HPP
#define SEARCH_BAR_HPP

#include <windows.h>
#include <string>

class SearchBar
{
public:
    RECT GetRect(HWND hWnd, const RECT &client, int top) const;
    void Draw(HDC dc, HWND hWnd, const RECT &client, int top) const;

    bool OnLButtonDown(HWND hWnd, const RECT &client, int top, POINT pt);
    bool OnChar(HWND hWnd, wchar_t ch, const RECT &client, int top);
    bool OnKeyDown(HWND hWnd, WPARAM key, bool ctrl, const RECT &client, int top);

    const std::wstring &Query() const { return m_query; }
    void SetValid(bool valid) { m_valid = valid; }
    bool IsFocused() const { return m_focused; }

private:
    static const int kHeight = 28;
    static const int kTextPadding = 10;

    int Scale(HWND hWnd, int value) const;
    HFONT CreateSearchFont(HWND hWnd) const;
    int FontHeight(HWND hWnd, HDC dc) const;
    size_t CharIndexFromX(HWND hWnd, HDC dc, int targetX) const;
    size_t PrevWordBoundary(size_t pos) const;
    size_t NextWordBoundary(size_t pos) const;
    void UpdateCaretPos(HWND hWnd, const RECT &client, int top) const;

    std::wstring m_query;
    size_t m_cursorPos = 0;
    bool m_focused = false;
    bool m_valid = true;
};

#endif
