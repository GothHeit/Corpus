#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "text_field.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cwctype>

namespace
{
    const int kTextPadding = 10;
    const int kIconWidth = 22;  // the magnifying glass and the gap after it
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

int TextField::TextLeft() const
{
    return m_bounds.left + Scale(kTextPadding) + (m_searchIcon ? Scale(kIconWidth) : 0);
}

int TextField::CaretHeight() const
{
    return static_cast<int>(m_renderer->LineHeight(Font::kBody) + 0.5f);
}

void TextField::CreateSystemCaret()
{
    CreateCaret(m_hWnd, nullptr, Scale(1), CaretHeight());
    ShowCaret(m_hWnd);
}

// Where the character at 'pos' starts, from the start of the text.
float TextField::TextX(size_t pos) const
{
    if (pos == 0)
        return 0.0f;
    ComPtr<IDWriteTextLayout> layout = m_renderer->Layout(m_query, Font::kBody, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    float x = 0.0f, y = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics;
    if (!layout || FAILED(layout->HitTestTextPosition(static_cast<UINT32>(pos), FALSE, &x, &y, &metrics)))
        return 0.0f;
    return x;
}

// The cursor position closest to 'x', from the start of the text.
size_t TextField::CharIndexFromX(float x) const
{
    if (m_query.empty())
        return 0;
    ComPtr<IDWriteTextLayout> layout = m_renderer->Layout(m_query, Font::kBody, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics;
    if (!layout || FAILED(layout->HitTestPoint(x, 0.0f, &trailing, &inside, &metrics)))
        return 0;
    size_t index = metrics.textPosition + (trailing ? metrics.length : 0);
    return index < m_query.size() ? index : m_query.size();
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

void TextField::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
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

// Scrolls the text so the cursor stays inside the field, then moves the caret to it. While the cursor is at the
// end with a suggestion after it, as much of the suggestion as fits is scrolled into view too.
void TextField::UpdateCaretPos()
{
    if (!m_hWnd || !m_renderer)
        return;

    float cursor = TextX(m_cursorPos);
    float end = m_renderer->TextWidth(m_query, Font::kBody);
    bool suggesting = m_focused && CursorAtEnd() && !m_suggestion.empty();
    if (suggesting)
        end += m_renderer->TextWidth(m_suggestion, Font::kBody);

    float visible = static_cast<float>((m_bounds.right - Scale(kTextPadding)) - TextLeft());
    if (visible < 0.0f)
        visible = 0.0f;

    float scroll = m_scrollX;
    if (cursor - scroll > visible)
        scroll = cursor - visible;
    if (cursor < scroll)
        scroll = cursor;
    // No empty space after the end of the text while part of it is hidden on the left.
    if (scroll > end - visible)
        scroll = end - visible;
    // As much of the suggestion as fits, keeping the cursor in view.
    if (suggesting && scroll < std::min(end - visible, cursor))
        scroll = std::min(end - visible, cursor);
    if (scroll < 0.0f)
        scroll = 0.0f;

    if (scroll != m_scrollX)
    {
        m_scrollX = scroll;
        Invalidate();
    }

    if (!m_focused)
        return;

    int x = TextLeft() + static_cast<int>(cursor - m_scrollX + 0.5f);
    int y = m_bounds.top + (m_bounds.bottom - m_bounds.top - CaretHeight()) / 2;
    SetCaretPos(x, y);
}

void TextField::Paint(Renderer &renderer) const
{
    const float radius = static_cast<float>(Scale(theme::kControlRadius));
    D2D1_RECT_F box = ToRectF(m_bounds);
    renderer.FillRoundedRect(box, radius, theme::kControl);

    // A thin edge, in the accent while focused. An invalid query also underlines the field.
    float line = Scale(4) / 4.0f;
    D2D1_RECT_F edge = D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2);
    renderer.DrawRoundedRect(edge, radius, m_focused ? theme::kYuri : theme::kStroke, line);
    if (!m_valid)
    {
        D2D1_RECT_F underline = D2D1::RectF(box.left + radius, box.bottom - 2 * line, box.right - radius, box.bottom);
        renderer.FillRect(underline, theme::kRose);
    }

    if (m_searchIcon)
    {
        // A magnifying glass: a ring and a handle at 45 degrees.
        float r = Scale(10) / 2.0f;
        float cx = m_bounds.left + Scale(kTextPadding) + r + line;
        float cy = (m_bounds.top + m_bounds.bottom) / 2.0f - line;
        D2D1_COLOR_F color = m_focused ? theme::kText2 : theme::kText3;
        renderer.DrawCircle(D2D1::Point2F(cx, cy), r, color, 1.5f * line);
        float handle = r * 0.7071f;
        renderer.DrawLine(D2D1::Point2F(cx + handle, cy + handle), D2D1::Point2F(cx + handle + r * 0.8f, cy + handle + r * 0.8f), color, 1.5f * line);
    }

    RECT textRect = m_bounds;
    textRect.left = TextLeft();
    textRect.right -= Scale(kTextPadding);
    if (textRect.right <= textRect.left)
        return;

    if (m_query.empty())
    {
        renderer.DrawTextLine(m_placeholder, Font::kBody, textRect, theme::kText3);
        return;
    }

    renderer.PushClip(textRect);

    float lineHeight = renderer.LineHeight(Font::kBody);
    float x = textRect.left - m_scrollX;
    float y = textRect.top + (textRect.bottom - textRect.top - lineHeight) / 2.0f;

    ComPtr<IDWriteTextLayout> query = renderer.Layout(m_query, Font::kBody, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    renderer.DrawLayout(query.Get(), x, y, theme::kText1);

    if (m_focused && CursorAtEnd() && !m_suggestion.empty())
    {
        ComPtr<IDWriteTextLayout> suggestion = renderer.Layout(m_suggestion, Font::kBody, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        renderer.DrawLayout(suggestion.Get(), x + renderer.TextWidth(m_query, Font::kBody), y, theme::kText3);
    }

    renderer.PopClip();
}

bool TextField::OnLButtonDown(POINT pt)
{
    if (PtInRect(&m_bounds, pt))
    {
        if (!m_focused)
        {
            m_focused = true;
            CreateSystemCaret();
        }

        m_cursorPos = CharIndexFromX(pt.x - TextLeft() + m_scrollX);

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
        CreateSystemCaret();
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
    UpdateCaretPos();  // the suggestion is gone, and the scroll that showed it
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
    UpdateCaretPos();
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
