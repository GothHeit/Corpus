#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tag_sidebar.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/tag.hpp"
#include "../../include/text.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    const int kInset = 12;          // the field and the row highlights, from the sides of the sidebar
    const int kTitleHeight = 28;    // the "Tags" title above the field
    const int kFieldGap = 8;        // between the field and the list
    const int kRowPadding = 8;      // inside a row highlight, before the marker and after the count
    const int kMarkerSize = 7;      // the square or dash before a name
    const int kMarkerGap = 10;      // between the marker and the name
    const int kCountGap = 8;        // between the name and the count
    const int kWheelRows = 3;       // rows scrolled per wheel notch
    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 4;
    const int kScrollbarMinThumb = 24;

    bool LessNoCase(const std::wstring &a, const std::wstring &b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    }

    // Off, then required, then excluded, then off again.
    TagState NextState(TagState state)
    {
        switch (state)
        {
        case TagState::kOff:      return TagState::kRequired;
        case TagState::kRequired: return TagState::kExcluded;
        default:                  return TagState::kOff;
        }
    }
}

TagSidebar::TagSidebar(const library &lib)
    : m_lib(lib), m_field(strings::kTagFilterPlaceholder)
{
    m_field.SetSearchIcon(true);
}

int TagSidebar::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void TagSidebar::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void TagSidebar::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    if (!Visible())
        m_field.Blur();
    m_field.SetBounds(hWnd, renderer, FieldRect());
    ScrollTo(m_scrollY);
    OnHover(m_hoverPoint);
}

// ---- Tags ----

void TagSidebar::Update(const std::wstring &query, const std::vector<file*> &results)
{
    std::unordered_map<const tag*, size_t> counts;
    for (const file *f : results)
    {
        for (const tag *t : f->get_tags())
            counts[t]++;
    }
    std::unordered_map<std::string, TagState> states = TagStates(WideToUtf8(query));

    std::vector<Row> rows;
    rows.reserve(m_lib.get_tags().size());
    for (const tag *t : m_lib.get_tags())
    {
        auto count = counts.find(t);
        auto state = states.find(t->get_folded_id());
        rows.push_back({ t->get_id(), Utf8ToWide(t->get_id()), t->get_folded_id(),
            state == states.end() ? TagState::kOff : state->second, count == counts.end() ? 0 : count->second });
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return LessNoCase(a.label, b.label); });

    // The hover fades go by position in the list, which now holds other tags, unless the tags are the same.
    bool same = rows.size() == m_rows.size() && std::equal(rows.begin(), rows.end(), m_rows.begin(),
        [](const Row &a, const Row &b) { return a.name == b.name; });
    m_rows = std::move(rows);
    if (!same)
    {
        m_hover.Reset(m_hWnd, kSidebarHoverTimer);
        Filter();
    }
    Invalidate();
}

// Keeps the tags whose name contains the text of the field, ignoring case like the search.
void TagSidebar::Filter()
{
    std::string text = fold_case(WideToUtf8(m_field.Query()));
    m_visible.clear();
    for (size_t i = 0; i < m_rows.size(); i++)
    {
        if (m_rows[i].folded.find(text) != std::string::npos)
            m_visible.push_back(i);
    }
    ScrollTo(m_scrollY);
    OnHover(m_hoverPoint);
}

// ---- Layout ----

RECT TagSidebar::FieldRect() const
{
    int top = m_bounds.top + Scale(kTitleHeight);
    RECT r = { m_bounds.left + Scale(kInset), top, m_bounds.right - Scale(kInset), top + Scale(theme::kControlHeight) };
    if (r.right < r.left)
        r.right = r.left;
    return r;
}

RECT TagSidebar::ListRect() const
{
    RECT r = { m_bounds.left, FieldRect().bottom + Scale(kFieldGap), m_bounds.right, m_bounds.bottom };
    if (r.bottom < r.top)
        r.bottom = r.top;
    return r;
}

// The highlight of the index-th visible row, scrolled.
RECT TagSidebar::RowRect(size_t index) const
{
    RECT list = ListRect();
    int height = Scale(theme::kSidebarRow);
    int top = list.top + static_cast<int>(index) * height - m_scrollY;
    RECT r = { list.left + Scale(kInset), top, list.right - Scale(kInset), top + height };
    return r;
}

