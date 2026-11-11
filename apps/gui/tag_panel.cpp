#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "tag_panel.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "filter_query.hpp"
#include "../../include/text.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include <algorithm>
#include <unordered_map>

namespace
{
    const int kLabelWidth = 64;      // "123 files", right-aligned before the button
    const int kAddWidth = 84;        // "+ Add tag"
    const int kGap = 12;             // between the label, the button and the chips
    const int kChipHeight = 22;
    const int kChipGap = 6;          // between chips
    const int kChipPadding = 8;      // inside a chip, left of the name and right of the x
    const int kCountGap = 4;         // between the name and the count of a partial chip
    const int kCloseGap = 6;         // between the name and the x
    const int kCloseSize = 7;        // the x
    const int kMaxLabel = 220;       // the widest a chip's name gets before its middle is cut
    const int kWheelStep = 40;       // pixels the chips scroll per wheel notch
    const int kAddHover = 1 << 20;   // the hover id of the add button
    const int kBarHeight = 40;
    const int kBarPadding = 14;      // inside the bar, left of the count and right of the chips
    const int kBarBottomGap = 16;    // from the bottom of the grid
    const int kBarSideGap = 16;      // from the sides of the grid, at the narrowest
    const int kBarMaxWidth = 820;
    const int kBarRise = 10;         // how far below its place the bar starts
    const unsigned kBarRiseMs = 140;
}

int TagPanel::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void TagPanel::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

bool TagPanel::Contains(POINT pt) const
{
    if (!Visible())
        return false;
    RECT bar = Bar();
    return PtInRect(&bar, pt) != 0;
}

// The bar's place: as wide as what it holds, but not wider than the grid, centered, with a gap under it.
RECT TagPanel::Bar() const
{
    int pad = Scale(kBarPadding);
    int content = pad + Scale(kLabelWidth) + Scale(kGap) + Scale(kAddWidth) + (m_chipsWidth > 0 ? Scale(kGap) + m_chipsWidth : 0) + pad;
    int room = std::min<int>(static_cast<int>(m_bounds.right - m_bounds.left) - 2 * Scale(kBarSideGap), Scale(kBarMaxWidth));
    int width = std::max(0, std::min(content, room));
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    int bottom = m_bounds.bottom - Scale(kBarBottomGap);
    int top = bottom - Scale(kBarHeight);
    // The grid may be shorter than the bar.
    if (top < m_bounds.top)
        top = m_bounds.top, bottom = std::min<int>(bottom, top + Scale(kBarHeight));
    int drop = static_cast<int>((1.0f - m_level) * Scale(kBarRise) + 0.5f);
    RECT r = { left, top + drop, left + width, bottom + drop };
    return r;
}

void TagPanel::SetSuppressed(bool suppressed)
{
    if (suppressed == m_suppressed)
        return;
    m_suppressed = suppressed;
    if (!Visible())
    {
        m_level = 0.0f;
        KillTimer(m_hWnd, kTagBarTimer);
    }
    else
        Rise();
    Invalidate();
}

// Starts the bar rising when it appears; at once if Windows animations are off. It keeps its place while the
// selection changes under it.
void TagPanel::Rise()
{
    if (m_level >= 1.0f)
        return;
    if (!AnimationsEnabled())
    {
        m_level = 1.0f;
        return;
    }
    m_riseTick = GetTickCount64();
    SetTimer(m_hWnd, kTagBarTimer, theme::kFrameMs, nullptr);
}

void TagPanel::OnRiseTimer()
{
    ULONGLONG now = GetTickCount64();
    m_level = std::min(1.0f, m_level + static_cast<float>(now - m_riseTick) / kBarRiseMs);
    m_riseTick = now;
    if (m_level >= 1.0f)
        KillTimer(m_hWnd, kTagBarTimer);
    Invalidate();
}

void TagPanel::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;

    // The DPI may have changed, and with it the size of every chip.
    MeasureChips();
}

// ---- Selection ----

