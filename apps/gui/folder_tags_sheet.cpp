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
#include "../../include/text.hpp"
#include <shlwapi.h>
#include <algorithm>
#include <unordered_map>

namespace
{
    const int kCardWidth = 520;
    const int kMargin = 24;         // least room between the card and the window's edges
    const int kPadding = 20;
    const int kTitleHeight = 24;
    const int kIntroHeight = 34;    // two lines of captions
    const int kGap = 10;
    const int kPresetHeight = 28;
    const int kPresetPadding = 12;  // inside a preset button, left and right of its label
    const int kSegmentInset = 2;    // between the preset switch's edge and its buttons
    const int kRowHeight = 28;
    const int kTreePadding = 4;
    const int kRowInset = 8;
    const int kIndent = 14;         // per level of the tree
    const int kArrowSlot = 14;
    const int kArrowSize = 6;
    const int kBox = 9;             // the tick square
    const int kBoxGap = 8;          // between the square and the name
    const int kRowRadius = 3;
    const int kNoteHeight = 18;     // the dashed "in Album" beside an unticked folder
    const int kNotePadding = 5;
    const int kNoteGap = 8;
    const int kCountGap = 8;
    const int kRemoveGap = 8;
    const int kFooterGap = 14;
    const int kScrollbarWidth = 4;
    const int kScrollbarMinThumb = 20;

    std::wstring Files(size_t count)
    {
        return count == 1 ? std::wstring(strings::kOneFile) : FormatCount(count) + L" " + strings::kFiles;
    }
}

int FolderTagsSheet::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void FolderTagsSheet::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

// ---- The tree ----

// Builds the tree from the paths of the folders that have files: the folders above them are nodes too, whether or
// not they have files of their own.
void FolderTagsSheet::Build(std::vector<FolderTag> tags)
{
    struct Temp
    {
        std::string path;
        std::wstring name;
        std::vector<int> kids;
        std::vector<std::string> files;
    };
    std::vector<Temp> temp;
    std::vector<int> tops;
    std::unordered_map<std::string, int> byPath;

    m_fileCount = 0;
    for (FolderTag &tag : tags)
    {
        int parent = -1;
        size_t end = 0;
        for (;;)
        {
            end = tag.name.find('/', end);
            std::string path = tag.name.substr(0, end);
            std::string folded = fold_case(path);
            auto found = byPath.find(folded);
            int index;
            if (found != byPath.end())
                index = found->second;
            else
            {
                index = static_cast<int>(temp.size());
                size_t slash = path.rfind('/');
                temp.push_back({ path, Utf8ToWide(slash == std::string::npos ? path : path.substr(slash + 1)), {}, {} });
                byPath.emplace(folded, index);
                (parent < 0 ? tops : temp[static_cast<size_t>(parent)].kids).push_back(index);
            }
            parent = index;
            if (end == std::string::npos)
                break;
            end++;
        }
        m_fileCount += tag.paths.size();
        std::vector<std::string> &files = temp[static_cast<size_t>(parent)].files;
        files.insert(files.end(), tag.paths.begin(), tag.paths.end());
    }

    auto before = [&temp](int a, int b)
    {
        int c = StrCmpLogicalW(temp[static_cast<size_t>(a)].name.c_str(), temp[static_cast<size_t>(b)].name.c_str());
        return c != 0 ? c < 0 : a < b;
    };
    std::sort(tops.begin(), tops.end(), before);

    // Depth first, so a folder is followed by what is under it.
    m_nodes.clear();
    std::vector<std::pair<int, int>> stack;  // temp index, parent node
    for (auto top = tops.rbegin(); top != tops.rend(); ++top)
        stack.push_back({ *top, -1 });
    while (!stack.empty())
    {
        auto [index, parent] = stack.back();
        stack.pop_back();
        Temp &t = temp[static_cast<size_t>(index)];
        Node node;
        node.path = t.path;
        node.name = t.name;
        node.parent = parent;
        node.depth = parent < 0 ? 0 : m_nodes[static_cast<size_t>(parent)].depth + 1;
        node.hasKids = !t.kids.empty();
        node.open = node.hasKids && node.depth <= 1;  // the root and its first level
        node.files = std::move(t.files);
        node.total = node.files.size();
        int self = static_cast<int>(m_nodes.size());
        m_nodes.push_back(std::move(node));
        std::sort(t.kids.begin(), t.kids.end(), before);
        for (auto kid = t.kids.rbegin(); kid != t.kids.rend(); ++kid)
            stack.push_back({ *kid, self });
    }
    for (size_t i = m_nodes.size(); i-- > 0;)
    {
        if (m_nodes[i].parent >= 0)
            m_nodes[static_cast<size_t>(m_nodes[i].parent)].total += m_nodes[i].total;
    }
}

