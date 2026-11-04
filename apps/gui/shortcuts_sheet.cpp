#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "shortcuts_sheet.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    enum Section { kEverywhere, kGridKeys, kSearchKeys, kViewerKeys, kMediaKeys, kBookKeys, kMouseKeys, kSettingsKeys, kSectionCount };

    struct Entry
    {
        Section section;
        const wchar_t *keys;    // key caps, '|' between them
        const wchar_t *action;
    };

    // Every shortcut the user can look up, in the order each section shows them. Keep it in step with window_keys.cpp
    // (the window's shortcuts), file_grid.cpp (the grid's keys) and viewer_input.cpp (the viewer's).
    const Entry kEntries[] = {
        { kEverywhere, L"Ctrl+F", L"Search" },
        { kEverywhere, L"Ctrl+I", L"Info pane" },
        { kEverywhere, L"Ctrl+B", L"Sidebar" },
        { kEverywhere, L"Ctrl+,", L"Settings" },
        { kEverywhere, L"Ctrl+O", L"Add folders" },
        { kEverywhere, L"Ctrl+T", L"Add tags" },
        { kEverywhere, L"Ctrl++|Ctrl+-", L"Tile size" },
        { kEverywhere, L"Ctrl+Z", L"Undo" },
        { kEverywhere, L"Ctrl+Y|Ctrl+Shift+Z", L"Redo" },
        { kEverywhere, L"Tab|Shift+Tab", L"Search, grid, tags" },
        { kEverywhere, L"Ctrl+Space", L"Play or pause music" },
        { kEverywhere, L"F11", L"Fullscreen" },

        { kGridKeys, L"Arrows", L"Move" },
        { kGridKeys, L"Shift+Arrows", L"Extend selection" },
        { kGridKeys, L"Ctrl+A", L"Select all" },
        { kGridKeys, L"Esc", L"Clear selection" },
        { kGridKeys, L"PgUp|PgDn", L"Scroll a page" },
        { kGridKeys, L"Home|End", L"Top or bottom" },
        { kGridKeys, L"Enter", L"Open" },
        { kGridKeys, L"Space", L"Quick look" },
        { kGridKeys, L"Space", L"Song: play or pause" },
        { kGridKeys, L"F2", L"Rename" },
        { kGridKeys, L"Del", L"Remove from library" },
        { kGridKeys, L"Shift+Del", L"Also to Recycle Bin" },
        { kGridKeys, L"Ctrl+C", L"Copy files" },
        { kGridKeys, L"Q", L"Add to queue" },
        { kGridKeys, L"Shift+Q", L"Play next" },
        { kGridKeys, L"Menu|Shift+F10", L"Context menu" },

        { kSearchKeys, L"Esc", L"Clear, then leave" },
        { kSearchKeys, L"Enter|Down", L"First result" },

        { kViewerKeys, L"Esc|Space", L"Close (picture)" },
        { kViewerKeys, L"Left|Right", L"Previous or next" },
        { kViewerKeys, L"Up|Down", L"Previous or next (picture)" },
        { kViewerKeys, L"PgUp|PgDn", L"Previous or next" },
        { kViewerKeys, L"Home|End", L"First or last" },
        { kViewerKeys, L"F|F11", L"Fullscreen" },
        { kViewerKeys, L"+|-", L"Zoom" },
        { kViewerKeys, L"0", L"Fit" },
        { kViewerKeys, L"Z", L"Actual size (picture)" },
        { kViewerKeys, L"P", L"Sharp pixels / smooth (picture)" },
        { kViewerKeys, L"Ctrl+T", L"Tag row" },

        { kMediaKeys, L"Space|K", L"Play or pause" },
        { kMediaKeys, L"Left|Right", L"Seek; paused: step files" },
        { kMediaKeys, L"Ctrl+Left|Ctrl+Right", L"Step files" },
        { kMediaKeys, L"Up|Down", L"Volume" },
        { kMediaKeys, L"M", L"Mute" },
        { kMediaKeys, L"A", L"Next audio track" },
        { kMediaKeys, L"C", L"Next subtitle track, then off" },
        { kMediaKeys, L"[|]", L"Slower or faster" },
        { kMediaKeys, L"Backspace", L"Normal speed" },
        { kMediaKeys, L"Q", L"Queue song under pointer" },
        { kMediaKeys, L"Shift+Q", L"Play it next" },
        { kMediaKeys, L"Del", L"Unqueue song under pointer" },

        { kBookKeys, L"Left|Right", L"Turn page" },
        { kBookKeys, L"Up|Down", L"Scroll, then turn" },
        { kBookKeys, L"PgUp|PgDn", L"Scroll a view" },
        { kBookKeys, L"Space", L"Read on" },
        { kBookKeys, L"Shift+Space", L"Read back" },
        { kBookKeys, L"Home|End", L"First or last page" },
        { kBookKeys, L"Ctrl+Left|Ctrl+Right", L"Step files" },
        { kBookKeys, L"W", L"Fit page or width" },
        { kBookKeys, L"R", L"Reading direction" },

        { kMouseKeys, L"Double-click", L"Actual size (picture)" },
        { kMouseKeys, L"Wheel", L"Zoom at pointer" },
        { kMouseKeys, L"Drag", L"Pan" },
        { kMouseKeys, L"Ctrl+click|Shift+click", L"Select" },
        { kMouseKeys, L"Double-click", L"Maximize (title area)" },
        { kMouseKeys, L"Right-click", L"Context menu" },

        { kSettingsKeys, L"Esc", L"Back" },
    };

    const wchar_t *const kSectionTitles[kSectionCount] = {
        strings::kShortcutsEverywhere, strings::kShortcutsGrid, strings::kShortcutsSearch, strings::kShortcutsViewer,
        strings::kShortcutsMedia, strings::kShortcutsBooks, strings::kShortcutsMouse, strings::kShortcutsSettings,
    };

    const int kMargin = 24;
    const int kColumnGap = 32;
    const int kColumnMax = 380;
    const int kWideWindow = 900;   // narrower windows get one column
    const int kTopPadding = 20;
    const int kTitleHeight = 28;
    const int kRowHeight = 28;
    const int kSectionGap = 20;
    const int kKeyHeight = 20;
    const int kKeyPadding = 6;
    const int kKeyGap = 4;
    const int kKeysToAction = 12;
    const int kSlide = 40;
    const int kWheelStep = 96;

    std::vector<std::wstring> SplitKeys(const wchar_t *keys)
    {
        std::vector<std::wstring> out;
        std::wstring current;
        for (const wchar_t *p = keys; *p; p++)
        {
            if (*p == L'|')
            {
                out.push_back(current);
                current.clear();
            }
            else
                current += *p;
        }
        out.push_back(current);
        return out;
    }

    // The sections in the order a context shows them: its own first, the viewer's after it for the viewer's kinds.
    std::vector<int> SectionOrder(ShortcutsSheet::Context context)
    {
        using C = ShortcutsSheet::Context;
        std::vector<int> order;
        auto add = [&order](int section)
        {
            if (std::find(order.begin(), order.end(), section) == order.end())
                order.push_back(section);
        };
        switch (context)
        {
        case C::kGrid:     add(kGridKeys); break;
        case C::kPicture:  add(kViewerKeys); break;
        case C::kVideo:
        case C::kMusic:    add(kMediaKeys); add(kViewerKeys); break;
        case C::kBook:     add(kBookKeys); add(kViewerKeys); break;
        case C::kSettings: add(kSettingsKeys); break;
        }
        for (int section = 0; section < kSectionCount; section++)
            add(section);
        return order;
    }

    int OwnSection(ShortcutsSheet::Context context)
    {
        using C = ShortcutsSheet::Context;
        switch (context)
        {
        case C::kGrid:     return kGridKeys;
        case C::kPicture:  return kViewerKeys;
        case C::kVideo:
        case C::kMusic:    return kMediaKeys;
        case C::kBook:     return kBookKeys;
        case C::kSettings: return kSettingsKeys;
        }
        return -1;
    }

    int RowCount(int section)
    {
        int count = 0;
        for (const Entry &entry : kEntries)
            count += entry.section == section;
        return count;
    }
}

int ShortcutsSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

// Puts each section, in the context's order, at the top of the shortest column.
void ShortcutsSheet::Layout()
{
    int width = static_cast<int>(m_bounds.right - m_bounds.left);
    m_columns = width >= Scale(kWideWindow) ? 3 : 1;
    int gap = Scale(kColumnGap);
    int room = width - 2 * Scale(kMargin) - (m_columns - 1) * gap;
    m_columnWidth = std::max(1, std::min(room / m_columns, Scale(kColumnMax)));
    int total = m_columns * m_columnWidth + (m_columns - 1) * gap;
    m_left = m_bounds.left + (width - total) / 2;

    m_blocks.clear();
    std::vector<int> heights(m_columns, Scale(kTopPadding));
    for (int section : SectionOrder(m_context))
    {
        int column = static_cast<int>(std::min_element(heights.begin(), heights.end()) - heights.begin());
        int height = Scale(kTitleHeight) + RowCount(section) * Scale(kRowHeight);
        m_blocks.push_back({ section, column, heights[column], height });
        heights[column] += height + Scale(kSectionGap);
    }
    m_contentHeight = *std::max_element(heights.begin(), heights.end());
    ScrollTo(m_scroll);
}

void ShortcutsSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, int top, Context context)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_top = top;
    m_context = context;
    m_open = true;
    m_scroll = 0;
    m_start = AnimationsEnabled() ? GetTickCount64() : 0;
    Layout();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void ShortcutsSheet::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_start = 0;
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void ShortcutsSheet::SetBounds(const RECT &bounds)
{
    m_bounds = bounds;
    if (m_open)
        Layout();
}

