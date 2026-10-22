#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "text_field.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <cwctype>
#include <vector>

namespace
{
    const int kTextPadding = 10;
    const int kFontSize = 13;
}

int TextField::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void TextField::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

HFONT TextField::CreateFieldFont() const
{
    return CreateUIFont(Scale(kFontSize));
}

int TextField::FontHeight(HDC dc) const
{
    HFONT font = CreateFieldFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    return tm.tmHeight;
}

size_t TextField::CharIndexFromX(HDC dc, int targetX) const
{
    if (m_query.empty())
        return 0;

    HFONT font = CreateFieldFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    std::vector<int> extents(m_query.size());
    GetTextExtentExPointW(dc, m_query.c_str(), static_cast<int>(m_query.size()),
        INT_MAX, nullptr, extents.data(), nullptr);

    SelectObject(dc, oldFont);
    DeleteObject(font);

    size_t index = 0;
    int prevExtent = 0;
    for (; index < extents.size(); index++)
    {
        int charWidth = extents[index] - prevExtent;
        if (targetX < prevExtent + charWidth / 2)
            break;
        prevExtent = extents[index];
    }
    return index;
}

size_t TextField::PrevWordBoundary(size_t pos) const
{
    auto isWordChar = [](wchar_t c) { return std::iswalnum(c) != 0; };

    while (pos > 0 && !isWordChar(m_query[pos - 1]))
        pos--;
    while (pos > 0 && isWordChar(m_query[pos - 1]))
        pos--;
    return pos;
}

size_t TextField::NextWordBoundary(size_t pos) const
{
    auto isWordChar = [](wchar_t c) { return std::iswalnum(c) != 0; };
    size_t size = m_query.size();

    while (pos < size && !isWordChar(m_query[pos]))
        pos++;
    while (pos < size && isWordChar(m_query[pos]))
        pos++;
    return pos;
}

void TextField::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_bounds = bounds;
    UpdateCaretPos();
}

void TextField::SetPlaceholder(const wchar_t *placeholder)
{
    if (placeholder == m_placeholder)
        return;
    m_placeholder = placeholder;
    Invalidate();
}

void TextField::SetValid(bool valid)
{
    if (valid == m_valid)
        return;

    m_valid = valid;
    Invalidate();
}

// Scrolls the text so the cursor stays inside the field, then moves the caret to it.
void TextField::UpdateCaretPos()
{
    if (!m_hWnd)
        return;

    HDC dc = GetDC(m_hWnd);
    HFONT font = CreateFieldFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SIZE cursor, text;
    GetTextExtentPoint32W(dc, m_query.c_str(), static_cast<int>(m_cursorPos), &cursor);
    GetTextExtentPoint32W(dc, m_query.c_str(), static_cast<int>(m_query.size()), &text);

    int fontHeight = FontHeight(dc);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(m_hWnd, dc);

    int visible = (m_bounds.right - m_bounds.left) - 2 * Scale(kTextPadding);
    if (visible < 0)
        visible = 0;

    int scroll = m_scrollX;
    if (cursor.cx - scroll > visible)
        scroll = cursor.cx - visible;
    if (cursor.cx < scroll)
        scroll = cursor.cx;
    // No empty space after the end of the text while part of it is hidden on the left.
    if (scroll > text.cx - visible)
        scroll = text.cx - visible;
    if (scroll < 0)
        scroll = 0;

    if (scroll != m_scrollX)
    {
        m_scrollX = scroll;
        Invalidate();
    }

    if (!m_focused)
        return;

    int x = m_bounds.left + Scale(kTextPadding) + cursor.cx - m_scrollX;
    int y = m_bounds.top + (m_bounds.bottom - m_bounds.top - fontHeight) / 2;
    SetCaretPos(x, y);
}

