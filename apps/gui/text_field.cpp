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
    const int kErrorGap = 4;      // between the field and the message under it
    const int kErrorPadding = 8;
    const int kErrorHeight = 22;
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

bool TextField::DeleteSelection()
{
    if (!HasSelection())
        return false;
    size_t start = SelStart();
    m_query.erase(start, SelEnd() - start);
    m_cursorPos = m_anchor = start;
    return true;
}

void TextField::CopySelection() const
{
    if (!HasSelection() || !OpenClipboard(m_hWnd))
        return;
    EmptyClipboard();
    size_t count = SelEnd() - SelStart();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (count + 1) * sizeof(wchar_t));
    if (mem)
    {
        wchar_t *dest = static_cast<wchar_t *>(GlobalLock(mem));
        std::copy_n(m_query.begin() + SelStart(), count, dest);
        dest[count] = L'\0';
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem))
            GlobalFree(mem);
    }
    CloseClipboard();
}

// Replaces the selection with the text on the clipboard, line breaks turned into spaces.
bool TextField::Paste()
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(m_hWnd))
        return false;
    std::wstring text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
    {
        if (const wchar_t *src = static_cast<const wchar_t *>(GlobalLock(data)))
        {
            text = src;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();

    for (wchar_t &ch : text)
        if (ch < L' ')
            ch = L' ';
    if (text.empty())
        return false;
    DeleteSelection();
    m_query.insert(m_cursorPos, text);
    m_cursorPos += text.size();
    m_anchor = m_cursorPos;
    return true;
}

void TextField::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    UpdateCaretPos();
}

void TextField::OnDpiChanged()
{
    if (m_focused)
    {
        DestroyCaret();
        CreateSystemCaret();
    }
    UpdateCaretPos();
}

void TextField::SetPlaceholder(const wchar_t *placeholder)
{
    if (placeholder == m_placeholder)
        return;
    m_placeholder = placeholder;
    Invalidate();
}

void TextField::SetError(const std::wstring &message)
{
    if (message == m_error)
        return;

    // The old message and the new one may be of different widths.
    RECT old = ErrorRect();
    m_error = message;
    RECT now = ErrorRect();
    Invalidate();
    if (m_hWnd)
    {
        InvalidateRect(m_hWnd, &old, FALSE);
        InvalidateRect(m_hWnd, &now, FALSE);
    }
}

// The message hangs under the field, lined up with its text, over whatever is below like a tooltip.
RECT TextField::ErrorRect() const
{
    if (m_error.empty() || !m_renderer)
        return RECT{};
    int left = TextLeft() - Scale(kErrorPadding);
    int width = static_cast<int>(m_renderer->TextWidth(m_error, Font::kCaption) + 0.99f) + 2 * Scale(kErrorPadding);
    int top = m_bounds.bottom + Scale(kErrorGap);
    RECT r = { left, top, left + width, top + Scale(kErrorHeight) };
    if (r.right > m_bounds.right)
        r.right = m_bounds.right;
    return r;
}

bool TextField::OverError(POINT pt) const
{
    RECT error = ErrorRect();
    return PtInRect(&error, pt) != 0;
}

