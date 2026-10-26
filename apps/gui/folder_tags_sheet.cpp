#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "folder_tags_sheet.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>

namespace
{
    const int kCardWidth = 440;
    const int kMargin = 24;
    const int kPadding = 20;
    const int kTitleHeight = 24;
    const int kIntroHeight = 22;
    const int kRowHeight = 30;
    const int kListGap = 8;
    const int kButtonGap = 16;
    const int kRowInset = 10;
    const int kBox = 12;          // the tick square
    const int kRowRadius = 3;
    const int kHoverSkip = -2;
    const int kHoverAdd = -3;
}

int FolderTagsSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

int FolderTagsSheet::Visible() const
{
    return std::min<int>(kVisibleRows, static_cast<int>(m_tags.size()));
}

RECT FolderTagsSheet::CardRect() const
{
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin));
    int height = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kIntroHeight) + Scale(kListGap) +
        Visible() * Scale(kRowHeight) + Scale(kButtonGap) + Scale(kButtonHeight);
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int top = std::max<int>(m_bounds.top, (m_bounds.top + m_bounds.bottom - height) / 2 - Scale(40));
    return { left, top, left + width, top + height };
}

RECT FolderTagsSheet::ListRect() const
{
    RECT card = CardRect();
    int top = card.top + Scale(kPadding) + Scale(kTitleHeight) + Scale(kIntroHeight) + Scale(kListGap);
    return { card.left + Scale(kPadding) - Scale(kRowInset) / 2, top, card.right - Scale(kPadding) + Scale(kRowInset) / 2,
        top + Visible() * Scale(kRowHeight) };
}

RECT FolderTagsSheet::RowRect(int slot) const
{
    RECT list = ListRect();
    return { list.left, list.top + slot * Scale(kRowHeight), list.right, list.top + (slot + 1) * Scale(kRowHeight) };
}

RECT FolderTagsSheet::ConfirmRect() const
{
    RECT card = CardRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kFolderTagsApply);
    int bottom = card.bottom - Scale(kPadding);
    return { card.right - Scale(kPadding) - width, bottom - Scale(kButtonHeight), card.right - Scale(kPadding), bottom };
}

RECT FolderTagsSheet::SkipRect() const
{
    RECT confirm = ConfirmRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kFolderTagsSkip);
    return { confirm.left - Scale(kButtonGap / 2) - width, confirm.top, confirm.left - Scale(kButtonGap / 2), confirm.bottom };
}

// The index of the tag whose row is under the point, or -1.
int FolderTagsSheet::RowAt(POINT pt) const
{
    for (int slot = 0; slot < Visible(); slot++)
    {
        RECT r = RowRect(slot);
        if (PtInRect(&r, pt))
            return m_first + slot;
    }
    return -1;
}

void FolderTagsSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, std::vector<FolderTag> tags)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_tags = std::move(tags);
    m_open = true;
    m_first = m_pick = 0;
    m_hover = -1;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void FolderTagsSheet::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_tags.clear();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void FolderTagsSheet::ScrollTo(int first)
{
    int max = std::max(0, static_cast<int>(m_tags.size()) - kVisibleRows);
    first = std::max(0, std::min(first, max));
    if (first != m_first)
    {
        m_first = first;
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
    }
}

