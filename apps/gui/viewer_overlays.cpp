#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include "strings.hpp"
#include "tag_panel.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    // The tag chips of the tag row: their height, the room between them, inside them left of the name and right of
    // the x, between the name and the x, and the x itself.
    const int kChipHeight = 22;
    const int kChipGap = 6;
    const int kChipPadding = 8;
    const int kCloseGap = 6;
    const int kCloseSize = 7;
    // The tag field, at least.
    const int kFieldWidth = 140;
}

// The chips go by index, so their hover fades start over when the tags change.
void Viewer::RefreshTags()
{
    std::vector<std::string> tags;
    bool inLibrary = m_open && m_tagsOf && m_tagsOf(m_paths[m_index], tags);
    std::sort(tags.begin(), tags.end(), [](const std::string &a, const std::string &b)
        { return LessNoCase(Utf8ToWide(a), Utf8ToWide(b)); });
    if (inLibrary == m_inLibrary && tags == m_tags)
        return;
    m_inLibrary = inLibrary;
    m_tags = std::move(tags);
    if (!m_inLibrary)
        m_tagRow = false;
    if (m_hover.Current() >= kTagChip || m_hover.Previous() >= kTagChip)
        m_hover.Reset(m_hWnd, kViewerHoverTimer);
    Invalidate();
}

RECT Viewer::ButtonRect(int button) const
{
    int size = Scale(theme::kViewerButton);
    int margin = Scale(theme::kViewerMargin);
    int top = (m_bounds.top + m_bounds.bottom - size) / 2;
    switch (button)
    {
    case kCloseButton:
        return { m_bounds.right - margin - size, m_bounds.top + margin, m_bounds.right - margin, m_bounds.top + margin + size };
    case kPreviousButton:
        return { m_bounds.left + margin, top, m_bounds.left + margin + size, top + size };
    case kNextButton:
        return { m_bounds.right - margin - size, top, m_bounds.right - margin, top + size };
    }

    Controls c = LayoutControls();
    switch (button)
    {
    case kPlayButton:       return c.play;
    case kMuteButton:       return c.mute;
    case kSubtitleButton:   return c.subtitles;
    case kAudioButton:      return c.audio;
    case kSpeedButton:      return c.speed;
    case kFullscreenButton: return c.fullscreen;
    case kFitButton:        return LayoutPageBar().fit;
    case kDirectionButton:  return LayoutPageBar().direction;
    }

    RECT menu = MenuRect();
    int row = Scale(theme::kViewerMenuRow);
    int item = button - kMenuItem;
    int itemTop = menu.top + Scale(4) + item * row;
    return { menu.left, itemTop, menu.right, itemTop + row };
}

bool Viewer::ButtonShown(int button) const
{
    switch (button)
    {
    case kCloseButton:
        return true;
    case kPreviousButton:
    case kNextButton:
        return m_book ? m_pageCount > 1 : m_paths.size() > 1;
    case kFitButton:
        return PageBarShown() && !m_reflow;
    case kDirectionButton:
        return PageBarShown();
    case kPlayButton:
    case kMuteButton:
        return Playing();
    case kSpeedButton:
    case kFullscreenButton:
        return VideoShown();
    case kSubtitleButton:
        return VideoShown() && !m_video.SubtitleTracks().empty();
    case kAudioButton:
        return VideoShown() && m_video.AudioTracks().size() > 1;
    }
    return VideoShown() && m_menu != Menu::kNone && button - kMenuItem < static_cast<int>(MenuItems().size());
}

bool Viewer::ButtonEnabled(int button) const
{
    // In a book they turn the page that way.
    if (m_book && (button == kPreviousButton || button == kNextButton))
    {
        long long target = static_cast<long long>(m_page) + SideStep(button == kPreviousButton ? -1 : 1);
        return target >= 0 && target < static_cast<long long>(m_pageCount);
    }
    if (button == kPreviousButton)
        return m_index > 0;
    if (button == kNextButton)
        return m_index + 1 < m_paths.size();
    return true;
}