void TagPanel::SetSelection(const std::vector<file*> &files)
{
    bool wasVisible = Visible();
    m_selectedCount = files.size();
    if (!Visible())
    {
        m_level = 0.0f;
        KillTimer(m_hWnd, kTagBarTimer);
    }
    else if (!wasVisible)
        Rise();

    // The levels the selected files have, and how many files have each.
    std::vector<std::vector<std::string>> fileTags;
    for (const file *f : files)
    {
        std::vector<std::string> names;
        for (const tag *t : f->get_tags())
            names.push_back(t->get_id());
        fileTags.push_back(std::move(names));
    }

    std::vector<Chip> chips;
    for (LevelChip &level : LevelChips(fileTags))
    {
        bool partial = level.files < files.size();
        std::wstring count = partial ? std::to_wstring(level.files) + L"/" + std::to_wstring(files.size()) : std::wstring();
        std::wstring tip = LevelChipTip(level);
        // A partial chip reacts to a click and nothing says how: say it.
        if (partial)
            tip = std::wstring(strings::kTipOn) + std::to_wstring(level.files) + strings::kTipOf + std::to_wstring(files.size()) +
                L" \u00B7 " + strings::kTipAddToAll +
                (tip.empty() ? L"" : L" \u00B7 " + tip);
        std::wstring parent = level.parent.empty() ? std::wstring() : TagDisplay(level.parent);
        chips.push_back({ TagDisplay(level.name), std::wstring(), partial, count, 0, 0, 0, std::move(tip), std::move(level), std::move(parent), 0 });
    }

    // The hover fades go by chip index, which now stands for another chip, unless the chips are the same.
    bool same = chips.size() == m_chips.size() && std::equal(chips.begin(), chips.end(), m_chips.begin(),
        [](const Chip &a, const Chip &b) { return a.name == b.name && a.parent == b.parent && a.partial == b.partial && a.count == b.count; });
    if (!same)
        m_hover.Reset(m_hWnd, kChipHoverTimer);
    m_chips = std::move(chips);

    MeasureChips();
    Invalidate();
}