// The visible row under the point, or -1.
int TagSidebar::RowAt(POINT pt) const
{
    RECT list = ListRect();
    if (!Visible() || !PtInRect(&list, pt))
        return -1;
    int index = (pt.y - list.top + m_scrollY) / Scale(theme::kSidebarRow);
    if (index < 0 || static_cast<size_t>(index) >= m_visible.size())
        return -1;
    RECT r = RowRect(static_cast<size_t>(index));
    return PtInRect(&r, pt) ? index : -1;
}

int TagSidebar::MaxScroll() const
{
    RECT list = ListRect();
    int content = static_cast<int>(m_visible.size()) * Scale(theme::kSidebarRow);
    int max = content - (list.bottom - list.top);
    return max > 0 ? max : 0;
}

void TagSidebar::ScrollTo(int y)
{
    int max = MaxScroll();
    if (y > max)
        y = max;
    if (y < 0)
        y = 0;
    if (y != m_scrollY)
    {
        m_scrollY = y;
        OnHover(m_hoverPoint);
        Invalidate();
    }
}

void TagSidebar::OnHover(POINT pt)
{
    m_hoverPoint = pt;
    if (m_hover.Set(m_hWnd, kSidebarHoverTimer, RowAt(pt)))
        Invalidate();
}

void TagSidebar::OnTimer()
{
    m_hover.Step(m_hWnd, kSidebarHoverTimer);
    Invalidate();
}

// ---- Input ----

bool TagSidebar::OnLButtonDown(POINT pt, std::wstring &query)
{
    if (!Visible() || !Contains(pt))
        return false;

    if (m_field.Contains(pt))
    {
        m_field.OnLButtonDown(pt);
        return true;
    }

    // A click on a tag leaves the field as it is, so the filter can be typed on after it.
    int index = RowAt(pt);
    if (index >= 0)
    {
        const Row &row = m_rows[m_visible[index]];
        // A tag the search can't name is dimmed, and a click on it does nothing.
        if (!CanFilterBy(row.name))
            return true;
        query = Utf8ToWide(WithTagState(WideToUtf8(query), row.name, NextState(row.state)));
    }
    return true;
}

// The list now holds other tags: it starts again from the top.
void TagSidebar::FilterChanged()
{
    m_scrollY = 0;
    m_hover.Reset(m_hWnd, kSidebarHoverTimer);
    Filter();
    Invalidate();
}

void TagSidebar::OnChar(wchar_t ch)
{
    if (m_field.OnChar(ch))
        FilterChanged();
}

void TagSidebar::OnKeyDown(WPARAM key, bool ctrl)
{
    // Esc empties the field first, then leaves it.
    if (key == VK_ESCAPE)
    {
        if (m_field.Query().empty())
            Blur();
        else
        {
            m_field.SetText(std::wstring());
            FilterChanged();
        }
        return;
    }
    if (m_field.OnKeyDown(key, ctrl))
        FilterChanged();
}

bool TagSidebar::OnMouseWheel(POINT pt, int delta)
{
    if (!Visible() || !Contains(pt))
        return false;
    ScrollTo(m_scrollY - MulDiv(delta, kWheelRows * Scale(theme::kSidebarRow), WHEEL_DELTA));
    return true;
}

// ---- Painting ----

void TagSidebar::Paint(Renderer &renderer) const
{
    if (!Visible())
        return;

    float line = Scale(4) / 4.0f;
    renderer.FillRect(ToRectF(m_bounds), theme::kBase);
    renderer.FillRect(D2D1::RectF(m_bounds.right - line, static_cast<float>(m_bounds.top),
        static_cast<float>(m_bounds.right), static_cast<float>(m_bounds.bottom)), theme::kStroke);

    RECT title = { m_bounds.left + Scale(kInset) + Scale(kRowPadding), m_bounds.top,
                   m_bounds.right - Scale(kInset), m_bounds.top + Scale(kTitleHeight) };
    renderer.DrawTextLine(strings::kTagsTitle, Font::kCaptionStrong, title, theme::kText3);

    m_field.Paint(renderer);

    RECT list = ListRect();
    if (list.bottom <= list.top)
        return;

    if (m_visible.empty())
    {
        RECT r = RowRect(0);
        r.left += Scale(kRowPadding);
        renderer.DrawTextLine(m_rows.empty() ? strings::kNoTags : strings::kNoTagMatches, Font::kCaption, r, theme::kText3);
        return;
    }

    renderer.PushClip(list);
    int height = Scale(theme::kSidebarRow);
    size_t first = static_cast<size_t>(m_scrollY / height);
    size_t last = static_cast<size_t>((m_scrollY + list.bottom - list.top) / height) + 1;
    for (size_t i = first; i < m_visible.size() && i <= last; i++)
        PaintRow(renderer, m_rows[m_visible[i]], RowRect(i), m_hover.Level(static_cast<int>(i)));
    renderer.PopClip();

    PaintScrollbar(renderer);
}