// Hidden buttons can't be clicked. The open menu covers what is under it.
int Viewer::ButtonAt(POINT pt) const
{
    if (!m_open || m_overlayTo == 0.0f)
        return -1;
    if (m_menu != Menu::kNone)
    {
        int count = static_cast<int>(MenuItems().size());
        for (int item = 0; item < count; item++)
        {
            RECT r = ButtonRect(kMenuItem + item);
            if (PtInRect(&r, pt))
                return kMenuItem + item;
        }
    }
    for (int button = 0; button < kButtonCount; button++)
    {
        RECT r = ButtonRect(button);
        if (ButtonShown(button) && PtInRect(&r, pt))
            return button;
    }
    return TagButtonAt(pt);
}

void Viewer::PaintButton(Renderer &renderer, int button, float level) const
{
    D2D1_RECT_F rect = ToRectF(ButtonRect(button));
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    bool enabled = ButtonEnabled(button);
    renderer.FillRoundedRect(rect, radius, Faded(theme::kOverlay, level));
    float hover = enabled ? m_hover.Level(button) : 0.0f;
    if (hover > 0.0f)
        renderer.FillRoundedRect(rect, radius, Faded(theme::kOverlayHover, level * hover));

    D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
    float s = static_cast<float>(Scale(5));
    float width = Scale(3) / 2.0f;  // 1.5 pixels at 96 DPI
    D2D1_COLOR_F color = Faded(enabled ? theme::kText1 : theme::kText3, level);
    if (button == kCloseButton)
    {
        renderer.DrawLine(D2D1::Point2F(c.x - s, c.y - s), D2D1::Point2F(c.x + s, c.y + s), color, width);
        renderer.DrawLine(D2D1::Point2F(c.x - s, c.y + s), D2D1::Point2F(c.x + s, c.y - s), color, width);
    }
    else
    {
        // A chevron pointing the way it steps.
        float side = button == kPreviousButton ? 1.0f : -1.0f;
        const D2D1_POINT_2F points[] = { { c.x + side * s / 2, c.y - s }, { c.x - side * s / 2, c.y }, { c.x + side * s / 2, c.y + s } };
        renderer.DrawPolyline(points, 3, color, width);
    }
}

void Viewer::OpenTagRow(bool open)
{
    if (open == m_tagRow)
        return;
    m_tagRow = open;
    m_tagText.clear();
    m_tagSuggestion.clear();
    m_hover.Reset(m_hWnd, kViewerHoverTimer);
    if (open)
        SetTimer(m_hWnd, kViewerTagTimer, kTagIdleMs, nullptr);
    else
        KillTimer(m_hWnd, kViewerTagTimer);
    ShowOverlays();
    Invalidate();
}

void Viewer::ClearTagText()
{
    m_tagText.clear();
    TagTyped();
}

void Viewer::OnChar(wchar_t ch)
{
    // Enter, Tab and Backspace come as keys (OnKeyDown); spaces don't pile up.
    if (!TagRowOpen() || ch < L' ' || (ch == L' ' && (m_tagText.empty() || m_tagText.back() == L' ')))
        return;
    m_tagText += ch;
    TagTyped();
}

// The word being typed completes from the library's tags, as in the tag sheet.
void Viewer::TagTyped()
{
    size_t space = m_tagText.find_last_of(L' ');
    std::wstring word = space == std::wstring::npos ? m_tagText : m_tagText.substr(space + 1);
    m_tagSuggestion = !word.empty() && m_completeTag ? m_completeTag(word) : std::wstring();
    SetTimer(m_hWnd, kViewerTagTimer, kTagIdleMs, nullptr);
    ShowOverlays();
    Invalidate();
}