// Scrolls the text so the cursor stays inside the field, then moves the caret to it. While the cursor is at the
// end with a suggestion after it, as much of the suggestion as fits is scrolled into view too.
void TextField::UpdateCaretPos()
{
    if (!m_hWnd || !m_renderer)
        return;

    float cursor = TextX(m_cursorPos);
    float end = m_renderer->TextWidth(m_query, Font::kBody);
    bool suggesting = m_focused && CursorAtEnd() && !HasSelection() && !m_suggestion.empty();
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
    if (!m_error.empty())
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

    if (!m_error.empty())
    {
        RECT error = ErrorRect();
        D2D1_RECT_F errorBox = ToRectF(error);
        renderer.FillRoundedRect(errorBox, radius, theme::kControl);
        renderer.DrawRoundedRect(D2D1::RectF(errorBox.left + line / 2, errorBox.top + line / 2, errorBox.right - line / 2,
            errorBox.bottom - line / 2), radius, theme::kStroke, line);
        InflateRect(&error, -Scale(kErrorPadding), 0);
        renderer.DrawTextLine(m_error, Font::kCaption, error, theme::kRose);
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

    if (m_focused && HasSelection())
    {
        float left = x + TextX(SelStart()), right = x + TextX(SelEnd());
        renderer.FillRect(D2D1::RectF(left, y, right, y + lineHeight), theme::Rgb(0x9A6FE0, 0.45f));
    }

    ComPtr<IDWriteTextLayout> query = renderer.Layout(m_query, Font::kBody, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    renderer.DrawLayout(query.Get(), x, y, theme::kText1);

    if (m_focused && CursorAtEnd() && !HasSelection() && !m_suggestion.empty())
    {
        ComPtr<IDWriteTextLayout> suggestion = renderer.Layout(m_suggestion, Font::kBody, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        renderer.DrawLayout(suggestion.Get(), x + renderer.TextWidth(m_query, Font::kBody), y, theme::kText3);
    }

    renderer.PopClip();
}

bool TextField::OnLButtonDown(POINT pt, bool doubleClick)
{
    if (PtInRect(&m_bounds, pt))
    {
        if (!m_focused)
        {
            m_focused = true;
            CreateSystemCaret();
        }

        size_t index = CharIndexFromX(pt.x - TextLeft() + m_scrollX);
        if (doubleClick)
        {
            auto isWordChar = [](wchar_t c) { return std::iswalnum(c) != 0; };
            size_t start = index, end = index;
            while (start > 0 && isWordChar(m_query[start - 1]))
                start--;
            while (end < m_query.size() && isWordChar(m_query[end]))
                end++;
            m_anchor = start;
            m_cursorPos = end;
        }
        else
        {
            m_cursorPos = index;
            if ((GetKeyState(VK_SHIFT) & 0x8000) == 0)
                m_anchor = index;
            if (m_dragEnabled)
            {
                m_dragging = true;
                SetCapture(m_hWnd);
            }
        }

        UpdateCaretPos();
        Invalidate();  // the suggestion only shows while the cursor is at the end
        return true;
    }
    if (OverError(pt))
        return true;

    Blur();
    return false;
}

bool TextField::OnMouseMove(POINT pt)
{
    if (!m_dragging)
        return false;
    size_t index = CharIndexFromX(pt.x - TextLeft() + m_scrollX);
    if (index != m_cursorPos)
    {
        m_cursorPos = index;
        UpdateCaretPos();
        Invalidate();
    }
    return true;
}

void TextField::OnLButtonUp()
{
    if (!m_dragging)
        return;
    m_dragging = false;
    ReleaseCapture();
}

void TextField::Select(size_t start, size_t end)
{
    m_anchor = std::min(start, m_query.size());
    m_cursorPos = std::min(end, m_query.size());
    UpdateCaretPos();
    Invalidate();
}

void TextField::Focus()
{
    if (!m_focused)
    {
        m_cursorPos = m_anchor = m_query.size();
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
    m_dragging = false;
    DestroyCaret();
    UpdateCaretPos();  // the suggestion is gone, and the scroll that showed it
    Invalidate();
}

void TextField::SetText(const std::wstring &text)
{
    m_query = text;
    m_cursorPos = m_anchor = m_query.size();
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
        if (!DeleteSelection())
        {
            if (m_cursorPos == 0)
                return false;
            m_query.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
            m_anchor = m_cursorPos;
        }
    }
    else if (ch == 0x7F)
    {
        // Ctrl+Backspace arrives as DEL: delete the selection, or the word before the cursor.
        if (DeleteSelection())
        {
            UpdateCaretPos();
            Invalidate();
            return true;
        }
        size_t start = PrevWordBoundary(m_cursorPos);
        if (start == m_cursorPos)
            return false;
        m_query.erase(start, m_cursorPos - start);
        m_cursorPos = m_anchor = start;
    }
    else if (ch >= 0x20)
    {
        DeleteSelection();
        m_query.insert(m_cursorPos, 1, ch);
        m_cursorPos++;
        m_anchor = m_cursorPos;
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
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool moved = false;  // the cursor went somewhere: the selection collapses unless Shift is held

    switch (key)
    {
    case VK_LEFT:
        if (HasSelection() && !shift && !ctrl)
            m_cursorPos = SelStart();
        else if (ctrl)
            m_cursorPos = PrevWordBoundary(m_cursorPos);
        else if (m_cursorPos > 0)
            m_cursorPos--;
        moved = true;
        break;
    case VK_RIGHT:
        if (HasSelection() && !shift && !ctrl)
            m_cursorPos = SelEnd();
        else if (ctrl)
            m_cursorPos = NextWordBoundary(m_cursorPos);
        else if (m_cursorPos < m_query.size())
            m_cursorPos++;
        moved = true;
        break;
    case VK_HOME:
        m_cursorPos = 0;
        moved = true;
        break;
    case VK_END:
        m_cursorPos = m_query.size();
        moved = true;
        break;
    case VK_DELETE:
        if (DeleteSelection())
            changed = true;
        else if (m_cursorPos < m_query.size())
        {
            m_query.erase(m_cursorPos, 1);
            changed = true;
        }
        break;
    case 'A':
        if (!ctrl)
            return false;
        m_anchor = 0;
        m_cursorPos = m_query.size();
        break;
    case 'C':
        if (!ctrl)
            return false;
        CopySelection();
        return false;
    case 'X':
        if (!ctrl)
            return false;
        CopySelection();
        changed = DeleteSelection();
        break;
    case 'V':
        if (!ctrl)
            return false;
        changed = Paste();
        break;
    default:
        return false;
    }

    if (moved && !shift)
        m_anchor = m_cursorPos;
    UpdateCaretPos();
    // Moving the cursor away from the end hides the suggestion, and a selection has to be redrawn.
    Invalidate();
    return changed;
}
