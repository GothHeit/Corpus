#ifndef TEXT_FIELD_HPP
#define TEXT_FIELD_HPP

#include <windows.h>
#include <string>

class Renderer;

// Single-line text field (the search bar and the tag field). Uses the system caret for the blinking cursor.
// Text longer than the field scrolls sideways to keep the cursor in view.
class TextField
{
public:
    explicit TextField(const wchar_t *placeholder) : m_placeholder(placeholder) {}
    void SetPlaceholder(const wchar_t *placeholder);
    // Draws a magnifying glass before the text.
    void SetSearchIcon(bool icon) { m_searchIcon = icon; }

    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    // The text got a new size: makes the caret as tall as a line again.
    void OnDpiChanged();
    void Paint(Renderer &renderer) const;
    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }

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
    // Marks the text as invalid with a message under the field. An empty message marks it valid again.
    void SetError(const std::wstring &message);
    bool IsFocused() const { return m_focused; }

private:
    int Scale(int value) const;
    void Invalidate() const;
    int TextLeft() const;
    RECT ErrorRect() const;
    int CaretHeight() const;
    void CreateSystemCaret();
    float TextX(size_t pos) const;
    size_t CharIndexFromX(float x) const;
    size_t PrevWordBoundary(size_t pos) const;
    size_t NextWordBoundary(size_t pos) const;
    void UpdateCaretPos();

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_searchIcon = false;

    const wchar_t *m_placeholder;
    std::wstring m_query;
    std::wstring m_suggestion;
    size_t m_cursorPos = 0;
    float m_scrollX = 0.0f;  // pixels of text hidden off the left edge
    bool m_focused = false;
    std::wstring m_error;
};

#endif