// Above the bar (or the bottom edge for a picture), in the middle: the chips, as many as fit before the field,
// then "+3" for the rest.
Viewer::TagRow Viewer::LayoutTagRow(Renderer &renderer) const
{
    TagRow row;
    int margin = Scale(theme::kViewerMargin);
    int room = Width(m_bounds) - 2 * margin;
    int fieldWidth = Scale(kFieldWidth);
    float typed = renderer.TextWidth(m_tagText + m_tagSuggestion, Font::kCaption);
    fieldWidth = std::max(fieldWidth, static_cast<int>(std::ceil(typed)) + 2 * Scale(kChipPadding) + Scale(2));
    fieldWidth = std::min(fieldWidth, room);
    if (!m_tagRow || !m_inLibrary || fieldWidth <= 0)
        return row;

    std::vector<int> chipWidths;
    for (const std::string &name : m_tags)
    {
        int text = static_cast<int>(std::ceil(renderer.TextWidth(LeafTagName(Utf8ToWide(name)), Font::kCaption)));
        chipWidths.push_back(Scale(kChipPadding) + text + Scale(kCloseGap) + Scale(kCloseSize) + Scale(kChipPadding));
    }
    auto rowWidth = [&](size_t count)
    {
        int total = fieldWidth;
        for (size_t i = 0; i < count; i++)
            total += chipWidths[i] + Scale(kChipGap);
        if (count < m_tags.size())
            total += static_cast<int>(std::ceil(renderer.TextWidth(L"+" + std::to_wstring(m_tags.size() - count), Font::kMono))) +
                Scale(kChipGap);
        return total;
    };
    size_t shown = m_tags.size();
    while (shown > 0 && rowWidth(shown) > room)
        shown--;

    int bottom = m_bounds.bottom - margin;
    if (Playing())
        bottom = LayoutControls().bar.top - Scale(theme::kViewerBarGap);
    else if (PageBarShown())
        bottom = LayoutPageBar().bar.top - Scale(theme::kViewerBarGap);
    int top = bottom - Scale(kChipHeight);
    int width = rowWidth(shown);
    LONG x = (m_bounds.left + m_bounds.right - width) / 2;
    row.row = { x, top, x + width, bottom };
    row.shown = true;
    for (size_t i = 0; i < shown; i++)
    {
        Chip chip;
        chip.rect = { x, top, x + chipWidths[i], bottom };
        chip.textWidth = chipWidths[i] - 2 * Scale(kChipPadding) - Scale(kCloseGap) - Scale(kCloseSize);
        row.chips.push_back(chip);
        x += chipWidths[i] + Scale(kChipGap);
    }
    if (shown < m_tags.size())
    {
        row.more = L"+" + std::to_wstring(m_tags.size() - shown);
        LONG moreWidth = static_cast<LONG>(std::ceil(renderer.TextWidth(row.more, Font::kMono)));
        row.moreRect = { x, top, x + moreWidth, bottom };
        x += moreWidth + Scale(kChipGap);
    }
    row.field = { x, top, x + fieldWidth, bottom };
    return row;
}

// A chip's x: it takes the chip's right end, from halfway between it and the name.
int Viewer::TagButtonAt(POINT pt) const
{
    if (!m_renderer || !m_tagRow || (VideoShown() && m_menu != Menu::kNone))
        return -1;
    TagRow row = LayoutTagRow(*m_renderer);
    for (size_t i = 0; i < row.chips.size(); i++)
    {
        RECT close = row.chips[i].rect;
        close.left += Scale(kChipPadding) + row.chips[i].textWidth + Scale(kCloseGap) / 2;
        if (PtInRect(&close, pt))
            return kTagChip + static_cast<int>(i);
    }
    return -1;
}

RECT Viewer::TagButtonRect(int button) const
{
    if (!m_renderer)
        return {};
    TagRow row = LayoutTagRow(*m_renderer);
    size_t index = static_cast<size_t>(button - kTagChip);
    return index < row.chips.size() ? row.chips[index].rect : RECT{};
}

