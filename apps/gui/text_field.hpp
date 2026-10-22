#ifndef TEXT_FIELD_HPP
#define TEXT_FIELD_HPP

#include <windows.h>
#include <string>

// Single-line text field (the search bar). Uses the system caret for the blinking cursor.
class TextField
{
public:
    explicit TextField(const wchar_t *placeholder) : m_placeholder(placeholder) {}
    void SetPlaceholder(const wchar_t *placeholder);

    void SetBounds(HWND hWnd, const RECT &bounds);
    void Paint(HDC dc) const;

    // Takes focus on a click inside, drops it on a click anywhere else.
    // Returns true when the click landed on the search bar.
    bool OnLButtonDown(POINT pt);
    // Both return true when the query text changed.
    bool OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM key, bool ctrl);

    // Takes focus with the cursor at the end, as if clicked after the text.
    void Focus();
    void Blur();

    const std::wstring &Query() const { return m_query; }
    // Replaces the text and puts the cursor at the end.
    void SetText(const std::wstring &text);
    // Grey text shown after the cursor while it is at the end, such as the rest of a word being completed.
    void SetSuggestion(const std::wstring &suggestion);
    bool CursorAtEnd() const { return m_cursorPos == m_query.size(); }
    void SetValid(bool valid);
    bool IsFocused() const { return m_focused; }

private:
    int Scale(int value) const;
    void Invalidate() const;
    HFONT CreateFieldFont() const;
    int FontHeight(HDC dc) const;
    size_t CharIndexFromX(HDC dc, int targetX) const;
    size_t PrevWordBoundary(size_t pos) const;
    size_t NextWordBoundary(size_t pos) const;
    void UpdateCaretPos() const;

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};

    const wchar_t *m_placeholder;
    std::wstring m_query;
    std::wstring m_suggestion;
    size_t m_cursorPos = 0;
    bool m_focused = false;
    bool m_valid = true;
};

#endif