std::vector<LevelChip> LevelChips(const std::vector<std::vector<std::string>> &fileTags)
{
    struct Entry
    {
        LevelChip chip;
        size_t depth;
        size_t lastFile;
        std::vector<std::string> prefixes;  // where the level sits when no tag ends in it (an old catalog's parents)
        size_t below = 0;      // how many files have a tag under this level
        size_t lastBelow = 0;
    };
    std::unordered_map<std::string, Entry> found;  // by folded level name
    for (size_t n = 0; n < fileTags.size(); n++)
    {
        for (const std::string &path : fileTags[n])
        {
            size_t depth = 0;
            for (size_t start = 0; start <= path.size(); depth++)
            {
                size_t slash = path.find('/', start);
                size_t end = slash == std::string::npos ? path.size() : slash;
                std::string level = path.substr(start, end - start);
                auto it = found.emplace(fold_case(level), Entry{ LevelChip{ level, {}, 0 }, depth, n + 1, {} }).first;
                Entry &entry = it->second;
                if (entry.lastFile != n + 1 || entry.chip.files == 0)
                {
                    entry.chip.files++;
                    entry.lastFile = n + 1;
                }
                entry.depth = std::min(entry.depth, depth);
                if (slash != std::string::npos && entry.lastBelow != n + 1)
                {
                    entry.below++;
                    entry.lastBelow = n + 1;
                }
                if (slash == std::string::npos)
                {
                    if (std::find(entry.chip.paths.begin(), entry.chip.paths.end(), path) == entry.chip.paths.end())
                        entry.chip.paths.push_back(path);
                    break;
                }
                if (std::find(entry.prefixes.begin(), entry.prefixes.end(), path.substr(0, end)) == entry.prefixes.end())
                    entry.prefixes.push_back(path.substr(0, end));
                start = slash + 1;
            }
        }
    }
    std::vector<Entry> entries;
    for (auto &item : found)
        entries.push_back(std::move(item.second));
    const size_t all = fileTags.size();
    std::sort(entries.begin(), entries.end(), [all](const Entry &a, const Entry &b)
    {
        if ((a.chip.files == all) != (b.chip.files == all))
            return a.chip.files == all;
        if (a.depth != b.depth)
            return a.depth < b.depth;
        return LessNoCase(Utf8ToWide(a.chip.name), Utf8ToWide(b.chip.name));
    });
    // The level just above a path, empty for a top-level one.
    auto parentOf = [](const std::string &path)
    {
        size_t last = path.rfind('/');
        if (last == std::string::npos || last == 0)
            return std::string();
        std::string prefix = path.substr(0, last);
        size_t before = prefix.rfind('/');
        return before == std::string::npos ? prefix : prefix.substr(before + 1);
    };
    // Where each level sits: the tags ending in it, else the parents of an old catalog.
    auto sourceOf = [](const Entry &entry) -> const std::vector<std::string> &
    {
        return entry.chip.paths.empty() ? entry.prefixes : entry.chip.paths;
    };
    // First the parent each chip can show: the same level above for all its paths, else none.
    for (Entry &entry : entries)
    {
        bool same = !sourceOf(entry).empty();
        std::string parent;
        for (const std::string &path : sourceOf(entry))
        {
            std::string above = parentOf(path);
            if (above.empty() || (!parent.empty() && fold_case(above) != fold_case(parent)))
                same = false;
            else if (parent.empty())
                parent = above;
        }
        if (same)
            entry.chip.parent = parent;
    }
    // A level every file has a tag under is left out, as its tags say it all, unless one of those chips could not show
    // it as its parent (Queen/Live and Metallica/Live: Live has two): then the level stays. Kept levels can need their
    // own parents kept, so go on until nothing changes.
    std::vector<bool> shown(entries.size());
    for (size_t i = 0; i < entries.size(); i++)
        shown[i] = entries[i].below != entries[i].chip.files;
    for (bool changed = true; changed;)
    {
        changed = false;
        for (size_t i = 0; i < entries.size(); i++)
        {
            if (!shown[i] || !entries[i].chip.parent.empty())
                continue;
            for (const std::string &path : sourceOf(entries[i]))
            {
                std::string above = fold_case(parentOf(path));
                for (size_t k = 0; !above.empty() && k < entries.size(); k++)
                {
                    if (!shown[k] && fold_case(entries[k].chip.name) == above)
                        shown[k] = changed = true;
                }
            }
        }
    }
    std::vector<LevelChip> chips;
    for (size_t i = 0; i < entries.size(); i++)
    {
        if (!shown[i])
            continue;
        Entry &entry = entries[i];
        if (entry.chip.paths.empty())
            entry.chip.paths = std::move(entry.prefixes);
        std::sort(entry.chip.paths.begin(), entry.chip.paths.end());
        chips.push_back(std::move(entry.chip));
    }
    return chips;
}

std::wstring TagDisplay(std::wstring name)
{
    std::replace(name.begin(), name.end(), L'_', L' ');
    return name;
}

std::wstring TagDisplay(const std::string &name)
{
    return TagDisplay(Utf8ToWide(name));
}

std::wstring LevelChipTip(const LevelChip &chip)
{
    if (chip.paths.size() == 1 && (chip.paths[0] == chip.name || !chip.parent.empty()))
        return std::wstring();
    std::wstring tip;
    for (const std::string &path : chip.paths)
        tip += (tip.empty() ? L"" : L" \u00B7 ") + TagDisplay(path);
    return tip;
}

std::string FilterByPaths(const std::string &query, const std::vector<std::string> &paths)
{
    if (paths.empty() || !std::all_of(paths.begin(), paths.end(), CanFilterBy))
        return query;
    if (paths.size() == 1)
    {
        bool on = TagStates(query)[fold_case(paths[0])] == TagState::kRequired;
        return WithTagState(query, paths[0], on ? TagState::kOff : TagState::kRequired);
    }
    std::string group = "{";
    for (const std::string &path : paths)
        group += (group.size() > 1 ? " OR " : "") + path;
    group += "}";
    if (query.find(group) != std::string::npos)
        return query;
    return query + (query.empty() ? "" : " ") + group;
}