// The tag chips, as in the strip under the grid: the name and an x, with a soft square behind the x under the
// pointer; then the field, outlined as the one with the keys, with its caret after the text.
void Viewer::PaintTagRow(Renderer &renderer, float level) const
{
    TagRow row = LayoutTagRow(renderer);
    if (!row.shown)
        return;
    float line = Scale(4) / 4.0f;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    for (size_t i = 0; i < row.chips.size(); i++)
    {
        const Chip &chip = row.chips[i];
        const RECT &r = chip.rect;
        renderer.FillRoundedRect(ToRectF(r), radius, Faded(theme::kChip, level));
        RECT text = { r.left + Scale(kChipPadding), r.top, r.left + Scale(kChipPadding) + chip.textWidth + 1, r.bottom };
        renderer.DrawTextLine(LeafTagName(Utf8ToWide(m_tags[i])), Font::kCaption, text, Faded(theme::kText1, level));

        float hover = m_hover.Level(kTagChip + static_cast<int>(i));
        float size = static_cast<float>(Scale(kCloseSize));
        float cx = r.right - Scale(kChipPadding) - size / 2.0f;
        float cy = (r.top + r.bottom) / 2.0f;
        if (hover > 0.0f)
        {
            float pad = static_cast<float>(Scale(3));
            renderer.FillRoundedRect(D2D1::RectF(cx - size / 2 - pad, cy - size / 2 - pad, cx + size / 2 + pad, cy + size / 2 + pad),
                static_cast<float>(Scale(2)), Faded(theme::kOverlayHover, level * hover));
        }
        D2D1_COLOR_F color = Faded(Mix(theme::kText2, theme::kText1, hover), level);
        float h = size / 2.0f;
        renderer.DrawLine(D2D1::Point2F(cx - h, cy - h), D2D1::Point2F(cx + h, cy + h), color, 1.2f * line);
        renderer.DrawLine(D2D1::Point2F(cx - h, cy + h), D2D1::Point2F(cx + h, cy - h), color, 1.2f * line);
    }
    if (!row.more.empty())
        renderer.DrawTextLine(row.more, Font::kMono, row.moreRect, Faded(theme::kText3, level));

    D2D1_RECT_F field = ToRectF(row.field);
    renderer.FillRoundedRect(field, radius, Faded(theme::kControl, level));
    D2D1_RECT_F edge = D2D1::RectF(field.left + line / 2, field.top + line / 2, field.right - line / 2, field.bottom - line / 2);
    renderer.DrawRoundedRect(edge, radius, Faded(theme::kYuri, level), line);
    RECT text = { row.field.left + Scale(kChipPadding), row.field.top, row.field.right - Scale(kChipPadding), row.field.bottom };
    float x = static_cast<float>(text.left);
    if (m_tagText.empty())
        renderer.DrawTextLine(strings::kViewerTagField, Font::kCaption, text, Faded(theme::kText3, level));
    else
    {
        renderer.DrawTextLine(m_tagText, Font::kCaption, text, Faded(theme::kText1, level));
        x += renderer.TextWidth(m_tagText, Font::kCaption);
        RECT rest = { static_cast<LONG>(x), text.top, text.right, text.bottom };
        if (!m_tagSuggestion.empty() && rest.left < rest.right)
            renderer.DrawTextLine(m_tagSuggestion, Font::kCaption, rest, Faded(theme::kText3, level));
    }
    // A steady caret: over a video there is no system caret to blink.
    float caretHeight = renderer.LineHeight(Font::kCaption);
    float middle = (field.top + field.bottom) / 2.0f;
    x = std::min(std::floor(x) + 0.5f, field.right - Scale(kChipPadding));
    renderer.DrawLine(D2D1::Point2F(x, middle - caretHeight / 2), D2D1::Point2F(x, middle + caretHeight / 2),
        Faded(theme::kText1, level), line);
}

bool Viewer::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    int button = ButtonAt(pt);
    if (button < 0 && m_renderer && m_tagRow && m_menu == Menu::kNone && m_overlayTo > 0.0f)
    {
        // Over the name of a chip: the whole path of the tag.
        TagRow row = LayoutTagRow(*m_renderer);
        for (size_t i = 0; i < row.chips.size() && i < m_tags.size(); i++)
        {
            if (!PtInRect(&row.chips[i].rect, pt))
                continue;
            m_tipText = Utf8ToWide(m_tags[i]);
            if (m_tipText == LeafTagName(m_tipText))
                return false;
            anchor = row.chips[i].rect;
            text = m_tipText.c_str();
            return true;
        }
    }
    switch (button)
    {
    case kCloseButton:      text = strings::kTipViewerClose; break;
    case kPreviousButton:   text = strings::kTipViewerPrevious; break;
    case kNextButton:       text = strings::kTipViewerNext; break;
    case kMuteButton:       text = strings::kTipViewerMute; break;
    case kSpeedButton:      text = strings::kTipViewerSpeed; break;
    case kFullscreenButton: text = strings::kTipViewerFullscreen; break;
    default:
        if (button < kTagChip)
            return false;
        text = strings::kTipViewerRemoveTag;
        break;
    }
    anchor = button >= kTagChip ? TagButtonRect(button) : ButtonRect(button);
    return true;
}