// The nodes with a row: those under open folders only.
void FolderTagsSheet::Rebuild()
{
    m_rows.clear();
    std::vector<char> shown(m_nodes.size(), 0);
    for (size_t i = 0; i < m_nodes.size(); i++)
    {
        const Node &node = m_nodes[i];
        shown[i] = node.parent < 0 || (shown[static_cast<size_t>(node.parent)] && m_nodes[static_cast<size_t>(node.parent)].open);
        if (shown[i])
            m_rows.push_back(static_cast<int>(i));
    }
    m_pick = std::min(m_pick, static_cast<int>(m_rows.size()) + (m_offerRemoveOld ? 0 : -1));
    m_pick = std::max(m_pick, 0);
    ScrollTo(m_first);
}

// The folder whose name a file in 'node' takes: the nearest ticked one from it upward, or -1.
int FolderTagsSheet::EffectiveTag(int node) const
{
    for (int n = node; n >= 0; n = m_nodes[static_cast<size_t>(n)].parent)
    {
        if (m_nodes[static_cast<size_t>(n)].on)
            return n;
    }
    return -1;
}

size_t FolderTagsSheet::TaggedFiles() const
{
    size_t count = 0;
    for (size_t i = 0; i < m_nodes.size(); i++)
    {
        if (!m_nodes[i].files.empty() && EffectiveTag(static_cast<int>(i)) >= 0)
            count += m_nodes[i].files.size();
    }
    return count;
}

size_t FolderTagsSheet::TagCount() const
{
    std::vector<char> used(m_nodes.size(), 0);
    for (size_t i = 0; i < m_nodes.size(); i++)
    {
        int tag = m_nodes[i].files.empty() ? -1 : EffectiveTag(static_cast<int>(i));
        if (tag >= 0)
            used[static_cast<size_t>(tag)] = 1;
    }
    return static_cast<size_t>(std::count(used.begin(), used.end(), 1));
}

std::vector<FolderTag> FolderTagsSheet::Tags() const
{
    std::vector<FolderTag> tags;
    std::vector<int> slot(m_nodes.size(), -1);
    for (size_t i = 0; i < m_nodes.size(); i++)
    {
        if (m_nodes[i].files.empty())
            continue;
        int tag = EffectiveTag(static_cast<int>(i));
        if (tag < 0)
            continue;
        if (slot[static_cast<size_t>(tag)] < 0)
        {
            slot[static_cast<size_t>(tag)] = static_cast<int>(tags.size());
            tags.push_back({ m_nodes[static_cast<size_t>(tag)].path, {}, true });
        }
        std::vector<std::string> &paths = tags[static_cast<size_t>(slot[static_cast<size_t>(tag)])].paths;
        paths.insert(paths.end(), m_nodes[i].files.begin(), m_nodes[i].files.end());
    }
    return tags;
}

// The name of the one folder everything is under, or empty with several roots.
std::wstring FolderTagsSheet::RootName() const
{
    std::wstring name;
    for (const Node &node : m_nodes)
    {
        if (node.parent >= 0)
            continue;
        if (!name.empty())
            return std::wstring();
        name = node.name;
    }
    return name;
}