void FolderTagsSheet::Pick(int index)
{
    int count = static_cast<int>(m_tags.size());
    m_pick = std::max(0, std::min(index, count - 1));
    if (m_pick < m_first)
        ScrollTo(m_pick);
    else if (m_pick >= m_first + kVisibleRows)
        ScrollTo(m_pick - kVisibleRows + 1);
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void FolderTagsSheet::OnHover(POINT pt)
{
    if (!m_open)
        return;
    RECT skip = SkipRect(), confirm = ConfirmRect();
    int hover = PtInRect(&skip, pt) ? kHoverSkip : PtInRect(&confirm, pt) ? kHoverAdd : RowAt(pt);
    if (hover != m_hover)
    {
        m_hover = hover;
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
    }
}

void FolderTagsSheet::OnMouseWheel(POINT, int delta)
{
    ScrollTo(m_first - (delta > 0 ? 1 : -1) * std::max(1, std::abs(delta) / WHEEL_DELTA));
}

FolderTagsSheet::Action FolderTagsSheet::OnLButtonDown(POINT pt, bool)
{
    RECT card = CardRect(), confirm = ConfirmRect(), skip = SkipRect();
    if (PtInRect(&confirm, pt))
        return Action::kConfirm;
    if (PtInRect(&skip, pt) || !PtInRect(&card, pt))
        return Action::kCancel;
    int row = RowAt(pt);
    if (row >= 0)
    {
        m_tags[row].on = !m_tags[row].on;
        m_pick = row;
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
    }
    return Action::kNone;
}

FolderTagsSheet::Action FolderTagsSheet::OnKeyDown(WPARAM key, bool ctrl)
{
    switch (key)
    {
    case VK_ESCAPE:
        return Action::kCancel;
    case VK_RETURN:
        return Action::kConfirm;
    case VK_UP:
        Pick(m_pick - 1);
        break;
    case VK_DOWN:
        Pick(m_pick + 1);
        break;
    case VK_SPACE:
        if (m_pick < static_cast<int>(m_tags.size()))
        {
            m_tags[m_pick].on = !m_tags[m_pick].on;
            InvalidateRect(m_hWnd, &m_bounds, FALSE);
        }
        break;
    case 'A':
        // Ctrl+A ticks them all, or unticks them all when they were.
        if (ctrl)
        {
            bool all = std::all_of(m_tags.begin(), m_tags.end(), [](const FolderTag &t) { return t.on; });
            for (FolderTag &t : m_tags)
                t.on = !all;
            InvalidateRect(m_hWnd, &m_bounds, FALSE);
        }
        break;
    }
    return Action::kNone;
}

void FolderTagsSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    PaintBackdrop(renderer, m_bounds);
    RECT card = CardRect();
    PaintCard(renderer, m_hWnd, card);

    RECT title = { card.left + Scale(kPadding), card.top + Scale(kPadding), card.right - Scale(kPadding),
        card.top + Scale(kPadding) + Scale(kTitleHeight) };
    renderer.DrawTextLine(strings::kFolderTagsTitle, Font::kBodyStrong, title, theme::kText1);
    RECT intro = { title.left, title.bottom, title.right, title.bottom + Scale(kIntroHeight) };
    renderer.DrawTextLine(strings::kFolderTagsIntro, Font::kCaption, intro, theme::kText3);

    const float line = Scale(4) / 4.0f;
    for (int slot = 0; slot < Visible(); slot++)
    {
        int index = m_first + slot;
        const FolderTag &tag = m_tags[index];
        RECT row = RowRect(slot);
        D2D1_RECT_F box = ToRectF(row);
        if (index == m_hover)
            renderer.FillRoundedRect(box, static_cast<float>(Scale(kRowRadius)), theme::kHover);
        if (index == m_pick)
            renderer.DrawRoundedRect({ box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2 },
                static_cast<float>(Scale(kRowRadius)), Faded(theme::kYuri, 0.5f), line);

        // The square of the sidebar's states: filled violet when ticked, hollow when not.
        float size = static_cast<float>(Scale(kBox));
        float x = row.left + static_cast<float>(Scale(kRowInset));
        float y = (row.top + row.bottom - size) / 2.0f;
        D2D1_RECT_F square = { x, y, x + size, y + size };
        if (tag.on)
            renderer.FillRoundedRect(square, static_cast<float>(Scale(2)), theme::kYuri);
        else
            renderer.DrawRoundedRect({ square.left + line / 2, square.top + line / 2, square.right - line / 2, square.bottom - line / 2 },
                static_cast<float>(Scale(2)), theme::kText3, line);

        RECT text = { static_cast<LONG>(x + size) + Scale(10), row.top, row.right - Scale(kRowInset), row.bottom };
        renderer.DrawTextLine(Utf8ToWide(tag.name), Font::kBody, text, tag.on ? theme::kText1 : theme::kText2);
        renderer.DrawTextLine(FormatCount(tag.paths.size()), Font::kMono, text, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    PaintButton(renderer, m_hWnd, SkipRect(), strings::kFolderTagsSkip, false, m_hover == kHoverSkip);
    PaintButton(renderer, m_hWnd, ConfirmRect(), strings::kFolderTagsApply, true, m_hover == kHoverAdd);
}
