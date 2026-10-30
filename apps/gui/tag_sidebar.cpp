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
#include "file_kind.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/tag.hpp"
#include "../../include/text.hpp"
#include <algorithm>
#include <cmath>
#include <shlwapi.h>
#include <unordered_map>

namespace
{
    const int kInset = 12;          // the field and the row highlights, from the sides of the sidebar
    const int kTitleHeight = 28;    // the "Library" and "Tags" titles
    const int kSectionGap = 12;     // between the Library views and the "Tags" title
    const int kIconSize = 12;       // the icon before a Library view; its name lines up with the tag names
    const int kFieldGap = 8;        // between the field and the list
    const int kRowPadding = 8;      // inside a row highlight, before the marker and after the count
    const int kMarkerSize = 7;      // the square or dash before a name
    const int kMarkerGap = 10;      // between the marker and the name
    const int kCountGap = 8;        // between the name and the count
    const int kWheelRows = 3;       // rows scrolled per wheel notch
    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 4;
    const int kScrollbarMinThumb = 24;
    const int kFootPadding = 8;     // under the Settings row
    const int kIndent = 12;         // per level of the tag tree
    const int kArrowSlot = 12;      // the disclosure arrow's room, in front of the marker once a tag has others under it
    const int kArrowSize = 6;
    const int kFlatRowHeight = 40;  // a row of the filtered list: the name and, under it, the branch it is in
    const int kSortHeight = 20;     // the Name and Count switch
    const int kSortPadding = 8;     // inside one of its segments

    const wchar_t *const kViewNames[] = { strings::kAllFiles, strings::kPictures, strings::kVideos, strings::kAudio,
        strings::kBooks, strings::kUntagged, strings::kMissing };