std::wstring FolderTagsSheet::ApplyLabel() const
{
    size_t files = TaggedFiles();
    return files > 0 ? strings::kFolderTagsTitleBefore + Files(files) : std::wstring(strings::kFolderTagsApply);
}

bool FolderTagsSheet::CanApply() const
{
    return TaggedFiles() > 0 || RemoveOld();
}

void FolderTagsSheet::ApplyPreset(Preset preset)
{
    for (Node &node : m_nodes)
        node.on = preset == Preset::kEvery || node.parent < 0;
    m_preset = preset;
    Invalidate();
}

// ---- Layout ----

// The rows that show at once: as many as the window leaves room for, up to ten. Not fewer when branches are closed,
// so the card keeps its size while the tree opens and closes.
int FolderTagsSheet::RowsShown() const
{
    int fixed = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kIntroHeight) + 2 * Scale(kGap) + Scale(kPresetHeight) +
        2 * Scale(kTreePadding) + Scale(kFooterGap) + Scale(kButtonHeight) + (m_offerRemoveOld ? Scale(kRemoveGap) + Scale(kRowHeight) : 0);
    int room = (static_cast<int>(m_bounds.bottom - m_bounds.top) - fixed - 2 * Scale(kMargin)) / Scale(kRowHeight);
    return std::max(3, std::min({ kMaxRows, room, static_cast<int>(m_nodes.size()) }));
}

RECT FolderTagsSheet::CardRect() const
{
    int width = std::min(Scale(kCardWidth), static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kMargin));
    int height = 2 * Scale(kPadding) + Scale(kTitleHeight) + Scale(kIntroHeight) + 2 * Scale(kGap) + Scale(kPresetHeight) +
        2 * Scale(kTreePadding) + RowsShown() * Scale(kRowHeight) + Scale(kFooterGap) + Scale(kButtonHeight) +
        (m_offerRemoveOld ? Scale(kRemoveGap) + Scale(kRowHeight) : 0);
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int top = std::max<int>(m_bounds.top, (m_bounds.top + m_bounds.bottom - height) / 2 - Scale(40));
    return { left, top, left + width, top + height };
}

// The Only <root> and Every folder buttons of the switch under the intro.
RECT FolderTagsSheet::PresetRect(Preset which) const
{
    RECT card = CardRect();
    std::wstring root = RootName();
    std::wstring only = root.empty() ? std::wstring(strings::kFolderTagsOnlyTops) : strings::kFolderTagsOnly + root;
    int onlyWidth = static_cast<int>(m_renderer->TextWidth(only, Font::kBody) + 0.99f) + 2 * Scale(kPresetPadding);
    int everyWidth = static_cast<int>(m_renderer->TextWidth(strings::kFolderTagsEvery, Font::kBody) + 0.99f) + 2 * Scale(kPresetPadding);
    int maxWidth = static_cast<int>(card.right - card.left) - 2 * Scale(kPadding) - 2 * Scale(kSegmentInset) - everyWidth;
    onlyWidth = std::min(onlyWidth, std::max(maxWidth, Scale(60)));
    int left = card.left + Scale(kPadding) + Scale(kSegmentInset);
    int top = card.top + Scale(kPadding) + Scale(kTitleHeight) + Scale(kIntroHeight) + Scale(kGap) + Scale(kSegmentInset);
    int height = Scale(kPresetHeight) - 2 * Scale(kSegmentInset);
    if (which == Preset::kRoot)
        return { left, top, left + onlyWidth, top + height };
    return { left + onlyWidth, top, left + onlyWidth + everyWidth, top + height };
}

RECT FolderTagsSheet::TreeRect() const
{
    RECT card = CardRect();
    int top = card.top + Scale(kPadding) + Scale(kTitleHeight) + Scale(kIntroHeight) + 2 * Scale(kGap) + Scale(kPresetHeight);
    return { card.left + Scale(kPadding), top, card.right - Scale(kPadding),
        top + 2 * Scale(kTreePadding) + RowsShown() * Scale(kRowHeight) };
}

