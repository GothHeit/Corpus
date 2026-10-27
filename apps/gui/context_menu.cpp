#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "context_menu.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <vector>

namespace
{
    const int kRow = 28;
    const int kPadding = 4;      // between the edge of the menu and its rows
    const int kTextInset = 12;   // text starts this far into a row
    const int kIconColumn = 20;  // room for the (i) before "Info"
    const int kSeparator = 9;    // the rule before the destructive item
    const int kMinWidth = 220;
    const int kRadius = 4;
    const int kItemRadius = 3;

    struct Entry
    {
        ContextMenu::Item item;
        const wchar_t *label;
        const wchar_t *hint;    // the shortcut, or nullptr
        bool ruleAbove;
    };

    const Entry kTagEntries[] = {
        { ContextMenu::Item::kRenameTag, strings::kMenuRename, strings::kMenuRenameKeys, false },
        { ContextMenu::Item::kDeleteTag, strings::kMenuDeleteTag, nullptr, false },
    };

    const Entry kGridEntries[] = {
        { ContextMenu::Item::kAddFolder, strings::kMenuAddFolder, strings::kMenuAddFolderKeys, false },
    };

    // The labels of the two orders depend on the key, and come from SetSortMarks.
    const Entry kSortEntries[] = {
        { ContextMenu::Item::kSortAdded,      strings::kSortAdded, nullptr, false },
        { ContextMenu::Item::kSortName,       strings::kSortName,  nullptr, false },
        { ContextMenu::Item::kSortSize,       strings::kSortSize,  nullptr, false },
        { ContextMenu::Item::kSortType,       strings::kSortType,  nullptr, false },
        { ContextMenu::Item::kSortAscending,  nullptr,             nullptr, true },
        { ContextMenu::Item::kSortDescending, nullptr,             nullptr, false },
    };

    const Entry kEntries[] = {
        { ContextMenu::Item::kOpen,           strings::kMenuOpen,           nullptr,                  false },
        { ContextMenu::Item::kShowInExplorer, strings::kMenuShowInExplorer, nullptr,                  false },
        { ContextMenu::Item::kCopy,           strings::kMenuCopy,           strings::kMenuCopyKeys,   false },
        { ContextMenu::Item::kAddTag,         strings::kMenuAddTag,         strings::kMenuAddTagKeys, true },
        { ContextMenu::Item::kInfo,           strings::kMenuInfo,           strings::kMenuInfoKeys,   false },
        { ContextMenu::Item::kRename,         strings::kMenuRename,         strings::kMenuRenameKeys, false },
        { ContextMenu::Item::kRemove,         strings::kMenuRemove,         strings::kMenuRemoveKeys, true },
        { ContextMenu::Item::kDelete,         strings::kMenuDelete,         strings::kMenuDeleteKeys, false },
    };

    template <size_t N> int Count(const Entry (&)[N]) { return static_cast<int>(N); }

    const Entry *EntriesOf(ContextMenu::Kind kind)
    {
        switch (kind)
        {
        case ContextMenu::Kind::kTag:  return kTagEntries;
        case ContextMenu::Kind::kGrid: return kGridEntries;
        case ContextMenu::Kind::kSort: return kSortEntries;
        case ContextMenu::Kind::kFile: break;
        }
        return kEntries;
    }

    int CountOf(ContextMenu::Kind kind)
    {
        switch (kind)
        {
        case ContextMenu::Kind::kTag:  return Count(kTagEntries);
        case ContextMenu::Kind::kGrid: return Count(kGridEntries);
        case ContextMenu::Kind::kSort: return Count(kSortEntries);
        case ContextMenu::Kind::kFile: break;
        }
        return Count(kEntries);
    }

    // Only what can't be taken back from inside Corpus is red; Remove from library leaves the file where it is.
    bool IsDestructive(ContextMenu::Item item)
    {
        return item == ContextMenu::Item::kDelete;
    }

}

int ContextMenu::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void ContextMenu::SetSortMarks(Item key, Item order, const wchar_t *ascending, const wchar_t *descending)
{
    m_sortKey = key;
    m_sortOrder = order;
    m_ascending = ascending;
    m_descending = descending;
}