    // The part of a path after its last '/'.
    std::string LastPart(const std::string &path)
    {
        size_t slash = path.rfind(tag_path_separator);
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // The words a typed filter is made of: spaces and '_' both separate, so "disc 2" finds Disc_2.
    std::vector<std::string> Words(const std::string &folded)
    {
        std::vector<std::string> words;
        std::string word;
        for (char c : folded)
        {
            if (c == ' ' || c == '_')
            {
                if (!word.empty())
                    words.push_back(std::move(word));
                word.clear();
            }
            else
                word += c;
        }
        if (!word.empty())
            words.push_back(std::move(word));
        return words;
    }

    bool HasWords(const std::string &text, const std::vector<std::string> &words)
    {
        return std::all_of(words.begin(), words.end(), [&](const std::string &w) { return text.find(w) != std::string::npos; });
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
    ViewsChanged();
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

// Builds the tree of tag paths, counting for each node the result files that have it or anything under it, each once.
void TagSidebar::Update(const std::wstring &query, const std::vector<file*> &results)
{
    std::unordered_map<std::string, TagState> states = TagStates(WideToUtf8(query));

    // Shorter names first, so a parent that is a tag spells itself and a missing one takes its first child's spelling.
    std::vector<const tag*> tags(m_lib.get_tags().begin(), m_lib.get_tags().end());
    std::stable_sort(tags.begin(), tags.end(), [](const tag *a, const tag *b) { return a->get_id().size() < b->get_id().size(); });

    std::vector<Node> nodes;
    nodes.reserve(tags.size());
    std::unordered_map<std::string, int> byPath;
    std::unordered_map<const tag*, int> ofTag;
    for (const tag *t : tags)
    {
        const std::string &id = t->get_id();
        int parent = -1;
        size_t end = 0;
        for (;;)
        {
            end = id.find(tag_path_separator, end);
            std::string path = id.substr(0, end);
            std::string folded = fold_case(path);
            auto found = byPath.find(folded);
            int index;
            if (found != byPath.end())
                index = found->second;
            else
            {
                index = static_cast<int>(nodes.size());
                Node node;
                node.path = path;
                node.folded = folded;
                std::string last = LastPart(path);
                node.label = Utf8ToWide(last.empty() ? path : last);
                node.full = Utf8ToWide(path);
                node.parent = parent;
                node.depth = parent < 0 ? 0 : nodes[parent].depth + 1;
                if (parent >= 0)
                {
                    node.parentLabel = nodes[parent].label;
                    nodes[parent].kids.push_back(index);
                }
                node.searchable = folded;
                std::replace(node.searchable.begin(), node.searchable.end(), '_', ' ');
                auto state = states.find(folded);
                node.state = state == states.end() ? TagState::kOff : state->second;
                nodes.push_back(std::move(node));
                byPath.emplace(folded, index);
            }
            parent = index;
            if (end == std::string::npos)
                break;
            end++;
        }
        ofTag[t] = parent;
    }

    std::vector<size_t> stamp(nodes.size(), 0);
    size_t seen = 0;
    for (const file *f : results)
    {
        seen++;
        for (const tag *t : f->get_tags())
        {
            auto node = ofTag.find(t);
            for (int n = node == ofTag.end() ? -1 : node->second; n >= 0 && stamp[n] != seen; n = nodes[n].parent)
            {
                stamp[n] = seen;
                nodes[n].count++;
            }
        }
    }

    // With a tag toggled, the tags that make sense to pick next (the results use them, or they are toggled, or hold a
    // toggled one) come first among their siblings and the rest sink: they stay, dimmed, where a drop can still reach them.
    m_filtering = std::any_of(nodes.begin(), nodes.end(), [](const Node &n) { return n.state != TagState::kOff; });
    for (Node &node : nodes)
        node.relevant = !m_filtering || node.count > 0 || node.state != TagState::kOff;
    for (const Node &node : nodes)
    {
        if (node.state == TagState::kOff)
            continue;
        for (int n = node.parent; n >= 0; n = nodes[n].parent)
            nodes[n].relevant = true;
    }

    // A tag that was just asked for in the search shows itself: the branches above it open.
    std::unordered_set<std::string> marked;
    for (const Node &node : nodes)
    {
        if (node.state == TagState::kOff)
            continue;
        marked.insert(node.folded);
        if (m_marked.count(node.folded))
            continue;
        for (int n = node.parent; n >= 0; n = nodes[n].parent)
            m_open.insert(nodes[n].folded);
    }
    m_marked = std::move(marked);

    // The hover fades go by position in the list, which now holds other tags, unless the tags are the same.
    bool same = nodes.size() == m_nodes.size() && std::equal(nodes.begin(), nodes.end(), m_nodes.begin(),
        [](const Node &a, const Node &b) { return a.path == b.path; });
    m_nodes = std::move(nodes);
    m_branches = std::any_of(m_nodes.begin(), m_nodes.end(), [](const Node &n) { return !n.kids.empty(); });
    if (!same)
        m_hover.Reset(m_hWnd, kSidebarHoverTimer);
    Filter();
    Invalidate();
}

void TagSidebar::CountViews(std::optional<size_t> missing)
{
    std::fill(std::begin(m_viewCounts), std::end(m_viewCounts), 0);
    for (const file *f : m_lib.get_files())
    {
        if (f->get_tags().empty())
            m_viewCounts[static_cast<int>(SidebarView::kUntagged)]++;
        switch (KindOf(f->get_path()))
        {
        case FileKind::kPicture: m_viewCounts[static_cast<int>(SidebarView::kPictures)]++; break;
        case FileKind::kVideo:   m_viewCounts[static_cast<int>(SidebarView::kVideos)]++; break;
        case FileKind::kAudio:   m_viewCounts[static_cast<int>(SidebarView::kAudio)]++; break;
        case FileKind::kBook:    m_viewCounts[static_cast<int>(SidebarView::kBooks)]++; break;
        default:                 break;
        }
    }
    m_viewCounts[static_cast<int>(SidebarView::kAll)] = m_lib.get_files().size();
    m_viewCounts[static_cast<int>(SidebarView::kMissing)] = missing.value_or(0);
    m_missingKnown = missing.has_value();
    ViewsChanged();
}

// Which views have a row: Missing only while some files are, or while it is the one shown (the window then falls
// back to All files, see Window::CountViews).
void TagSidebar::ViewsChanged()
{
    m_shownViews.clear();
    for (int i = 0; i < kViewCount; i++)
    {
        SidebarView view = kViews[i];
        if (view != SidebarView::kMissing || (m_missingKnown && m_viewCounts[i] > 0) || m_view == view)
            m_shownViews.push_back(i);
    }
    m_viewHover.Reset(m_hWnd, kSidebarViewHoverTimer);
    if (m_hWnd)
    {
        SetBounds(m_hWnd, *m_renderer, m_bounds);
        OnHover(m_hoverPoint);
    }
    Invalidate();
}

void TagSidebar::SetView(SidebarView view)
{
    m_view = view;
    ViewsChanged();
}

void TagSidebar::SetActive(bool active)
{
    m_active = active;
    Invalidate();
}

// Lists the nodes with a row: the tree with its open branches in the order picked, or, with text in the field, the
// tags whose name has every word of it (else whose whole path has them) as a flat list.
void TagSidebar::Filter()
{
    std::vector<std::string> words = Words(fold_case(WideToUtf8(m_field.Query())));
    m_visible.clear();
    m_flat = !words.empty();
    if (m_flat)
    {
        for (size_t i = 0; i < m_nodes.size(); i++)
        {
            std::string name = fold_case(LastPart(m_nodes[i].path));
            std::replace(name.begin(), name.end(), '_', ' ');
            if (HasWords(name, words))
                m_visible.push_back(i);
        }
        if (m_visible.empty())
        {
            for (size_t i = 0; i < m_nodes.size(); i++)
            {
                if (HasWords(m_nodes[i].searchable, words))
                    m_visible.push_back(i);
            }
        }
        std::sort(m_visible.begin(), m_visible.end(), [this](size_t a, size_t b)
        {
            if (m_nodes[a].relevant != m_nodes[b].relevant)
                return m_nodes[a].relevant;
            int c = StrCmpLogicalW(m_nodes[a].label.c_str(), m_nodes[b].label.c_str());
            return c != 0 ? c < 0 : StrCmpLogicalW(m_nodes[a].full.c_str(), m_nodes[b].full.c_str()) < 0;
        });
    }
    else
    {
        std::vector<int> tops;
        for (size_t i = 0; i < m_nodes.size(); i++)
        {
            if (m_nodes[i].parent < 0)
                tops.push_back(static_cast<int>(i));
        }
        SortNodes(tops);
        for (int top : tops)
            Arrange(m_visible, top);
    }
    ScrollTo(m_scrollY);
    OnHover(m_hoverPoint);
}

// Name order puts numbers in their natural order (Disc_2 before Disc_10); Count puts the busiest first.
void TagSidebar::SortNodes(std::vector<int> &nodes) const
{
    std::sort(nodes.begin(), nodes.end(), [this](int a, int b)
    {
        if (m_nodes[a].relevant != m_nodes[b].relevant)
            return m_nodes[a].relevant;
        if (m_sort == Sort::kCount && m_nodes[a].count != m_nodes[b].count)
            return m_nodes[a].count > m_nodes[b].count;
        int c = StrCmpLogicalW(m_nodes[a].label.c_str(), m_nodes[b].label.c_str());
        return c != 0 ? c < 0 : a < b;
    });
}

// Adds the node and, if it is open, what is under it.
void TagSidebar::Arrange(std::vector<size_t> &out, int node) const
{
    out.push_back(static_cast<size_t>(node));
    const Node &n = m_nodes[node];
    if (n.kids.empty() || !m_open.count(n.folded))
        return;
    std::vector<int> kids = n.kids;
    SortNodes(kids);
    for (int kid : kids)
        Arrange(out, kid);
}

// ---- Layout ----

// The highlight of the index-th Library view, under the "Library" title.
RECT TagSidebar::ViewRect(int index) const
{
    int height = Scale(theme::kSidebarRow);
    int top = m_bounds.top + Scale(kTitleHeight) + index * height;
    RECT r = { m_bounds.left + Scale(kInset), top, m_bounds.right - Scale(kInset), top + height };
    return r;
}

// The Library view under the point, or -1.
int TagSidebar::ViewAt(POINT pt) const
{
    if (!Visible() || pt.y >= m_bounds.bottom)
        return -1;
    for (int i = 0; i < ShownViewCount(); i++)
    {
        RECT r = ViewRect(i);
        if (PtInRect(&r, pt))
            return i;
    }
    return -1;
}

// Where the "Tags" title starts, under the Library views.
int TagSidebar::TagsTop() const
{
    return ViewRect(ShownViewCount() - 1).bottom + Scale(kSectionGap);
}

RECT TagSidebar::FieldRect() const
{
    int top = TagsTop() + Scale(kTitleHeight);
    RECT r = { m_bounds.left + Scale(kInset), top, m_bounds.right - Scale(kInset), top + Scale(theme::kControlHeight) };
    if (r.right < r.left)
        r.right = r.left;
    return r;
}

// Where the Settings button goes at the foot of the sidebar: a square with just the cog.
RECT TagSidebar::SettingsSlot() const
{
    int size = Scale(theme::kSidebarRow);
    int bottom = m_bounds.bottom - Scale(kFootPadding);
    RECT r = { m_bounds.left + Scale(kInset), bottom - size, m_bounds.left + Scale(kInset) + size, bottom };
    return r;
}

// The button, or an empty rectangle in a window so short that it would sit over the Library views.
RECT TagSidebar::SettingsRect() const
{
    RECT r = SettingsSlot();
    if (r.top < ViewRect(ShownViewCount() - 1).bottom)
        return RECT{};
    return r;
}

RECT TagSidebar::ListRect() const
{
    RECT r = { m_bounds.left, FieldRect().bottom + Scale(kFieldGap), m_bounds.right, SettingsSlot().top - Scale(kFieldGap) };
    if (r.bottom < r.top)
        r.bottom = r.top;
    return r;
}

// A row of the tree is a tag's height; one of the filtered list has the branch under the name.
int TagSidebar::RowHeight() const
{
    return Scale(m_flat ? kFlatRowHeight : theme::kSidebarRow);
}

// The highlight of the index-th visible row, scrolled.
RECT TagSidebar::RowRect(size_t index) const
{
    RECT list = ListRect();
    int height = RowHeight();
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
    int index = (pt.y - list.top + m_scrollY) / RowHeight();
    if (index < 0 || static_cast<size_t>(index) >= m_visible.size())
        return -1;
    RECT r = RowRect(static_cast<size_t>(index));
    return PtInRect(&r, pt) ? index : -1;
}

// Where the parts of a row go: from the left an indent per level, the arrow (once any tag has others under it), the
// marker and the name; the count at the right edge.
TagSidebar::RowBox TagSidebar::Box(const Node &node, const RECT &rect) const
{
    RowBox box = {};
    int x = rect.left + Scale(kRowPadding) + (m_flat ? 0 : node.depth * Scale(kIndent));
    int arrow = m_branches && !m_flat ? Scale(kArrowSlot) : 0;
    box.arrow = { x - Scale(kRowPadding) / 2, rect.top, x + arrow, rect.bottom };
    if (m_flat || node.kids.empty())
        box.arrow = {};
    box.marker = static_cast<float>(x + arrow);

    int right = rect.right - Scale(kRowPadding);
    box.count = { right, rect.top, right, rect.bottom };
    if (node.state != TagState::kExcluded)
    {
        int width = static_cast<int>(m_renderer->TextWidth(FormatCount(node.count), Font::kMono) + 0.99f);
        box.count.left = right - width;
        right -= width + Scale(kCountGap);
    }
    int left = static_cast<int>(box.marker) + Scale(kMarkerSize) + Scale(kMarkerGap);
    box.name = { left, rect.top, std::max<int>(left, right), rect.bottom };
    return box;
}

// Whether the name is cut short by the room it has.
bool TagSidebar::Truncated(const Node &node, const RECT &rect) const
{
    RowBox box = Box(node, rect);
    return m_renderer->TextWidth(node.label, Font::kCaption) > static_cast<float>(box.name.right - box.name.left);
}

int TagSidebar::MaxScroll() const
{
    RECT list = ListRect();
    int content = static_cast<int>(m_visible.size()) * RowHeight();
    int max = content - (list.bottom - list.top);
    return max > 0 ? max : 0;
}

// The Name or Count button of the switch at the right end of the "Tags" title.
RECT TagSidebar::SortRect(Sort sort) const
{
    int top = TagsTop() + (Scale(kTitleHeight) - Scale(kSortHeight)) / 2;
    int nameWidth = static_cast<int>(m_renderer->TextWidth(strings::kSortByName, Font::kCaption) + 0.99f) + 2 * Scale(kSortPadding);
    int countWidth = static_cast<int>(m_renderer->TextWidth(strings::kSortByCount, Font::kCaption) + 0.99f) + 2 * Scale(kSortPadding);
    int right = m_bounds.right - Scale(kInset);
    if (sort == Sort::kCount)
        return { right - countWidth, top, right, top + Scale(kSortHeight) };
    return { right - countWidth - nameWidth, top, right - countWidth, top + Scale(kSortHeight) };
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
    bool changed = m_hover.Set(m_hWnd, kSidebarHoverTimer, m_renderer ? RowAt(pt) : -1);
    if (m_viewHover.Set(m_hWnd, kSidebarViewHoverTimer, m_renderer ? ViewAt(pt) : -1))
        changed = true;
    RECT settings = SettingsRect();
    if (m_settingsHover.Set(m_hWnd, kSidebarSettingsHoverTimer, Visible() && PtInRect(&settings, pt) ? 0 : -1))
        changed = true;
    int sort = -1;
    if (Visible() && m_renderer)
    {
        for (int i = 0; i < 2; i++)
        {
            RECT r = SortRect(static_cast<Sort>(i));
            if (PtInRect(&r, pt))
                sort = i;
        }
    }
    if (m_sortHover.Set(m_hWnd, kSidebarSortHoverTimer, sort))
        changed = true;
    if (changed)
        Invalidate();
}

// The settings cog's label; over a tag whose name is cut short, or in the filtered list, its whole path.
bool TagSidebar::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    anchor = SettingsRect();
    text = strings::kTipSettings;
    if (Visible() && PtInRect(&anchor, pt))
        return true;
    int index = RowAt(pt);
    if (index < 0)
        return false;
    const Node &node = m_nodes[m_visible[static_cast<size_t>(index)]];
    anchor = RowRect(static_cast<size_t>(index));
    if (!m_flat && !Truncated(node, anchor))
        return false;
    m_tipText = node.full;
    text = m_tipText.c_str();
    return true;
}

void TagSidebar::OnTimer(UINT_PTR timer)
{
    if (timer == kSidebarViewHoverTimer)
        m_viewHover.Step(m_hWnd, kSidebarViewHoverTimer);
    else if (timer == kSidebarSettingsHoverTimer)
        m_settingsHover.Step(m_hWnd, kSidebarSettingsHoverTimer);
    else if (timer == kSidebarSortHoverTimer)
        m_sortHover.Step(m_hWnd, kSidebarSortHoverTimer);
    else
        m_hover.Step(m_hWnd, kSidebarHoverTimer);
    Invalidate();
}

// ---- Input ----

bool TagSidebar::OnLButtonDown(POINT pt, std::wstring &query, std::optional<SidebarView> &view, bool &openSettings)
{
    if (!Visible() || !Contains(pt))
        return false;

    RECT settings = SettingsRect();
    if (PtInRect(&settings, pt))
    {
        openSettings = true;
        return true;
    }

    int viewIndex = ViewAt(pt);
    if (viewIndex >= 0)
    {
        view = kViews[ViewIndex(viewIndex)];
        return true;
    }

    for (int i = 0; i < 2; i++)
    {
        RECT r = SortRect(static_cast<Sort>(i));
        if (PtInRect(&r, pt))
        {
            if (m_sort != static_cast<Sort>(i))
            {
                m_sort = static_cast<Sort>(i);
                Filter();
                Invalidate();
            }
            return true;
        }
    }

    if (m_field.Contains(pt))
    {
        m_field.OnLButtonDown(pt);
        return true;
    }

    // A click on a tag leaves the field as it is, so the filter can be typed on after it.
    int index = RowAt(pt);
    if (index >= 0)
    {
        const Node &node = m_nodes[m_visible[static_cast<size_t>(index)]];
        RowBox box = Box(node, RowRect(static_cast<size_t>(index)));
        if (PtInRect(&box.arrow, pt))
        {
            // The arrow opens or closes the branch.
            if (!m_open.erase(node.folded))
                m_open.insert(node.folded);
            Filter();
            Invalidate();
            return true;
        }
        // A tag the search can't name is dimmed, and a click on it does nothing.
        if (!CanFilterBy(node.path))
            return true;
        query = Utf8ToWide(WithTagState(WideToUtf8(query), node.path, NextState(node.state)));
    }
    return true;
}

std::string TagSidebar::TagNameAt(POINT pt) const
{
    int index = RowAt(pt);
    if (index < 0)
        return std::string();
    const Node &node = m_nodes[m_visible[static_cast<size_t>(index)]];
    RowBox box = Box(node, RowRect(static_cast<size_t>(index)));
    return PtInRect(&box.arrow, pt) ? std::string() : node.path;
}

std::string TagSidebar::RowTagAt(POINT pt) const
{
    int index = RowAt(pt);
    return index < 0 ? std::string() : m_nodes[m_visible[static_cast<size_t>(index)]].path;
}

void TagSidebar::SetDropTag(const std::string &name)
{
    std::string folded = fold_case(name);
    if (folded == m_dropFolded)
        return;
    m_dropFolded = std::move(folded);
    Invalidate();
}

bool TagSidebar::DragScroll(POINT pt)
{
    RECT list = ListRect();
    if (!Visible() || pt.x < list.left || pt.x >= list.right)
        return false;
    int edge = Scale(24), step = Scale(10);
    int before = m_scrollY;
    if (pt.y >= list.top && pt.y < list.top + edge)
        ScrollTo(m_scrollY - step);
    else if (pt.y < list.bottom && pt.y >= list.bottom - edge)
        ScrollTo(m_scrollY + step);
    return m_scrollY != before;
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
    renderer.DrawTextLine(strings::kLibraryTitle, Font::kCaptionStrong, title, theme::kText3);
    renderer.PushClip(m_bounds);
    for (int i = 0; i < ShownViewCount(); i++)
        PaintView(renderer, i, m_viewHover.Level(i));
    renderer.PopClip();

    PaintSettings(renderer);

    title.top = TagsTop();
    title.bottom = title.top + Scale(kTitleHeight);
    if (title.top >= m_bounds.bottom)
        return;
    renderer.DrawTextLine(strings::kTagsTitle, Font::kCaptionStrong, title, theme::kText3);
    PaintSort(renderer);

    m_field.Paint(renderer);

    RECT list = ListRect();
    if (list.bottom <= list.top)
        return;

    if (m_visible.empty())
    {
        RECT r = RowRect(0);
        r.left += Scale(kRowPadding);
        renderer.DrawTextLine(m_nodes.empty() ? strings::kNoTags : strings::kNoTagMatches, Font::kCaption, r, theme::kText3);
        return;
    }

    renderer.PushClip(list);
    int height = RowHeight();
    size_t first = static_cast<size_t>(m_scrollY / height);
    size_t last = static_cast<size_t>((m_scrollY + list.bottom - list.top) / height) + 1;
    for (size_t i = first; i < m_visible.size() && i <= last; i++)
        PaintRow(renderer, m_nodes[m_visible[i]], RowRect(i), m_hover.Level(static_cast<int>(i)));
    renderer.PopClip();

    PaintScrollbar(renderer);
}

// The Settings button: a 1.5px line cog (eight teeth around a hole), on a soft fill under the pointer.
void TagSidebar::PaintSettings(Renderer &renderer) const
{
    RECT rect = SettingsRect();
    if (IsRectEmpty(&rect))
        return;
    float hover = m_settingsHover.Level(0);
    if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(rect), static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kHover, hover));