void TagSidebar::PaintRow(Renderer &renderer, const Row &row, const RECT &rect, float hover) const
{
    float line = Scale(4) / 4.0f;
    if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(rect), static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kHover, hover));

    // A tag none of the results have is dimmed; required and excluded ones keep their colour.
    bool dim = row.state == TagState::kOff && (row.count == 0 || !CanFilterBy(row.name));
    float size = static_cast<float>(Scale(kMarkerSize));
    float x = static_cast<float>(rect.left + Scale(kRowPadding));
    float cy = (rect.top + rect.bottom) / 2.0f;
    D2D1_RECT_F marker = D2D1::RectF(x, cy - size / 2, x + size, cy + size / 2);

    D2D1_COLOR_F color;
    if (row.state == TagState::kRequired)
    {
        color = theme::kYuri;
        renderer.FillRoundedRect(marker, 1.5f * line, color);
    }
    else if (row.state == TagState::kExcluded)
    {
        color = theme::kRose;
        renderer.DrawLine(D2D1::Point2F(marker.left, cy), D2D1::Point2F(marker.right, cy), color, 1.5f * line);
    }
    else
    {
        color = dim ? theme::kText3 : theme::kText1;
        D2D1_RECT_F hollow = D2D1::RectF(marker.left + line / 2, marker.top + line / 2, marker.right - line / 2, marker.bottom - line / 2);
        renderer.DrawRoundedRect(hollow, 1.5f * line, dim ? Faded(theme::kText3, 0.6f) : theme::kText3, line);
    }

    // The count, except on an excluded tag, which no result has by definition.
    int right = rect.right - Scale(kRowPadding);
    if (row.state != TagState::kExcluded)
    {
        std::wstring count = FormatCount(row.count);
        int width = static_cast<int>(renderer.TextWidth(count, Font::kMono) + 0.99f);
        RECT countRect = { right - width, rect.top, right, rect.bottom };
        renderer.DrawTextLine(count, Font::kMono, countRect, dim ? Faded(theme::kText3, 0.6f) : theme::kText3,
            DWRITE_TEXT_ALIGNMENT_TRAILING);
        right -= width + Scale(kCountGap);
    }

    RECT name = { static_cast<LONG>(marker.right) + Scale(kMarkerGap), rect.top, right, rect.bottom };
    if (name.right <= name.left)
        return;
    renderer.DrawTextLine(row.label, Font::kCaption, name, color);
    if (row.state == TagState::kExcluded)
    {
        float width = std::min(renderer.TextWidth(row.label, Font::kCaption), static_cast<float>(name.right - name.left));
        float y = std::floor(cy) + line / 2;
        renderer.DrawLine(D2D1::Point2F(static_cast<float>(name.left), y), D2D1::Point2F(name.left + width, y), color, line);
    }
}

// A thin thumb along the right edge while the list is longer than the sidebar. The wheel scrolls it.
void TagSidebar::PaintScrollbar(Renderer &renderer) const
{
    int max = MaxScroll();
    if (max <= 0)
        return;

    RECT list = ListRect();
    int trackHeight = list.bottom - list.top - Scale(kScrollbarEdgeGap);
    int content = static_cast<int>(m_visible.size()) * Scale(theme::kSidebarRow);
    int thumbHeight = MulDiv(trackHeight, list.bottom - list.top, content);
    if (thumbHeight < Scale(kScrollbarMinThumb))
        thumbHeight = Scale(kScrollbarMinThumb);
    if (thumbHeight > trackHeight)
        return;

    int right = m_bounds.right - Scale(kScrollbarEdgeGap);
    int top = list.top + MulDiv(trackHeight - thumbHeight, m_scrollY, max);
    RECT thumb = { right - Scale(kScrollbarWidth), top, right, top + thumbHeight };
    renderer.FillRoundedRect(ToRectF(thumb), static_cast<float>(Scale(theme::kControlRadius)), theme::kScrollThumb);
}