void ContextMenu::Open(HWND hWnd, POINT pt, Kind kind, bool alignRight)
{
    m_kind = kind;
    m_hWnd = hWnd;
    m_open = true;
    m_pointer = pt;
    m_keyboardItem = -1;
    m_hover.Reset(m_hWnd, kMenuHoverTimer);
    m_width = Scale(kMinWidth);

    int height = 2 * Scale(kPadding) + CountOf(m_kind) * Scale(kRow);
    for (int n = 0; n < CountOf(m_kind); n++)
    {
        if (EntriesOf(m_kind)[n].ruleAbove)
            height += Scale(kSeparator);
    }

    // Opens down and right of the pointer, or up and left of it where that would leave the window.
    RECT client;
    GetClientRect(hWnd, &client);
    if (alignRight)
        pt.x = std::max(0L, pt.x - m_width);
    m_origin = pt;
    if (m_origin.x + m_width > client.right)
        m_origin.x = std::max(0L, pt.x - m_width);
    if (m_origin.y + height > client.bottom)
        m_origin.y = std::max(0L, pt.y - height);
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void ContextMenu::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_hover.Reset(m_hWnd, kMenuHoverTimer);
    RECT bounds = Bounds();
    InvalidateRect(m_hWnd, &bounds, FALSE);
}

RECT ContextMenu::Bounds() const
{
    int height = 2 * Scale(kPadding) + CountOf(m_kind) * Scale(kRow);
    for (int n = 0; n < CountOf(m_kind); n++)
    {
        if (EntriesOf(m_kind)[n].ruleAbove)
            height += Scale(kSeparator);
    }
    // A little more for the shadow edge.
    return { m_origin.x - 1, m_origin.y - 1, m_origin.x + m_width + 1, m_origin.y + height + 1 };
}

RECT ContextMenu::ItemRect(int index) const
{
    int top = m_origin.y + Scale(kPadding);
    for (int i = 0; i <= index; i++)
    {
        if (EntriesOf(m_kind)[i].ruleAbove)
            top += Scale(kSeparator);
        if (i < index)
            top += Scale(kRow);
    }
    return { m_origin.x + Scale(kPadding), top, m_origin.x + m_width - Scale(kPadding), top + Scale(kRow) };
}

int ContextMenu::ItemAt(POINT pt) const
{
    for (int i = 0; i < CountOf(m_kind); i++)
    {
        RECT r = ItemRect(i);
        if (PtInRect(&r, pt))
            return i;
    }
    return -1;
}

void ContextMenu::OnHover(POINT pt)
{
    if (!m_open)
        return;
    if (pt.x != m_pointer.x || pt.y != m_pointer.y)
        m_keyboardItem = -1;
    m_pointer = pt;
    if (m_hover.Set(m_hWnd, kMenuHoverTimer, ItemAt(pt)))
    {
        RECT bounds = Bounds();
        InvalidateRect(m_hWnd, &bounds, FALSE);
    }
}

void ContextMenu::OnTimer()
{
    m_hover.Step(m_hWnd, kMenuHoverTimer);
    RECT bounds = Bounds();
    InvalidateRect(m_hWnd, &bounds, FALSE);
}

ContextMenu::Item ContextMenu::OnLButtonDown(POINT pt)
{
    int index = ItemAt(pt);
    Item item = index >= 0 ? EntriesOf(m_kind)[index].item : Item::kNone;
    Close();
    return item;
}

ContextMenu::Item ContextMenu::OnKeyDown(WPARAM key)
{
    if (key == VK_ESCAPE)
        Close();
    else if (key == VK_DOWN || key == VK_UP)
    {
        int step = key == VK_DOWN ? 1 : -1;
        m_keyboardItem = m_keyboardItem < 0 ? (step > 0 ? 0 : CountOf(m_kind) - 1) : (m_keyboardItem + step + CountOf(m_kind)) % CountOf(m_kind);
        RECT bounds = Bounds();
        InvalidateRect(m_hWnd, &bounds, FALSE);
    }
    else if (key == VK_RETURN && m_keyboardItem >= 0)
    {
        Item item = EntriesOf(m_kind)[m_keyboardItem].item;
        Close();
        return item;
    }
    return Item::kNone;
}