    float u = Scale(4) / 4.0f;  // one 96-DPI pixel
    D2D1_COLOR_F color = hover > 0.0f ? theme::kText1 : m_active ? theme::kText2 : theme::kText3;
    D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
    const int kTeeth = 8;
    const float pitch = 6.28318531f / kTeeth;
    const float tip = 7.0f * u, root = 5.2f * u;
    D2D1_POINT_2F outline[kTeeth * 4];
    size_t n = 0;
    for (int i = 0; i < kTeeth; i++)
    {
        float a = i * pitch;
        const float angles[4] = { a - 0.30f, a - 0.19f, a + 0.19f, a + 0.30f };
        const float radii[4] = { root, tip, tip, root };
        for (int k = 0; k < 4; k++)
            outline[n++] = D2D1::Point2F(c.x + std::cos(angles[k]) * radii[k], c.y + std::sin(angles[k]) * radii[k]);
    }
    renderer.DrawPolyline(outline, n, color, 1.5f * u, true);
    renderer.DrawCircle(c, 2.4f * u, color, 1.5f * u);
}

// A Library view: an icon, its name and how many files it holds. The one shown has a violet highlight, grey while
// the window is inactive.
void TagSidebar::PaintView(Renderer &renderer, int index, float hover) const
{
    RECT rect = ViewRect(index);
    SidebarView view = kViews[ViewIndex(index)];
    bool shown = view == m_view;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    if (shown)
        renderer.FillRoundedRect(ToRectF(rect), radius, m_active ? theme::kYuri : theme::kPlate);
    else if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(rect), radius, Faded(theme::kHover, hover));

    bool onAccent = shown && m_active;
    size_t count = m_viewCounts[static_cast<int>(view)];
    D2D1_COLOR_F text = onAccent ? theme::kOnAccent : theme::kText1;
    D2D1_COLOR_F countColor = onAccent ? theme::kOnAccent : count == 0 ? Faded(theme::kText3, 0.6f) : theme::kText3;
    D2D1_COLOR_F icon = onAccent ? theme::kOnAccent : view == SidebarView::kMissing ? theme::kCaution : theme::kText2;

    float x = static_cast<float>(rect.left + Scale(kRowPadding));
    float cy = (rect.top + rect.bottom) / 2.0f;
    PaintViewIcon(renderer, index, x, cy, icon);

    // No count for Missing until the disk was checked: a 0 would say none are.
    int right = rect.right - Scale(kRowPadding);
    if (view != SidebarView::kMissing || m_missingKnown)
    {
        std::wstring number = FormatCount(count);
        int width = static_cast<int>(renderer.TextWidth(number, Font::kMono) + 0.99f);
        RECT countRect = { right - width, rect.top, right, rect.bottom };
        renderer.DrawTextLine(number, Font::kMono, countRect, countColor, DWRITE_TEXT_ALIGNMENT_TRAILING);
        right -= width + Scale(kCountGap);
    }

    // The names start where the tag names do.
    RECT name = { rect.left + Scale(kRowPadding) + Scale(kMarkerSize) + Scale(kMarkerGap), rect.top, right, rect.bottom };
    if (name.right > name.left)
        renderer.DrawTextLine(kViewNames[ViewIndex(index)], Font::kCaption, name, text);
}