std::wstring LeafTagName(const std::wstring &path)
{
    size_t slash = path.rfind(L'/');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// "Music/…/Disc_1": the root and as many of the last parts as fit, the rest of the middle left out.
std::wstring ShortTagPath(Renderer &renderer, const std::wstring &path, Font font, float maxWidth)
{
    if (renderer.TextWidth(path, font) <= maxWidth)
        return path;
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t slash; (slash = path.find(L'/', start)) != std::wstring::npos; start = slash + 1)
        parts.push_back(path.substr(start, slash - start));
    parts.push_back(path.substr(start));
    if (parts.size() < 3)
        return path;
    std::wstring best;
    for (size_t from = 2; from < parts.size(); from++)
    {
        best = parts[0] + L"/\u2026";
        for (size_t i = from; i < parts.size(); i++)
            best += L"/" + parts[i];
        if (renderer.TextWidth(best, font) <= maxWidth)
            return best;
    }
    return best;
}

bool TagPanel::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    bool onClose;
    int index = Visible() ? ChipAt(pt, onClose) : -1;
    if (index < 0 || m_chips[static_cast<size_t>(index)].tip.empty())
        return false;
    anchor = ChipRect(m_chips[static_cast<size_t>(index)]);
    text = m_chips[static_cast<size_t>(index)].tip.c_str();
    return true;
}

// ---- Chips ----

RECT TagPanel::AddRect() const
{
    RECT bar = Bar();
    int left = bar.left + Scale(kBarPadding) + Scale(kLabelWidth) + Scale(kGap);
    int top = (bar.top + bar.bottom - Scale(theme::kControlHeight)) / 2;
    int right = std::min<int>(left + Scale(kAddWidth), bar.right - Scale(kBarPadding));
    RECT r = { left, top, std::max<int>(left, right), top + Scale(theme::kControlHeight) };
    return r;
}

RECT TagPanel::ChipArea() const
{
    RECT bar = Bar();
    RECT area = { AddRect().right + Scale(kGap), bar.top, bar.right - Scale(kBarPadding), bar.bottom };
    if (area.right < area.left)
        area.right = area.left;
    return area;
}

RECT TagPanel::ChipRect(const Chip &chip) const
{
    RECT area = ChipArea();
    int left = area.left + chip.offset - m_chipScroll;
    int top = (area.top + area.bottom - Scale(kChipHeight)) / 2;
    RECT r = { left, top, left + chip.width, top + Scale(kChipHeight) };
    return r;
}

// The x takes the right end of the chip, from halfway between it and the name.
int TagPanel::CloseLeft(const Chip &chip) const
{
    return ChipRect(chip).left + Scale(kChipPadding) + chip.textWidth + Scale(kCloseGap) / 2;
}

