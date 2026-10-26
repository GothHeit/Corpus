#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    // The tag chips under the name: their height, the room between them, inside them left of the name and right of
    // the x, between the name and the x, and the x itself.
    const int kChipHeight = 22;
    const int kChipGap = 6;
    const int kChipPadding = 8;
    const int kCloseGap = 6;
    const int kCloseSize = 7;
    // Under the chips, to the panel's edge.
    const int kChipRowBottom = 8;

    // "photo.jpg" for C:\Pictures\photo.jpg.
    std::wstring FileName(const std::string &path)
    {
        std::wstring wide = Utf8ToWide(path);
        size_t slash = wide.find_last_of(L"\\/");
        return slash == std::wstring::npos ? wide : wide.substr(slash + 1);
    }
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
    if (m_hover.Current() >= kAddTagButton || m_hover.Previous() >= kAddTagButton)
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
    case kFullscreenButton: return c.fullscreen;
    case kFitButton:        return LayoutPageBar().fit;
    case kDirectionButton:  return LayoutPageBar().direction;
    }

    // A menu item.
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

// The name at the bottom, with where it is in the list, its size and the zoom: "3 of 20 · 1920 × 1080 · 50%". A
// picture too big to keep at full size says it was reduced, so 100% isn't taken for its own pixels. Over a video,
// it sits above the control bar.
Viewer::Info Viewer::LayoutInfo(Renderer &renderer) const
{
    Info info;
    info.name = FileName(m_paths[m_index]);
    const std::wstring separator = L" \u00B7 ";
    if (m_paths.size() > 1)
        info.detail = FormatCount(m_index + 1) + L" " + strings::kOf + L" " + FormatCount(m_paths.size());
    if (IsVideo())
    {
        if (m_video.Width() > 0 && m_video.Height() > 0)
        {
            if (!info.detail.empty())
                info.detail += separator;
            info.detail += std::to_wstring(m_video.Width()) + L" \u00D7 " + std::to_wstring(m_video.Height());
        }
    }
    // A book's page is drawn at a size of the reader's choosing, so its pixels and the zoom say nothing; the page
    // bar says where it is.
    else if (m_status == ImageDecoder::Status::kFrame && !m_book)
    {
        if (!info.detail.empty())
            info.detail += separator;
        info.detail += std::to_wstring(m_frame.sourceWidth) + L" \u00D7 " + std::to_wstring(m_frame.sourceHeight) + separator;
        if (m_frame.width < m_frame.sourceWidth || m_frame.height < m_frame.sourceHeight)
            info.detail += strings::kReduced + separator;
        info.detail += std::to_wstring(static_cast<int>(std::lround(Zoom() * 100))) + L"%";
    }

    float pad = static_cast<float>(Scale(12));
    float gap = static_cast<float>(Scale(10));
    float margin = static_cast<float>(Scale(theme::kViewerMargin));
    float room = (m_bounds.right - m_bounds.left) - 2 * margin - 2 * pad;
    float detailWidth = info.detail.empty() ? 0.0f : renderer.TextWidth(info.detail, Font::kMono);
    float nameRoom = room - (info.detail.empty() ? 0.0f : gap + detailWidth);
    // A narrow window keeps the name.
    if (nameRoom < Scale(48))
    {
        info.detail.clear();
        detailWidth = 0.0f;
        nameRoom = room;
    }
    if (nameRoom <= 0.0f)
        return info;
    info.nameWidth = std::min(std::ceil(renderer.TextWidth(info.name, Font::kBody)), nameRoom);
    float width = 2 * pad + info.nameWidth + (info.detail.empty() ? 0.0f : gap + detailWidth);

    // The tags, as many as fit before the add button, then "+3" for the rest.
    std::vector<int> chipWidths;
    int chipsWidth = 0;
    size_t shown = 0;
    int addWidth = static_cast<int>(std::ceil(renderer.TextWidth(strings::kTagStripAdd, Font::kCaption))) + 2 * Scale(kChipPadding);
    if (m_inLibrary && addWidth <= room)
    {
        info.tagRow = true;
        for (const std::string &name : m_tags)
        {
            int text = static_cast<int>(std::ceil(renderer.TextWidth(Utf8ToWide(name), Font::kCaption)));
            chipWidths.push_back(Scale(kChipPadding) + text + Scale(kCloseGap) + Scale(kCloseSize) + Scale(kChipPadding));
        }
        auto rowWidth = [&](size_t count)
        {
            int total = addWidth;
            for (size_t i = 0; i < count; i++)
                total += chipWidths[i] + Scale(kChipGap);
            if (count < m_tags.size())
                total += static_cast<int>(std::ceil(renderer.TextWidth(L"+" + std::to_wstring(m_tags.size() - count), Font::kMono))) +
                    Scale(kChipGap);
            return total;
        };
        shown = m_tags.size();
        while (shown > 0 && rowWidth(shown) > room)
            shown--;
        chipsWidth = rowWidth(shown);
        width = std::max(width, 2 * pad + chipsWidth);
    }

    float middle = (m_bounds.left + m_bounds.right) / 2.0f;
    float bottom = m_bounds.bottom - margin;
    if (VideoShown())
        bottom = static_cast<float>(LayoutControls().bar.top - Scale(theme::kViewerBarGap));
    else if (PageBarShown())
        bottom = static_cast<float>(LayoutPageBar().bar.top - Scale(theme::kViewerBarGap));
    int height = Scale(theme::kViewerInfoHeight) + (info.tagRow ? Scale(kChipHeight) + Scale(kChipRowBottom) : 0);
    info.panel = D2D1::RectF(std::floor(middle - width / 2), bottom - height, std::floor(middle - width / 2) + std::ceil(width), bottom);
    info.shown = true;

    if (info.tagRow)
    {
        LONG x = static_cast<LONG>(info.panel.left + pad);
        LONG top = static_cast<LONG>(bottom) - Scale(kChipRowBottom) - Scale(kChipHeight);
        LONG chipBottom = top + Scale(kChipHeight);
        for (size_t i = 0; i < shown; i++)
        {
            Chip chip;
            chip.rect = { x, top, x + chipWidths[i], chipBottom };
            chip.textWidth = chipWidths[i] - 2 * Scale(kChipPadding) - Scale(kCloseGap) - Scale(kCloseSize);
            info.chips.push_back(chip);
            x += chipWidths[i] + Scale(kChipGap);
        }
        if (shown < m_tags.size())
        {
            info.more = L"+" + std::to_wstring(m_tags.size() - shown);
            LONG moreWidth = static_cast<LONG>(std::ceil(renderer.TextWidth(info.more, Font::kMono)));
            info.moreRect = { x, top, x + moreWidth, chipBottom };
            x += moreWidth + Scale(kChipGap);
        }
        info.add = { x, top, x + addWidth, chipBottom };
    }
    return info;
}

