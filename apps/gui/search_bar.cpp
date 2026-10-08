#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "search_bar.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <cwctype>
#include <vector>

namespace
{
    const int kTextPadding = 10;
    const int kFontSize = 13;
}

int SearchBar::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void SearchBar::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

HFONT SearchBar::CreateSearchFont() const
{
    return CreateUIFont(Scale(kFontSize));
}

int SearchBar::FontHeight(HDC dc) const
{
    HFONT font = CreateSearchFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    return tm.tmHeight;
}

size_t SearchBar::CharIndexFromX(HDC dc, int targetX) const
{
    if (m_query.empty())
        return 0;

    HFONT font = CreateSearchFont();
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

size_t SearchBar::PrevWordBoundary(size_t pos) const
{
    auto isWordChar = [](wchar_t c) { return std::iswalnum(c) != 0; };

    while (pos > 0 && !isWordChar(m_query[pos - 1]))
        pos--;
    while (pos > 0 && isWordChar(m_query[pos - 1]))
        pos--;
    return pos;
}

size_t SearchBar::NextWordBoundary(size_t pos) const
{
    auto isWordChar = [](wchar_t c) { return std::iswalnum(c) != 0; };
    size_t size = m_query.size();

    while (pos < size && !isWordChar(m_query[pos]))
        pos++;
    while (pos < size && isWordChar(m_query[pos]))
        pos++;
    return pos;
}

void SearchBar::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_bounds = bounds;
    UpdateCaretPos();
}

void SearchBar::SetValid(bool valid)
{
    if (valid == m_valid)
        return;

    m_valid = valid;
    Invalidate();
}

void SearchBar::UpdateCaretPos() const
{
    if (!m_focused)
        return;

    HDC dc = GetDC(m_hWnd);
    HFONT font = CreateSearchFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SIZE size;
    GetTextExtentPoint32W(dc, m_query.c_str(), static_cast<int>(m_cursorPos), &size);

    int fontHeight = FontHeight(dc);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(m_hWnd, dc);

    int x = m_bounds.left + Scale(kTextPadding) + size.cx;
    int y = m_bounds.top + (m_bounds.bottom - m_bounds.top - fontHeight) / 2;
    SetCaretPos(x, y);
}

void SearchBar::Paint(HDC dc) const
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

    HFONT font = CreateSearchFont();
    HFONT oldFont = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    RECT textRect = m_bounds;
    textRect.left += Scale(kTextPadding);
    textRect.right -= Scale(kTextPadding);

    if (m_query.empty())
    {
        SetTextColor(dc, theme::kPlaceholderText);
        DrawTextW(dc, L"search by name or tag...", -1, &textRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        SetTextColor(dc, theme::kText);
        DrawTextW(dc, m_query.c_str(), -1, &textRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

bool SearchBar::OnLButtonDown(POINT pt)
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

        m_cursorPos = CharIndexFromX(dc, pt.x - (m_bounds.left + Scale(kTextPadding)));
        ReleaseDC(m_hWnd, dc);

        UpdateCaretPos();
        return true;
    }

    if (m_focused)
    {
        m_focused = false;
        DestroyCaret();
    }
    return false;
}

bool SearchBar::OnChar(wchar_t ch)
{
    if (ch == L'\b')
    {
        if (m_cursorPos == 0)
            return false;
        m_query.erase(m_cursorPos - 1, 1);
        m_cursorPos--;
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

bool SearchBar::OnKeyDown(WPARAM key, bool ctrl)
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
    if (changed)
        Invalidate();
    return changed;
}
