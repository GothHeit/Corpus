#ifndef SEARCH_BAR_HPP
#define SEARCH_BAR_HPP

#include <windows.h>
#include <string>

// Single-line text field for the search query. Uses the system caret for the blinking cursor.
class SearchBar
{
public:
    void SetBounds(HWND hWnd, const RECT &bounds);
    void Paint(HDC dc) const;

    // Takes focus on a click inside, drops it on a click anywhere else.
    // Returns true when the click landed on the search bar.
    bool OnLButtonDown(POINT pt);
    // Both return true when the query text changed.
    bool OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM key, bool ctrl);

    const std::wstring &Query() const { return m_query; }
    void SetValid(bool valid);
    bool IsFocused() const { return m_focused; }

private:
    int Scale(int value) const;
    void Invalidate() const;
    HFONT CreateSearchFont() const;
    int FontHeight(HDC dc) const;
    size_t CharIndexFromX(HDC dc, int targetX) const;
    size_t PrevWordBoundary(size_t pos) const;
    size_t NextWordBoundary(size_t pos) const;
    void UpdateCaretPos() const;

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};

    std::wstring m_query;
    size_t m_cursorPos = 0;
    bool m_focused = false;
    bool m_valid = true;
};

#endif
