#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "search_bar.hpp"
#include "ui_font.hpp"
#include <gdiplus.h>
#include <cwctype>
#include <vector>

int SearchBar::Scale(HWND hWnd, int value) const
{
    return MulDiv(value, GetDpiForWindow(hWnd), 96);
}

HFONT SearchBar::CreateSearchFont(HWND hWnd) const
{
    return CreateUIFont(Scale(hWnd, 13));
}

int SearchBar::FontHeight(HWND hWnd, HDC dc) const
{
    HFONT font = CreateSearchFont(hWnd);
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    return tm.tmHeight;
}

size_t SearchBar::CharIndexFromX(HWND hWnd, HDC dc, int targetX) const
{
    if (m_query.empty())
        return 0;

    HFONT font = CreateSearchFont(hWnd);
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

RECT SearchBar::GetRect(HWND hWnd, const RECT &client, int top) const
{
    const int margin = Scale(hWnd, 20);
    const int height = Scale(hWnd, kHeight);
    RECT rect = { margin, top, client.right - margin, top + height };
    return rect;
}

void SearchBar::UpdateCaretPos(HWND hWnd, const RECT &client, int top) const
{
    if (!m_focused)
        return;

    RECT barRect = GetRect(hWnd, client, top);

    HDC dc = GetDC(hWnd);
    HFONT font = CreateSearchFont(hWnd);
    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SIZE size;
    GetTextExtentPoint32W(dc, m_query.c_str(), static_cast<int>(m_cursorPos), &size);

    int fontHeight = FontHeight(hWnd, dc);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(hWnd, dc);

    int x = barRect.left + Scale(hWnd, kTextPadding) + size.cx;
    int y = barRect.top + (barRect.bottom - barRect.top - fontHeight) / 2;
    SetCaretPos(x, y);
}

void SearchBar::Draw(HDC dc, HWND hWnd, const RECT &client, int top) const
{
    RECT barRect = GetRect(hWnd, client, top);

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 45, 45, 50));
    graphics.FillRectangle(&barBrush,
        static_cast<int>(barRect.left), static_cast<int>(barRect.top),
        static_cast<int>(barRect.right - barRect.left), static_cast<int>(barRect.bottom - barRect.top));

    if (!m_valid)
    {
        Gdiplus::Pen errorPen(Gdiplus::Color(255, 220, 60, 60), 1.5f);
        graphics.DrawRectangle(&errorPen,
            static_cast<int>(barRect.left), static_cast<int>(barRect.top),
            static_cast<int>(barRect.right - barRect.left - 1), static_cast<int>(barRect.bottom - barRect.top - 1));
    }

    HFONT font = CreateSearchFont(hWnd);
    HFONT oldFont = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    RECT textRect = barRect;
    textRect.left += Scale(hWnd, kTextPadding);
    textRect.right -= Scale(hWnd, kTextPadding);

    if (m_query.empty())
    {
        SetTextColor(dc, RGB(140, 140, 140));
        DrawTextW(dc, L"search by name or tag...", -1, &textRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        SetTextColor(dc, RGB(230, 230, 230));
        DrawTextW(dc, m_query.c_str(), -1, &textRect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
}

bool SearchBar::OnLButtonDown(HWND hWnd, const RECT &client, int top, POINT pt)
{
    RECT rect = GetRect(hWnd, client, top);

    if (PtInRect(&rect, pt))
    {
        HDC dc = GetDC(hWnd);

        if (!m_focused)
        {
            m_focused = true;
            CreateCaret(hWnd, nullptr, Scale(hWnd, 1), FontHeight(hWnd, dc));
            ShowCaret(hWnd);
        }

        m_cursorPos = CharIndexFromX(hWnd, dc, pt.x - (rect.left + Scale(hWnd, kTextPadding)));
        ReleaseDC(hWnd, dc);

        UpdateCaretPos(hWnd, client, top);
        return true;
    }

    if (m_focused)
    {
        m_focused = false;
        DestroyCaret();
    }
    return false;
}

bool SearchBar::OnChar(HWND hWnd, wchar_t ch, const RECT &client, int top)
{
    if (ch == L'\b')
    {
        if (m_cursorPos > 0)
        {
            m_query.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
        }
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

    UpdateCaretPos(hWnd, client, top);
    return true;
}

bool SearchBar::OnKeyDown(HWND hWnd, WPARAM key, bool ctrl, const RECT &client, int top)
{
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
            m_query.erase(m_cursorPos, 1);
        break;
    default:
        return false;
    }

    UpdateCaretPos(hWnd, client, top);
    return true;
}