RECT FolderTagsSheet::RowRect(int slot) const
{
    RECT tree = TreeRect();
    int top = tree.top + Scale(kTreePadding) + slot * Scale(kRowHeight);
    return { tree.left + Scale(kTreePadding), top, tree.right - Scale(kTreePadding), top + Scale(kRowHeight) };
}

RECT FolderTagsSheet::RemoveRect() const
{
    RECT tree = TreeRect();
    return { tree.left, tree.bottom + Scale(kRemoveGap), tree.right, tree.bottom + Scale(kRemoveGap) + Scale(kRowHeight) };
}

RECT FolderTagsSheet::ConfirmRect() const
{
    RECT card = CardRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, ApplyLabel().c_str());
    int bottom = card.bottom - Scale(kPadding);
    return { card.right - Scale(kPadding) - width, bottom - Scale(kButtonHeight), card.right - Scale(kPadding), bottom };
}

RECT FolderTagsSheet::SkipRect() const
{
    RECT confirm = ConfirmRect();
    int width = ButtonWidth(*m_renderer, m_hWnd, strings::kFolderTagsSkip);
    return { confirm.left - Scale(kButtonGap) - width, confirm.top, confirm.left - Scale(kButtonGap), confirm.bottom };
}

// The summary line at the foot, left of the buttons.
RECT FolderTagsSheet::SummaryRect() const
{
    RECT card = CardRect(), skip = SkipRect();
    return { card.left + Scale(kPadding), skip.top, skip.left - Scale(kGap), skip.bottom };
}

// The arrow's hit area: its slot and a little around it, the height of the row. Empty on a folder with nothing under it.
RECT FolderTagsSheet::ArrowRect(const RECT &row, const Node &node) const
{
    if (!node.hasKids)
        return RECT{};
    int x = row.left + Scale(kRowInset) + node.depth * Scale(kIndent);
    return { x - Scale(kRowInset) / 2, row.top, x + Scale(kArrowSlot), row.bottom };
}

RECT FolderTagsSheet::TickRect(const RECT &row, const Node &node) const
{
    int x = row.left + Scale(kRowInset) + node.depth * Scale(kIndent) + Scale(kArrowSlot);
    int size = Scale(kBox);
    int top = (row.top + row.bottom - size) / 2;
    return { x, top, x + size, top + size };
}

// The index into m_rows of the row under the point, or -1.
int FolderTagsSheet::RowAt(POINT pt) const
{
    RECT tree = TreeRect();
    if (!PtInRect(&tree, pt))
        return -1;
    for (int slot = 0; slot < RowsShown(); slot++)
    {
        RECT r = RowRect(slot);
        int index = m_first + slot;
        if (PtInRect(&r, pt) && index < static_cast<int>(m_rows.size()))
            return index;
    }
    return -1;
}

// What is under the point: a row (its index into m_rows) or one of the negative kHover values.
int FolderTagsSheet::Target(POINT pt) const
{
    RECT confirm = ConfirmRect(), skip = SkipRect(), only = PresetRect(Preset::kRoot), every = PresetRect(Preset::kEvery);
    if (PtInRect(&confirm, pt))
        return kHoverApply;
    if (PtInRect(&skip, pt))
        return kHoverSkip;
    if (PtInRect(&only, pt))
        return kHoverRoot;
    if (PtInRect(&every, pt))
        return kHoverEvery;
    if (m_offerRemoveOld)
    {
        RECT remove = RemoveRect();
        if (PtInRect(&remove, pt))
            return kHoverRemove;
    }
    return RowAt(pt);
}

// ---- Opening and closing ----

void FolderTagsSheet::Open(HWND hWnd, Renderer &renderer, const RECT &bounds, std::vector<FolderTag> tags, bool offerRemoveOld)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_offerRemoveOld = offerRemoveOld;
    m_removeOld = true;
    m_preset = Preset::kEvery;
    m_open = true;
    m_first = m_pick = 0;
    m_hover = kHoverNone;
    m_keys = false;
    Build(std::move(tags));
    Rebuild();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void FolderTagsSheet::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_nodes.clear();
    m_rows.clear();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void FolderTagsSheet::ScrollTo(int first)
{
    int max = std::max(0, static_cast<int>(m_rows.size()) - RowsShown());
    first = std::max(0, std::min(first, max));
    if (first != m_first)
    {
        m_first = first;
        Invalidate();
    }
}