// The add button, or the x of a chip: it takes the chip's right end, from halfway between it and the name.
int Viewer::TagButtonAt(POINT pt) const
{
    // Faded out, the chips can't be seen, so a click there takes no tag off.
    if (!m_renderer || OverlayLevel() == 0.0f || (VideoShown() && m_menu != Menu::kNone))
        return -1;
    Info info = LayoutInfo(*m_renderer);
    if (!info.tagRow)
        return -1;
    if (PtInRect(&info.add, pt))
        return kAddTagButton;
    for (size_t i = 0; i < info.chips.size(); i++)
    {
        RECT close = info.chips[i].rect;
        close.left += Scale(kChipPadding) + info.chips[i].textWidth + Scale(kCloseGap) / 2;
        if (PtInRect(&close, pt))
            return kTagChip + static_cast<int>(i);
    }
    return -1;
}

RECT Viewer::TagButtonRect(int button) const
{
    if (!m_renderer)
        return {};
    Info info = LayoutInfo(*m_renderer);
    if (button == kAddTagButton)
        return info.add;
    size_t index = static_cast<size_t>(button - kTagChip);
    return index < info.chips.size() ? info.chips[index].rect : RECT{};
}

void Viewer::PaintInfo(Renderer &renderer, float level) const
{
    Info info = LayoutInfo(renderer);
    if (!info.shown)
        return;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(info.panel, radius, Faded(theme::kOverlay, level));

    LONG left = static_cast<LONG>(info.panel.left + Scale(12));
    LONG top = static_cast<LONG>(info.panel.top);
    RECT nameRect = { left, top, left + static_cast<LONG>(info.nameWidth) + 1, top + Scale(theme::kViewerInfoHeight) };
    renderer.DrawTextLine(info.name, Font::kBody, nameRect, Faded(theme::kText1, level));
    if (!info.detail.empty())
    {
        RECT detailRect = { nameRect.right + Scale(10), nameRect.top, static_cast<LONG>(info.panel.right), nameRect.bottom };
        renderer.DrawTextLine(info.detail, Font::kMono, detailRect, Faded(theme::kText2, level));
    }
    if (info.tagRow)
        PaintChips(renderer, info, level);
}

// The tag chips, as in the strip under the grid: the name and an x, with a soft square behind the x under the
// pointer; then the add button.
void Viewer::PaintChips(Renderer &renderer, const Info &info, float level) const
{
    float line = Scale(4) / 4.0f;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    for (size_t i = 0; i < info.chips.size(); i++)
    {
        const Chip &chip = info.chips[i];
        const RECT &r = chip.rect;
        renderer.FillRoundedRect(ToRectF(r), radius, Faded(theme::kChip, level));
        RECT text = { r.left + Scale(kChipPadding), r.top, r.left + Scale(kChipPadding) + chip.textWidth + 1, r.bottom };
        renderer.DrawTextLine(Utf8ToWide(m_tags[i]), Font::kCaption, text, Faded(theme::kText1, level));

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
    if (!info.more.empty())
        renderer.DrawTextLine(info.more, Font::kMono, info.moreRect, Faded(theme::kText3, level));

    D2D1_RECT_F add = ToRectF(info.add);
    renderer.FillRoundedRect(add, radius, Faded(theme::kControl, level));
    float hover = m_hover.Level(kAddTagButton);
    if (hover > 0.0f)
        renderer.FillRoundedRect(add, radius, Faded(theme::kOverlayHover, level * hover));
    renderer.DrawTextLine(strings::kTagStripAdd, Font::kCaption, info.add, Faded(theme::kText1, level), DWRITE_TEXT_ALIGNMENT_CENTER);
}

bool Viewer::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    int button = ButtonAt(pt);
    switch (button)
    {
    case kCloseButton:      text = strings::kTipViewerClose; break;
    case kPreviousButton:   text = strings::kTipViewerPrevious; break;
    case kNextButton:       text = strings::kTipViewerNext; break;
    case kMuteButton:       text = strings::kTipViewerMute; break;
    case kFullscreenButton: text = strings::kTipViewerFullscreen; break;
    case kAddTagButton:     text = strings::kTipViewerAddTag; break;
    default:
        if (button < kTagChip)
            return false;
        text = strings::kTipViewerRemoveTag;
        break;
    }
    anchor = button >= kAddTagButton ? TagButtonRect(button) : ButtonRect(button);
    return true;
}