// 1.5px line icons in a kIconSize square from 'x', centred on 'cy': a grid of four squares for All files, a framed
// hill for Pictures, a play triangle for Videos, a note for Audio, an open book for Books, a tag for Untagged and a warning triangle
// for Missing.
void TagSidebar::PaintViewIcon(Renderer &renderer, int index, float x, float cy, const D2D1_COLOR_F &color) const
{
    float u = Scale(4) / 4.0f;  // one 96-DPI pixel
    float line = 1.5f * u;
    float top = cy - kIconSize * u / 2;
    auto at = [&](float px, float py) { return D2D1::Point2F(x + px * u, top + py * u); };

    switch (kViews[ViewIndex(index)])
    {
    case SidebarView::kAll:
        for (int row = 0; row < 2; row++)
        {
            for (int col = 0; col < 2; col++)
            {
                D2D1_POINT_2F a = at(0.75f + col * 6.5f, 0.75f + row * 6.5f);
                renderer.DrawRoundedRect(D2D1::RectF(a.x, a.y, a.x + 4 * u, a.y + 4 * u), u, color, line);
            }
        }
        break;

    case SidebarView::kPictures:
    {
        renderer.DrawRoundedRect(D2D1::RectF(at(0.75f, 1.75f).x, at(0.75f, 1.75f).y, at(11.25f, 10.25f).x,
            at(11.25f, 10.25f).y), 1.5f * u, color, line);
        D2D1_POINT_2F hill[] = { at(2.5f, 9.0f), at(5.5f, 5.5f), at(7.5f, 7.5f), at(8.75f, 6.5f), at(10.0f, 8.0f) };
        renderer.DrawPolyline(hill, 5, color, line);
        renderer.FillCircle(at(8.25f, 4.25f), 1.0f * u, color);
        break;
    }

    case SidebarView::kVideos:
    {
        D2D1_POINT_2F play[] = { at(2.0f, 1.0f), at(11.0f, 6.0f), at(2.0f, 11.0f) };
        renderer.DrawPolyline(play, 3, color, line, true);
        break;
    }

    case SidebarView::kAudio:
    {
        // A note: a head, a stem and a flag.
        renderer.DrawCircle(at(3.75f, 9.0f), 2.0f * u, color, line);
        renderer.DrawLine(at(5.75f, 9.0f), at(5.75f, 1.5f), color, line);
        D2D1_POINT_2F flag[] = { at(5.75f, 1.5f), at(8.0f, 2.5f), at(10.0f, 4.0f) };
        renderer.DrawPolyline(flag, 3, color, line);
        break;
    }

    case SidebarView::kBooks:
    {
        // Two pages open from the spine.
        D2D1_POINT_2F left[] = { at(6.0f, 3.0f), at(4.0f, 1.75f), at(0.75f, 1.75f), at(0.75f, 9.75f), at(4.0f, 9.75f), at(6.0f, 11.0f) };
        D2D1_POINT_2F right[] = { at(6.0f, 3.0f), at(8.0f, 1.75f), at(11.25f, 1.75f), at(11.25f, 9.75f), at(8.0f, 9.75f), at(6.0f, 11.0f) };
        renderer.DrawPolyline(left, 6, color, line);
        renderer.DrawPolyline(right, 6, color, line);
        renderer.DrawLine(at(6.0f, 3.0f), at(6.0f, 11.0f), color, line);
        break;
    }

    case SidebarView::kUntagged:
    {
        D2D1_POINT_2F shape[] = { at(0.75f, 2.0f), at(7.0f, 2.0f), at(11.25f, 6.0f), at(7.0f, 10.0f), at(0.75f, 10.0f) };
        renderer.DrawPolyline(shape, 5, color, line, true);
        renderer.FillCircle(at(3.5f, 6.0f), 1.1f * u, color);
        break;
    }

    case SidebarView::kMissing:
    {
        D2D1_POINT_2F shape[] = { at(6.0f, 0.75f), at(11.5f, 11.0f), at(0.5f, 11.0f) };
        renderer.DrawPolyline(shape, 3, color, line, true);
        renderer.DrawLine(at(6.0f, 4.5f), at(6.0f, 7.25f), color, line);
        renderer.FillCircle(at(6.0f, 9.0f), 0.85f * u, color);
        break;
    }
    }
}