void TextField::Paint(HDC dc) const
{
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    int width = m_bounds.right - m_bounds.left;
    int height = m_bounds.bottom - m_bounds.top;

    Gdiplus::SolidBrush fillBrush(ToGdiplusColor(theme::kSearchFill));
    graphics.FillRectangle(&fillBrush, static_cast<int>(m_bounds.left), static_cast<int>(m_bounds.top), width, height);

    if (!m_valid)
    {
        Gdiplus::Pen errorPen(ToGdiplusColor(theme::kSearchError), 1.5f);
        graphics.DrawRectangle(&errorPen, static_cast<int>(m_bounds.left), static_cast<int>(m_bounds.top), width - 1, height - 1);
    }

    HFONT font = CreateFieldFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    RECT textRect = m_bounds;
    textRect.left += Scale(kTextPadding);
    textRect.right -= Scale(kTextPadding);

    if (m_query.empty())
    {
        SetTextColor(dc, theme::kPlaceholderText);
        DrawTextW(dc, m_placeholder, -1, &textRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else
    {
        int savedDC = SaveDC(dc);
        IntersectClipRect(dc, textRect.left, textRect.top, textRect.right, textRect.bottom);

        SIZE size;
        GetTextExtentPoint32W(dc, m_query.c_str(), static_cast<int>(m_query.size()), &size);

        RECT queryRect = textRect;
        queryRect.left -= m_scrollX;
        queryRect.right = queryRect.left + size.cx;
        SetTextColor(dc, theme::kText);
        DrawTextW(dc, m_query.c_str(), -1, &queryRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);

        if (m_focused && CursorAtEnd() && !m_suggestion.empty())
        {
            RECT suggestionRect = queryRect;
            suggestionRect.left = queryRect.right;
            suggestionRect.right = textRect.right;
            SetTextColor(dc, theme::kPlaceholderText);
            DrawTextW(dc, m_suggestion.c_str(), -1, &suggestionRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
        }

        RestoreDC(dc, savedDC);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

bool TextField::OnLButtonDown(POINT pt)
{
    if (PtInRect(&m_bounds, pt))
    {
        HDC dc = GetDC(m_hWnd);

        if (!m_focused)
        {
            m_focused = true;
            CreateCaret(m_hWnd, nullptr, Scale(1), FontHeight(dc));
            ShowCaret(m_hWnd);
        }

        m_cursorPos = CharIndexFromX(dc, pt.x - (m_bounds.left + Scale(kTextPadding)) + m_scrollX);
        ReleaseDC(m_hWnd, dc);

        UpdateCaretPos();
        Invalidate();  // the suggestion only shows while the cursor is at the end
        return true;
    }

    Blur();
    return false;
}

void TextField::Focus()
{
    m_cursorPos = m_query.size();
    if (!m_focused)
    {
        m_focused = true;
        HDC dc = GetDC(m_hWnd);
        CreateCaret(m_hWnd, nullptr, Scale(1), FontHeight(dc));
        ReleaseDC(m_hWnd, dc);
        ShowCaret(m_hWnd);
    }
    UpdateCaretPos();
    Invalidate();
}

void TextField::Blur()
{
    if (!m_focused)
        return;
    m_focused = false;
    DestroyCaret();
    Invalidate();
}

void TextField::SetText(const std::wstring &text)
{
    m_query = text;
    m_cursorPos = m_query.size();
    UpdateCaretPos();
    Invalidate();
}

void TextField::SetSuggestion(const std::wstring &suggestion)
{
    if (suggestion == m_suggestion)
        return;
    m_suggestion = suggestion;
    Invalidate();
}

bool TextField::OnChar(wchar_t ch)
{
    if (ch == L'\b')
    {
        if (m_cursorPos == 0)
            return false;
        m_query.erase(m_cursorPos - 1, 1);
        m_cursorPos--;
    }
    else if (ch == 0x7F)
    {
        // Ctrl+Backspace arrives as DEL: delete the word before the cursor.
        size_t start = PrevWordBoundary(m_cursorPos);
        if (start == m_cursorPos)
            return false;
        m_query.erase(start, m_cursorPos - start);
        m_cursorPos = start;
    }
    else if (ch >= 0x20)
    {
        m_query.insert(m_cursorPos, 1, ch);
        m_cursorPos++;
    }
    else
    {
        return false;
    }

    UpdateCaretPos();
    Invalidate();
    return true;
}

bool TextField::OnKeyDown(WPARAM key, bool ctrl)
{
    bool changed = false;

    switch (key)
    {
    case VK_LEFT:
        if (ctrl)
            m_cursorPos = PrevWordBoundary(m_cursorPos);
        else if (m_cursorPos > 0)
            m_cursorPos--;
        break;
    case VK_RIGHT:
        if (ctrl)
            m_cursorPos = NextWordBoundary(m_cursorPos);
        else if (m_cursorPos < m_query.size())
            m_cursorPos++;
        break;
    case VK_HOME:
        m_cursorPos = 0;
        break;
    case VK_END:
        m_cursorPos = m_query.size();
        break;
    case VK_DELETE:
        if (m_cursorPos < m_query.size())
        {
            m_query.erase(m_cursorPos, 1);
            changed = true;
        }
        break;
    default:
        return false;
    }

    UpdateCaretPos();
    // Moving the cursor away from the end hides the suggestion.
    if (changed || !m_suggestion.empty())
        Invalidate();
    return changed;
}