// The chip under the point, or -1. 'onClose' tells whether the point is on its x.
int TagPanel::ChipAt(POINT pt, bool &onClose) const
{
    onClose = false;
    RECT area = ChipArea();
    if (!PtInRect(&area, pt))
        return -1;

    for (size_t i = 0; i < m_chips.size(); i++)
    {
        RECT r = ChipRect(m_chips[i]);
        if (PtInRect(&r, pt))
        {
            onClose = pt.x >= CloseLeft(m_chips[i]);
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TagPanel::MeasureChips()
{
    m_chipsWidth = 0;
    if (!m_renderer)
        return;

    int offset = 0;
    for (Chip &chip : m_chips)
    {
        chip.label = ShortTagPath(*m_renderer, chip.name, Font::kCaption, static_cast<float>(Scale(kMaxLabel)));
        chip.parentWidth = chip.parent.empty() ? 0 : static_cast<int>(m_renderer->TextWidth(chip.parent + L" / ", Font::kCaption) + 0.99f);
        float width = chip.parentWidth + m_renderer->TextWidth(chip.label, Font::kCaption);
        if (chip.partial)
            width += Scale(kCountGap) + m_renderer->TextWidth(chip.count, Font::kMono);
        chip.textWidth = static_cast<int>(width + 0.99f);
        chip.offset = offset;
        chip.width = Scale(kChipPadding) + chip.textWidth + Scale(kCloseGap) + Scale(kCloseSize) + Scale(kChipPadding);
        offset += chip.width + Scale(kChipGap);
    }
    m_chipsWidth = m_chips.empty() ? 0 : offset - Scale(kChipGap);

    ScrollChipsTo(m_chipScroll);
    OnHover(m_hoverPoint);
}

void TagPanel::ScrollChipsTo(int x)
{
    RECT area = ChipArea();
    int maxScroll = m_chipsWidth - (area.right - area.left);
    if (x > maxScroll)
        x = maxScroll;
    if (x < 0)
        x = 0;

    if (x != m_chipScroll)
    {
        m_chipScroll = x;
        OnHover(m_hoverPoint);
        Invalidate();
    }
}

void TagPanel::OnHover(POINT pt)
{
    if (!Visible())
        pt = { -1, -1 };
    m_hoverPoint = pt;
    bool onClose = false;
    int chip = ChipAt(pt, onClose);
    RECT add = AddRect();
    bool overAdd = m_selectedCount > 0 && PtInRect(&add, pt);
    // Each chip has two things that light up: itself (a partial chip is a button) and its x.
    if (m_hover.Set(m_hWnd, kChipHoverTimer, overAdd ? kAddHover : chip < 0 ? -1 : chip * 2 + (onClose ? 1 : 0)))
        Invalidate();
}

void TagPanel::OnTimer()
{
    m_hover.Step(m_hWnd, kChipHoverTimer);
    Invalidate();
}

// ---- Input ----

bool TagPanel::OnLButtonDown(POINT pt, TagEdit &edit, bool &addTag)
{
    addTag = false;
    if (!Contains(pt))
        return false;

    RECT add = AddRect();
    if (m_selectedCount > 0 && PtInRect(&add, pt))
    {
        addTag = true;
        return true;
    }

    bool onClose = false;
    int index = ChipAt(pt, onClose);
    if (index >= 0)
    {
        const Chip &chip = m_chips[index];
        if (onClose)
            edit = { TagEdit::kRemove, { chip.level.name } };
        else if (chip.partial)
            edit = { TagEdit::kAdd, chip.level.paths };
        else
            edit = { TagEdit::kFilter, chip.level.paths };
    }
    return true;
}

bool TagPanel::OnMouseWheel(POINT pt, int delta)
{
    if (!Contains(pt))
        return false;
    ScrollChipsTo(m_chipScroll - MulDiv(delta, Scale(kWheelStep), WHEEL_DELTA));
    return true;
}

// ---- Painting ----

void TagPanel::Paint(Renderer &renderer) const
{
    if (!Visible())
        return;
    float line = Scale(4) / 4.0f;
    RECT bar = Bar();
    float opacity = m_level;

    // A soft shadow under the bar (a few layers growing outward and fading), then the bar and its thin edge.
    D2D1_RECT_F box = ToRectF(bar);
    const float barRadius = static_cast<float>(Scale(8));
    for (int i = 4; i >= 1; i--)
    {
        float spread = static_cast<float>(Scale(i * 3));
        renderer.FillRoundedRect(D2D1::RectF(box.left - spread, box.top - spread + Scale(4), box.right + spread, box.bottom + spread + Scale(4)),
            barRadius + spread, D2D1_COLOR_F{ 0.0f, 0.0f, 0.0f, 0.07f * opacity });
    }
    renderer.FillRoundedRect(box, barRadius, Faded(theme::kControl, opacity));
    renderer.DrawRoundedRect(D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2),
        barRadius, Faded(theme::kPlate, opacity), line);

    std::wstring label = m_selectedCount == 1 ? strings::kOneFile : std::to_wstring(m_selectedCount) + L" " + strings::kFiles;
    int left = bar.left + Scale(kBarPadding);
    RECT labelRect = { left, bar.top, left + Scale(kLabelWidth), bar.bottom };
    renderer.DrawTextLine(label, Font::kMono, labelRect, Faded(theme::kText3, opacity), DWRITE_TEXT_ALIGNMENT_LEADING);

    RECT add = AddRect();
    if (add.right > add.left)
    {
        D2D1_RECT_F button = ToRectF(add);
        const float buttonRadius = static_cast<float>(Scale(theme::kChipRadius));
        renderer.FillRoundedRect(button, buttonRadius, Faded(theme::kChip, opacity));
        float level = m_hover.Level(kAddHover);
        if (level > 0.0f)
            renderer.FillRoundedRect(button, buttonRadius, Faded(theme::kHover, level * opacity));
        renderer.DrawTextLine(strings::kTagStripAdd, Font::kCaption, add, Faded(theme::kText1, opacity), DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    RECT area = ChipArea();
    if (m_chips.empty() || area.right <= area.left)
        return;

    renderer.PushClip(area);
    const float radius = static_cast<float>(Scale(theme::kChipRadius));

    for (size_t i = 0; i < m_chips.size(); i++)
    {
        const Chip &chip = m_chips[i];
        RECT r = ChipRect(chip);
        if (r.right <= area.left || r.left >= area.right)
            continue;

        float hover = m_hover.Level(static_cast<int>(i) * 2);
        float closeHover = m_hover.Level(static_cast<int>(i) * 2 + 1);
        D2D1_RECT_F box = ToRectF(r);
        if (chip.partial)
        {
            // Clicking a partial chip adds the tag to the rest of the files, so it reacts like a button.
            if (hover > 0.0f)
                renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover * opacity));
            D2D1_RECT_F outline = D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2);
            renderer.DrawRoundedRect(outline, radius, Faded(theme::kText3, opacity), line, true);
        }
        else
        {
            renderer.FillRoundedRect(box, radius, Faded(theme::kChip, opacity));
        }

        // Up to the x: the measured width can come out a fraction short once drawn.
        RECT text = { r.left + Scale(kChipPadding), r.top, r.right - Scale(kChipPadding) - Scale(kCloseSize), r.bottom };
        if (chip.parentWidth > 0)
        {
            renderer.DrawTextLine(chip.parent + L" / ", Font::kCaption, text, Faded(theme::kText3, opacity));
            text.left += chip.parentWidth;
        }
        renderer.DrawTextLine(chip.label, Font::kCaption, text, Faded(theme::kText1, opacity));
        if (chip.partial)
        {
            text.left += static_cast<int>(renderer.TextWidth(chip.label, Font::kCaption) + 0.5f) + Scale(kCountGap);
            renderer.DrawTextLine(chip.count, Font::kMono, text, Faded(theme::kText3, opacity));
        }

        // The x, with a soft square behind it under the pointer.
        float size = static_cast<float>(Scale(kCloseSize));
        float cx = r.right - Scale(kChipPadding) - size / 2.0f;
        float cy = (r.top + r.bottom) / 2.0f;
        if (closeHover > 0.0f)
        {
            float pad = static_cast<float>(Scale(3));
            renderer.FillRoundedRect(D2D1::RectF(cx - size / 2 - pad, cy - size / 2 - pad, cx + size / 2 + pad, cy + size / 2 + pad),
                static_cast<float>(Scale(2)), Faded(theme::kHover, closeHover * opacity));
        }
        D2D1_COLOR_F color = Faded(Mix(theme::kText2, theme::kText1, closeHover), opacity);
        float h = size / 2.0f;
        renderer.DrawLine(D2D1::Point2F(cx - h, cy - h), D2D1::Point2F(cx + h, cy + h), color, 1.2f * line);
        renderer.DrawLine(D2D1::Point2F(cx - h, cy + h), D2D1::Point2F(cx + h, cy - h), color, 1.2f * line);
    }

    renderer.PopClip();
}