// Moves the keyboard to a row (or, one past the last, the Remove option), scrolling it into view.
void FolderTagsSheet::Pick(int index)
{
    int last = static_cast<int>(m_rows.size()) - 1 + (m_offerRemoveOld ? 1 : 0);
    m_pick = std::max(0, std::min(index, last));
    int shown = RowsShown();
    if (m_pick < m_first)
        ScrollTo(m_pick);
    else if (m_pick < static_cast<int>(m_rows.size()) && m_pick >= m_first + shown)
        ScrollTo(m_pick - shown + 1);
    Invalidate();
}

void FolderTagsSheet::ToggleOpen(int visibleIndex)
{
    Node &node = m_nodes[static_cast<size_t>(m_rows[static_cast<size_t>(visibleIndex)])];
    if (!node.hasKids)
        return;
    node.open = !node.open;
    Rebuild();
    Invalidate();
}

// ---- Input ----

void FolderTagsSheet::OnHover(POINT pt)
{
    if (!m_open)
        return;
    int hover = Target(pt);
    if (hover != m_hover)
    {
        m_hover = hover;
        Invalidate();
    }
}

void FolderTagsSheet::OnMouseWheel(POINT, int delta)
{
    ScrollTo(m_first - (delta > 0 ? 1 : -1) * 3 * std::max(1, std::abs(delta) / WHEEL_DELTA));
    m_hover = kHoverNone;
}

FolderTagsSheet::Action FolderTagsSheet::OnLButtonDown(POINT pt, bool)
{
    m_keys = false;
    RECT card = CardRect();
    int target = Target(pt);
    if (target == kHoverApply)
        return CanApply() ? Action::kConfirm : Action::kNone;
    if (target == kHoverSkip || (target == kHoverNone && !PtInRect(&card, pt)))
        return Action::kCancel;
    if (target == kHoverRoot)
        ApplyPreset(Preset::kRoot);
    else if (target == kHoverEvery)
        ApplyPreset(Preset::kEvery);
    else if (target == kHoverRemove)
    {
        m_removeOld = !m_removeOld;
        m_pick = static_cast<int>(m_rows.size());
        Invalidate();
    }
    else if (target >= 0)
    {
        Node &node = m_nodes[static_cast<size_t>(m_rows[static_cast<size_t>(target)])];
        RECT arrow = ArrowRect(RowRect(target - m_first), node);
        m_pick = target;
        if (PtInRect(&arrow, pt))
            ToggleOpen(target);
        else
        {
            node.on = !node.on;
            m_preset = Preset::kCustom;
            Invalidate();
        }
    }
    return Action::kNone;
}