// How far the slide is: 0 just opened, 1 landed (and always, with animations off). Fast at first, slowing down.
float ShortcutsSheet::Progress() const
{
    if (!m_start)
        return 1.0f;
    float t = static_cast<float>(GetTickCount64() - m_start) / theme::kPaneMs;
    if (t >= 1.0f)
        return 1.0f;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

bool ShortcutsSheet::OnTimer()
{
    if (!m_open)
        return false;
    InvalidateRect(m_hWnd, nullptr, FALSE);
    if (Progress() >= 1.0f)
    {
        m_start = 0;
        return false;
    }
    return true;
}

void ShortcutsSheet::ScrollTo(int y)
{
    int max = std::max(0, m_contentHeight + Scale(kMargin) - AreaHeight());
    y = std::max(0, std::min(y, max));
    if (y != m_scroll)
    {
        m_scroll = y;
        if (m_open)
            InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

Sheet::Action ShortcutsSheet::OnLButtonDown(POINT, bool)
{
    return Action::kCancel;
}

Sheet::Action ShortcutsSheet::OnKeyDown(WPARAM key, bool)
{
    int step = Scale(kWheelStep);
    int page = std::max(step, AreaHeight() - step);
    switch (key)
    {
    case VK_ESCAPE:
    case VK_F1:    return Action::kCancel;
    case VK_UP:    ScrollTo(m_scroll - step / 3); break;
    case VK_DOWN:  ScrollTo(m_scroll + step / 3); break;
    case VK_PRIOR: ScrollTo(m_scroll - page); break;
    case VK_NEXT:  ScrollTo(m_scroll + page); break;
    case VK_HOME:  ScrollTo(0); break;
    case VK_END:   ScrollTo(m_contentHeight); break;
    }
    return Action::kNone;
}

void ShortcutsSheet::OnMouseWheel(POINT, int delta)
{
    ScrollTo(m_scroll - delta * Scale(kWheelStep) / WHEEL_DELTA);
}

void ShortcutsSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    float progress = Progress();
    RECT area = { m_bounds.left, m_top, m_bounds.right, m_bounds.bottom };
    renderer.PushClip(area);
    renderer.FillRect(ToRectF(area), Faded(theme::kBase, 0.98f * progress));

    const int offset = -static_cast<int>((1.0f - progress) * Scale(kSlide));
    const int originY = m_top - m_scroll + offset;
    const int own = OwnSection(m_context);
    const float radius = static_cast<float>(Scale(theme::kChipRadius));

    for (const Block &block : m_blocks)
    {
        int x = m_left + block.column * (m_columnWidth + Scale(kColumnGap));
        int y = originY + block.top;
        if (y + block.height < m_top || y > m_bounds.bottom)
            continue;
        RECT title = { x, y, x + m_columnWidth, y + Scale(kTitleHeight) };
        renderer.DrawTextLine(kSectionTitles[block.section], Font::kCaptionStrong, title,
            Faded(block.section == own ? theme::kYuri : theme::kText3, progress));
        y = title.bottom;

        for (const Entry &entry : kEntries)
        {
            if (entry.section != block.section)
                continue;
            RECT row = { x, y, x + m_columnWidth, y + Scale(kRowHeight) };
            y = row.bottom;
            if (row.bottom < m_top || row.top > m_bounds.bottom)
                continue;

            // The key caps at the right end of the row, the action in what is left.
            std::vector<std::wstring> keys = SplitKeys(entry.keys);
            std::vector<int> widths;
            int total = 0;
            for (const std::wstring &key : keys)
            {
                widths.push_back(static_cast<int>(renderer.TextWidth(key, Font::kMono) + 0.99f) + 2 * Scale(kKeyPadding));
                total += widths.back() + (total ? Scale(kKeyGap) : 0);
            }
            int capLeft = row.right - total;
            int capTop = (row.top + row.bottom - Scale(kKeyHeight)) / 2;
            for (size_t i = 0; i < keys.size(); i++)
            {
                RECT cap = { capLeft, capTop, capLeft + widths[i], capTop + Scale(kKeyHeight) };
                renderer.FillRoundedRect(ToRectF(cap), radius, Faded(theme::kChip, progress));
                renderer.DrawTextLine(keys[i], Font::kMono, cap, Faded(theme::kText1, progress), DWRITE_TEXT_ALIGNMENT_CENTER);
                capLeft = cap.right + Scale(kKeyGap);
            }
            RECT action = { row.left, row.top, row.right - total - Scale(kKeysToAction), row.bottom };
            renderer.DrawTextLine(entry.action, Font::kBody, action, Faded(theme::kText1, progress));
        }
    }

    int max = m_contentHeight + Scale(kMargin) - AreaHeight();
    if (max > 0)
    {
        int track = AreaHeight();
        int thumb = std::max(Scale(24), track * track / (m_contentHeight + Scale(kMargin)));
        int top = m_top + (track - thumb) * m_scroll / max;
        int right = m_bounds.right - Scale(4);
        renderer.FillRoundedRect({ static_cast<float>(right - Scale(4)), static_cast<float>(top),
            static_cast<float>(right), static_cast<float>(top + thumb) }, static_cast<float>(Scale(2)), theme::kScrollThumb);
    }
    renderer.PopClip();
}