void ContextMenu::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    RECT bounds = Bounds();
    D2D1_RECT_F body = ToRectF({ bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1 });
    float radius = static_cast<float>(Scale(kRadius));
    // A soft shadow: two faint plates offset downward, under the menu.
    renderer.FillRoundedRect({ body.left - 1, body.top + 2, body.right + 1, body.bottom + 5 }, radius + 2, theme::Rgb(0x000000, 0.18f));
    renderer.FillRoundedRect({ body.left, body.top + 1, body.right, body.bottom + 2 }, radius + 1, theme::Rgb(0x000000, 0.22f));
    renderer.FillRoundedRect(body, radius, theme::kControl);
    renderer.DrawRoundedRect(body, radius, theme::White(0.10f), 1.0f);

    for (int i = 0; i < CountOf(m_kind); i++)
    {
        const Entry &e = EntriesOf(m_kind)[i];
        RECT row = ItemRect(i);

        if (e.ruleAbove)
        {
            float y = static_cast<float>(row.top - Scale(kSeparator) / 2);
            renderer.DrawLine({ static_cast<float>(row.left + Scale(kTextInset) / 2), y },
                { static_cast<float>(row.right - Scale(kTextInset) / 2), y }, theme::kStroke, 1.0f);
        }

        // The row under the pointer fades in with the hover; the one picked with the arrow keys is lit at once.
        float lit = m_hover.Level(i);
        if (i == m_keyboardItem)
            lit = 1.0f;
        if (lit > 0.0f)
        {
            D2D1_COLOR_F fill = Faded(theme::kYuri, lit);
            renderer.FillRoundedRect(ToRectF(row), static_cast<float>(Scale(kItemRadius)), fill);
        }

        bool on = lit > 0.5f;
        D2D1_COLOR_F lit1 = IsDestructive(e.item) ? theme::kRose : theme::kOnAccent;
        D2D1_COLOR_F text = on ? lit1 : (IsDestructive(e.item) ? theme::kRose : theme::kText1);
        D2D1_COLOR_F dim = on ? lit1 : theme::kText3;

        RECT label = { row.left + Scale(kTextInset), row.top, row.right - Scale(kTextInset), row.bottom };
        const wchar_t *name = e.item == Item::kSortAscending ? m_ascending : e.item == Item::kSortDescending ? m_descending : e.label;
        if (e.item == m_sortKey || e.item == m_sortOrder)
        {
            // The tick of the key and the order in use.
            float u = Scale(4) / 4.0f;
            float x = static_cast<float>(label.left), cy = (row.top + row.bottom) / 2.0f;
            const D2D1_POINT_2F tick[] = { { x + 2.0f * u, cy }, { x + 5.0f * u, cy + 3.0f * u }, { x + 11.0f * u, cy - 3.5f * u } };
            renderer.DrawPolyline(tick, 3, text, 1.5f * u);
            label.left += Scale(kIconColumn);
        }
        else if (e.item == Item::kInfo)
        {
            // The (i): a ring with a dot over a short stem.
            float size = static_cast<float>(Scale(14));
            float cx = label.left + size / 2.0f;
            float cy = (row.top + row.bottom) / 2.0f;
            renderer.DrawCircle({ cx, cy }, size / 2.0f - 0.5f, dim, 1.2f);
            renderer.FillCircle({ cx, cy - size * 0.2f }, std::max(1.0f, size * 0.07f), dim);
            renderer.DrawLine({ cx, cy - size * 0.02f }, { cx, cy + size * 0.24f }, dim, 1.4f);
            label.left += Scale(kIconColumn);
        }
        else
            label.left += Scale(kIconColumn);  // the other labels line up with the one after the icon
        if (name)
            renderer.DrawTextLine(name, Font::kBody, label, text);
        if (e.hint)
            renderer.DrawTextLine(e.hint, Font::kCaption, label, dim, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}