FolderTagsSheet::Action FolderTagsSheet::OnKeyDown(WPARAM key, bool ctrl)
{
    m_keys = true;
    bool onRemove = m_offerRemoveOld && m_pick == static_cast<int>(m_rows.size());
    Node *node = onRemove ? nullptr : &m_nodes[static_cast<size_t>(m_rows[static_cast<size_t>(m_pick)])];
    switch (key)
    {
    case VK_ESCAPE:
        return Action::kCancel;
    case VK_RETURN:
        return CanApply() ? Action::kConfirm : Action::kNone;
    case VK_UP:
        Pick(m_pick - 1);
        break;
    case VK_DOWN:
        Pick(m_pick + 1);
        break;
    case VK_HOME:
        Pick(0);
        break;
    case VK_END:
        Pick(static_cast<int>(m_rows.size()));
        break;
    case VK_LEFT:
        // Closes the folder, else goes up to the one it is in.
        if (node && node->hasKids && node->open)
            ToggleOpen(m_pick);
        else if (node && node->parent >= 0)
            Pick(static_cast<int>(std::find(m_rows.begin(), m_rows.end(), node->parent) - m_rows.begin()));
        break;
    case VK_RIGHT:
        // Opens the folder, else goes down into it.
        if (node && node->hasKids && !node->open)
            ToggleOpen(m_pick);
        else if (node && node->hasKids)
            Pick(m_pick + 1);
        break;
    case VK_SPACE:
        if (onRemove)
            m_removeOld = !m_removeOld;
        else
        {
            node->on = !node->on;
            m_preset = Preset::kCustom;
        }
        Invalidate();
        break;
    case 'A':
        // Ctrl+A ticks every folder, or unticks them all when they were.
        if (ctrl)
        {
            bool all = std::all_of(m_nodes.begin(), m_nodes.end(), [](const Node &n) { return n.on; });
            for (Node &n : m_nodes)
                n.on = !all;
            m_preset = all ? Preset::kCustom : Preset::kEvery;
            Invalidate();
        }
        break;
    }
    return Action::kNone;
}

// ---- Painting ----

