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
    m_selectedCount = files.size();

    // How many of the selected files have each tag.
    std::unordered_map<const tag*, size_t> counts;
    for (const file *f : files)
    {
        for (const tag *t : f->get_tags())
            counts[t]++;
    }

    std::vector<Chip> chips;
    for (const auto &entry : counts)
    {
        bool partial = entry.second < files.size();
        std::wstring count = partial ? std::to_wstring(entry.second) + L"/" + std::to_wstring(files.size()) : std::wstring();
        chips.push_back({ Utf8ToWide(entry.first->get_id()), std::wstring(), partial, count, 0, 0, 0 });
    }

    // The tags every selected file has come first.
    std::sort(chips.begin(), chips.end(), [](const Chip &a, const Chip &b)
    {
        if (a.partial != b.partial)
            return !a.partial;
        return LessNoCase(a.name, b.name);
    });

    // The hover fades go by chip index, which now stands for another chip, unless the chips are the same.
    bool same = chips.size() == m_chips.size() && std::equal(chips.begin(), chips.end(), m_chips.begin(),
        [](const Chip &a, const Chip &b) { return a.name == b.name && a.partial == b.partial && a.count == b.count; });
    if (!same)
        m_hover.Reset(m_hWnd, kChipHoverTimer);
    m_chips = std::move(chips);

    MeasureChips();
    Invalidate();
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
    int index = ChipAt(pt, onClose);
    if (index < 0 || m_chips[static_cast<size_t>(index)].label == m_chips[static_cast<size_t>(index)].name)
        return false;
    anchor = ChipRect(m_chips[static_cast<size_t>(index)]);
    text = m_chips[static_cast<size_t>(index)].name.c_str();
    return true;
}

// ---- Chips ----

RECT TagPanel::AddRect() const
{
    int left = m_bounds.left + Scale(theme::kPageMargin) + Scale(kLabelWidth) + Scale(kGap);
    int top = (m_bounds.top + m_bounds.bottom - Scale(theme::kControlHeight)) / 2;
    int right = std::min<int>(left + Scale(kAddWidth), m_bounds.right - Scale(theme::kPageMargin));
    RECT r = { left, top, std::max<int>(left, right), top + Scale(theme::kControlHeight) };
    return r;
}

RECT TagPanel::ChipArea() const
{
    RECT area = { AddRect().right + Scale(kGap), m_bounds.top, m_bounds.right - Scale(theme::kPageMargin), m_bounds.bottom };
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
        float width = m_renderer->TextWidth(chip.label, Font::kCaption);
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
            edit = { TagEdit::kRemove, { WideToUtf8(chip.name) } };
        else if (chip.partial)
            edit = { TagEdit::kAdd, { WideToUtf8(chip.name) } };
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
    float line = Scale(4) / 4.0f;
    renderer.FillRect(ToRectF(m_bounds), theme::kBase);
    renderer.FillRect(D2D1::RectF(static_cast<float>(m_bounds.left), static_cast<float>(m_bounds.top),
        static_cast<float>(m_bounds.right), m_bounds.top + line), theme::kStroke);

    if (m_selectedCount == 0)
    {
        RECT hint = { m_bounds.left + Scale(theme::kPageMargin), m_bounds.top, m_bounds.right - Scale(theme::kPageMargin), m_bounds.bottom };
        renderer.DrawTextLine(strings::kTagNoSelection, Font::kBody, hint, theme::kText3);
        return;
    }

    std::wstring label = m_selectedCount == 1 ? strings::kOneFile : std::to_wstring(m_selectedCount) + L" " + strings::kFiles;
    int left = m_bounds.left + Scale(theme::kPageMargin);
    RECT labelRect = { left, m_bounds.top, left + Scale(kLabelWidth), m_bounds.bottom };
    renderer.DrawTextLine(label, Font::kMono, labelRect, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);

    RECT add = AddRect();
    if (add.right > add.left)
    {
        D2D1_RECT_F button = ToRectF(add);
        const float buttonRadius = static_cast<float>(Scale(theme::kChipRadius));
        renderer.FillRoundedRect(button, buttonRadius, theme::kControl);
        float level = m_hover.Level(kAddHover);
        if (level > 0.0f)
            renderer.FillRoundedRect(button, buttonRadius, Faded(theme::kHover, level));
        renderer.DrawTextLine(strings::kTagStripAdd, Font::kCaption, add, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
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
                renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));
            D2D1_RECT_F outline = D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2);
            renderer.DrawRoundedRect(outline, radius, theme::kText3, line, true);
        }
        else
        {
            renderer.FillRoundedRect(box, radius, theme::kChip);
        }

        // Up to the x: the measured width can come out a fraction short once drawn.
        RECT text = { r.left + Scale(kChipPadding), r.top, r.right - Scale(kChipPadding) - Scale(kCloseSize), r.bottom };
        renderer.DrawTextLine(chip.label, Font::kCaption, text, theme::kText1);
        if (chip.partial)
        {
            text.left += static_cast<int>(renderer.TextWidth(chip.label, Font::kCaption) + 0.5f) + Scale(kCountGap);
            renderer.DrawTextLine(chip.count, Font::kMono, text, theme::kText3);
        }

        // The x, with a soft square behind it under the pointer.
        float size = static_cast<float>(Scale(kCloseSize));
        float cx = r.right - Scale(kChipPadding) - size / 2.0f;
        float cy = (r.top + r.bottom) / 2.0f;
        if (closeHover > 0.0f)
        {
            float pad = static_cast<float>(Scale(3));
            renderer.FillRoundedRect(D2D1::RectF(cx - size / 2 - pad, cy - size / 2 - pad, cx + size / 2 + pad, cy + size / 2 + pad),
                static_cast<float>(Scale(2)), Faded(theme::kHover, closeHover));
        }
        D2D1_COLOR_F color = Mix(theme::kText2, theme::kText1, closeHover);
        float h = size / 2.0f;
        renderer.DrawLine(D2D1::Point2F(cx - h, cy - h), D2D1::Point2F(cx + h, cy + h), color, 1.2f * line);
        renderer.DrawLine(D2D1::Point2F(cx - h, cy + h), D2D1::Point2F(cx + h, cy - h), color, 1.2f * line);
    }

    renderer.PopClip();
}