// The Name and Count switch: the one in use on a soft fill, the other in the quiet text colour.
void TagSidebar::PaintSort(Renderer &renderer) const
{
    const wchar_t *const labels[] = { strings::kSortByName, strings::kSortByCount };
    for (int i = 0; i < 2; i++)
    {
        RECT r = SortRect(static_cast<Sort>(i));
        bool on = m_sort == static_cast<Sort>(i);
        float hover = m_sortHover.Level(i);
        if (on)
            renderer.FillRoundedRect(ToRectF(r), static_cast<float>(Scale(theme::kControlRadius)), theme::kControl);
        else if (hover > 0.0f)
            renderer.FillRoundedRect(ToRectF(r), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kHover, hover));
        renderer.DrawTextLine(labels[i], Font::kCaption, r, on ? theme::kText1 : hover > 0.0f ? theme::kText2 : theme::kText3,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// A tag: from the left the disclosure arrow (a parent only), the state marker, the name and the count. In the
// filtered list the name has the branch it is in under it.
void TagSidebar::PaintRow(Renderer &renderer, const Node &node, const RECT &rect, float hover) const
{
    float line = Scale(4) / 4.0f;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    if (hover > 0.0f)
        renderer.FillRoundedRect(ToRectF(rect), radius, Faded(theme::kHover, hover));
    if (!m_dropFolded.empty() && node.folded == m_dropFolded)
    {
        // Files dragged over this tag would be tagged with it: the drop-target glow.
        D2D1_RECT_F box = ToRectF(rect);
        renderer.FillRoundedRect(box, radius, Faded(theme::kOxbloodFill, 0.55f));
        renderer.DrawRoundedRect({ box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2 }, radius,
            theme::kOxblood, 1.5f * line);
    }

    RowBox box = Box(node, rect);
    float cy = (rect.top + rect.bottom) / 2.0f;

    // A tag none of the results have is dimmed; required and excluded ones keep their colour.
    bool dim = node.state == TagState::kOff && (node.count == 0 || !CanFilterBy(node.path));

    if (!IsRectEmpty(&box.arrow))
    {
        // A triangle pointing right, down while the branch is open.
        float u = line, a = kArrowSize * u;
        float x = static_cast<float>(box.marker) - Scale(kArrowSlot) + (Scale(kArrowSlot) - a) / 2.0f;
        bool open = m_open.count(node.folded) != 0;
        D2D1_POINT_2F shape[3];
        if (open)
        {
            shape[0] = D2D1::Point2F(x, cy - a / 4);
            shape[1] = D2D1::Point2F(x + a, cy - a / 4);
            shape[2] = D2D1::Point2F(x + a / 2, cy + a / 4 + 0.5f * u);
        }
        else
        {
            shape[0] = D2D1::Point2F(x + a / 4, cy - a / 2);
            shape[1] = D2D1::Point2F(x + a / 4, cy + a / 2);
            shape[2] = D2D1::Point2F(x + a * 3 / 4 + 0.5f * u, cy);
        }
        renderer.FillPolygon(shape, 3, dim ? Faded(theme::kText3, 0.6f) : theme::kText3);
    }

    float size = static_cast<float>(Scale(kMarkerSize));
    D2D1_RECT_F marker = D2D1::RectF(box.marker, cy - size / 2, box.marker + size, cy + size / 2);

    D2D1_COLOR_F color;
    if (node.state == TagState::kRequired)
    {
        color = theme::kYuri;
        renderer.FillRoundedRect(marker, 1.5f * line, color);
    }
    else if (node.state == TagState::kExcluded)
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
    if (node.state != TagState::kExcluded)
        renderer.DrawTextLine(FormatCount(node.count), Font::kMono, box.count, dim ? Faded(theme::kText3, 0.6f) : theme::kText3,
            DWRITE_TEXT_ALIGNMENT_TRAILING);

    RECT name = box.name;
    if (name.right <= name.left)
        return;
    bool note = m_flat && !node.parentLabel.empty();
    if (note)
    {
        // The name over the branch it is in, together centred in the row.
        int text = static_cast<int>(renderer.LineHeight(Font::kCaption) + 0.99f);
        int top = rect.top + (rect.bottom - rect.top - 2 * text) / 2;
        name.top = top;
        name.bottom = top + text;
        RECT under = { name.left, name.bottom, name.right, name.bottom + text };
        renderer.DrawTextLine(std::wstring(strings::kTagIn) + L" " + node.parentLabel, Font::kCaption, under,
            dim ? Faded(theme::kText3, 0.6f) : theme::kText3);
    }
    renderer.DrawTextLine(node.label, Font::kCaption, name, color);
    if (node.state == TagState::kExcluded)
    {
        float width = std::min(renderer.TextWidth(node.label, Font::kCaption), static_cast<float>(name.right - name.left));
        float y = std::floor(note ? (name.top + name.bottom) / 2.0f : cy) + line / 2;
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
    int content = static_cast<int>(m_visible.size()) * RowHeight();
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