void FolderTagsSheet::Paint(Renderer &renderer) const
{
    if (!m_open)
        return;

    PaintBackdrop(renderer, m_bounds);
    RECT card = CardRect();
    PaintCard(renderer, m_hWnd, card);
    const float line = Scale(4) / 4.0f;
    const float radius = static_cast<float>(Scale(theme::kControlRadius));
    const int pad = Scale(kPadding);

    RECT title = { card.left + pad, card.top + pad, card.right - pad, card.top + pad + Scale(kTitleHeight) };
    renderer.DrawTextLine(strings::kFolderTagsTitleBefore + Files(m_fileCount) + strings::kFolderTagsTitleAfter, Font::kBodyStrong,
        title, theme::kText1);
    ComPtr<IDWriteTextLayout> intro = renderer.Layout(strings::kFolderTagsIntro, Font::kCaption,
        static_cast<float>(title.right - title.left), 2);
    if (intro)
        renderer.DrawLayout(intro.Get(), static_cast<float>(title.left), static_cast<float>(title.bottom), theme::kText3);

    // The presets: a switch of two, the one that matches the ticks lit.
    RECT only = PresetRect(Preset::kRoot), every = PresetRect(Preset::kEvery);
    RECT frame = { only.left - Scale(kSegmentInset), only.top - Scale(kSegmentInset), every.right + Scale(kSegmentInset),
        only.bottom + Scale(kSegmentInset) };
    renderer.FillRoundedRect(ToRectF(frame), radius, theme::kControl);
    std::wstring root = RootName();
    std::wstring onlyLabel = root.empty() ? std::wstring(strings::kFolderTagsOnlyTops) : strings::kFolderTagsOnly + root;
    struct Preset3 { RECT rect; std::wstring label; Preset which; int hover; };
    const Preset3 presets[] = { { only, onlyLabel, Preset::kRoot, kHoverRoot }, { every, strings::kFolderTagsEvery, Preset::kEvery, kHoverEvery } };
    for (const Preset3 &p : presets)
    {
        bool lit = m_preset == p.which;
        if (lit)
            renderer.FillRoundedRect(ToRectF(p.rect), radius, theme::kChip);
        else if (m_hover == p.hover)
            renderer.FillRoundedRect(ToRectF(p.rect), radius, theme::kHover);
        RECT text = { p.rect.left + Scale(kPresetPadding) / 2, p.rect.top, p.rect.right - Scale(kPresetPadding) / 2, p.rect.bottom };
        renderer.DrawTextLine(p.label, Font::kBody, text, lit ? theme::kText1 : theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // The tree, in a recessed box.
    RECT tree = TreeRect();
    renderer.FillRoundedRect(ToRectF(tree), radius, theme::kLayer);
    renderer.PushClip(tree);
    int shown = RowsShown();
    for (int slot = 0; slot < shown && m_first + slot < static_cast<int>(m_rows.size()); slot++)
    {
        int index = m_first + slot;
        int nodeIndex = m_rows[static_cast<size_t>(index)];
        const Node &node = m_nodes[static_cast<size_t>(nodeIndex)];
        RECT row = RowRect(slot);
        D2D1_RECT_F box = ToRectF(row);
        if (index == m_hover)
            renderer.FillRoundedRect(box, static_cast<float>(Scale(kRowRadius)), theme::kHover);
        if (m_keys && index == m_pick)
            renderer.DrawRoundedRect({ box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2 },
                static_cast<float>(Scale(kRowRadius)), Faded(theme::kYuri, 0.6f), line);
        float cy = (row.top + row.bottom) / 2.0f;

        // The disclosure arrow: right when closed, down when open.
        if (node.hasKids)
        {
            float a = kArrowSize * line;
            float x = static_cast<float>(row.left + Scale(kRowInset) + node.depth * Scale(kIndent)) + (Scale(kArrowSlot) - a) / 2.0f;
            D2D1_POINT_2F shape[3];
            if (node.open)
            {
                shape[0] = D2D1::Point2F(x, cy - a / 4);
                shape[1] = D2D1::Point2F(x + a, cy - a / 4);
                shape[2] = D2D1::Point2F(x + a / 2, cy + a / 4 + 0.5f * line);
            }
            else
            {
                shape[0] = D2D1::Point2F(x + a / 4, cy - a / 2);
                shape[1] = D2D1::Point2F(x + a / 4, cy + a / 2);
                shape[2] = D2D1::Point2F(x + a * 3 / 4 + 0.5f * line, cy);
            }
            renderer.FillPolygon(shape, 3, theme::kText3);
        }

        // The tick: the sidebar's square, violet when the folder gives a tag.
        D2D1_RECT_F tick = ToRectF(TickRect(row, node));
        if (node.on)
            renderer.FillRoundedRect(tick, 1.5f * line, theme::kYuri);
        else
            renderer.DrawRoundedRect({ tick.left + line / 2, tick.top + line / 2, tick.right - line / 2, tick.bottom - line / 2 },
                1.5f * line, theme::kText3, line);

        // The count at the right edge: the files in the folder and under it.
        std::wstring count = FormatCount(node.total);
        int countWidth = static_cast<int>(renderer.TextWidth(count, Font::kMono) + 0.99f);
        RECT countRect = { row.right - Scale(kRowInset) - countWidth, row.top, row.right - Scale(kRowInset), row.bottom };
        renderer.DrawTextLine(count, Font::kMono, countRect, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);

        int right = countRect.left - Scale(kCountGap);
        if (!node.on)
        {
            // Its files take the tag of the folder above, or none: a dashed note, like the strip's partial chips.
            int above = node.parent >= 0 ? EffectiveTag(node.parent) : -1;
            std::wstring note = above >= 0 ? std::wstring(strings::kTagIn) + L" " + m_nodes[static_cast<size_t>(above)].name
                                           : std::wstring(strings::kFolderTagsNoTag);
            int width = static_cast<int>(renderer.TextWidth(note, Font::kCaption) + 0.99f) + 2 * Scale(kNotePadding);
            width = std::min(width, std::max(0, (right - static_cast<int>(tick.right)) / 2));
            RECT noteRect = { right - width, static_cast<LONG>(cy) - Scale(kNoteHeight) / 2, right, static_cast<LONG>(cy) - Scale(kNoteHeight) / 2 + Scale(kNoteHeight) };
            if (width > 2 * Scale(kNotePadding))
            {
                D2D1_RECT_F dashed = ToRectF(noteRect);
                renderer.DrawRoundedRect({ dashed.left + line / 2, dashed.top + line / 2, dashed.right - line / 2, dashed.bottom - line / 2 },
                    static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kText3, 0.7f), line, true);
                RECT text = { noteRect.left + Scale(kNotePadding), noteRect.top, noteRect.right - Scale(kNotePadding), noteRect.bottom };
                renderer.DrawTextLine(note, Font::kCaption, text, theme::kText3);
                right = noteRect.left - Scale(kNoteGap);
            }
        }

        RECT name = { static_cast<LONG>(tick.right) + Scale(kBoxGap), row.top, right, row.bottom };
        if (name.right > name.left)
            renderer.DrawTextLine(node.name, Font::kBody, name, node.on ? theme::kText1 : theme::kText3);
    }
    renderer.PopClip();

    // A thin thumb while there are more rows than show.
    int rows = static_cast<int>(m_rows.size());
    if (rows > shown)
    {
        int track = tree.bottom - tree.top - 2 * Scale(kTreePadding);
        int thumb = std::max(Scale(kScrollbarMinThumb), MulDiv(track, shown, rows));
        int top = tree.top + Scale(kTreePadding) + MulDiv(track - thumb, m_first, rows - shown);
        RECT thumbRect = { tree.right - Scale(kScrollbarWidth) - Scale(2), top, tree.right - Scale(2), top + thumb };
        renderer.FillRoundedRect(ToRectF(thumbRect), static_cast<float>(Scale(2)), theme::kScrollThumb);
    }

    if (m_offerRemoveOld)
    {
        RECT remove = RemoveRect();
        if (m_hover == kHoverRemove)
            renderer.FillRoundedRect(ToRectF(remove), static_cast<float>(Scale(kRowRadius)), theme::kHover);
        if (m_keys && m_pick == rows)
            renderer.DrawRoundedRect({ remove.left + line / 2.0f, remove.top + line / 2.0f, remove.right - line / 2.0f, remove.bottom - line / 2.0f },
                static_cast<float>(Scale(kRowRadius)), Faded(theme::kYuri, 0.6f), line);
        int size = Scale(kBox);
        float x = static_cast<float>(remove.left + Scale(kRowInset));
        float y = (remove.top + remove.bottom - size) / 2.0f;
        D2D1_RECT_F square = { x, y, x + size, y + size };
        if (m_removeOld)
            renderer.FillRoundedRect(square, 1.5f * line, theme::kYuri);
        else
            renderer.DrawRoundedRect({ square.left + line / 2, square.top + line / 2, square.right - line / 2, square.bottom - line / 2 },
                1.5f * line, theme::kText3, line);
        RECT text = { static_cast<LONG>(square.right) + Scale(kBoxGap), remove.top, remove.right, remove.bottom };
        renderer.DrawTextLine(strings::kFolderTagsRemoveOld, Font::kBody, text, theme::kText2);
    }

    // The summary: how many tags the ticks make.
    RECT summary = SummaryRect();
    renderer.PushClip(summary);
    size_t tags = TagCount();
    float x = static_cast<float>(summary.left);
    auto part = [&](const std::wstring &text, Font font, const D2D1_COLOR_F &color)
    {
        RECT r = { static_cast<LONG>(x), summary.top, summary.right, summary.bottom };
        renderer.DrawTextLine(text, font, r, color);
        x += renderer.TextWidth(text, font);
    };
    if (tags == 0)
        part(strings::kFolderTagsNone, Font::kCaption, theme::kText3);
    else
    {
        bool single = !root.empty();
        part(FormatCount(tags), Font::kMono, theme::kText1);
        part(std::wstring(L" ") + (single ? (tags == 1 ? strings::kFolderTagsOneTag : strings::kFolderTagsTags)
                                          : (tags == 1 ? strings::kFolderTagsOneTagFlat : strings::kFolderTagsTagsFlat)) + (single ? L" " : L""),
            Font::kCaption, theme::kText3);
        if (single)
            part(root, Font::kMono, theme::kText1);
    }
    renderer.PopClip();

    PaintButton(renderer, m_hWnd, SkipRect(), strings::kFolderTagsSkip, false, m_hover == kHoverSkip);
    bool can = CanApply();
    PaintButton(renderer, m_hWnd, ConfirmRect(), ApplyLabel().c_str(), can, can && m_hover == kHoverApply);
}
