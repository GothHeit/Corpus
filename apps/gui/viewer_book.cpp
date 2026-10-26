#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cmath>

Viewer::Action Viewer::OnPageReady()
{
    DecodedFrame frame;
    size_t page = 0, count = 0;
    std::string location;
    BookReader::Status status = m_reader.Take(frame, page, count, location);
    if (!m_open || !m_book || status == BookReader::Status::kNothing)
        return Action::kNone;

    bool wasReloading = m_reloading;
    m_pageCount = count;
    m_pageShown = page;
    m_location = location;
    // A book shorter than the page it was left at opens on its last page. An EPUB opens at the text it was left
    // at, and stays at the text shown when its pages are laid out again, wherever that is now. Other pages that
    // arrive were asked for, and one asked for since (a key pressed meanwhile) stays the page wanted.
    if (m_page >= count || (m_reflow && m_adoptPage))
        m_page = page;
    m_adoptPage = false;
    // The page it opens at is the one saved already.
    if (m_pageSaved == kNoPage)
    {
        m_pageSaved = page;
        m_locationSaved = location;
    }
    if (status != BookReader::Status::kPage)
    {
        KillTimer(m_hWnd, kViewerLoadingTimer);
        m_reloading = false;
        // A book that can't be opened where it was opened goes to its default program.
        if (status == BookReader::Status::kFailed && m_first && count == 0 && !wasReloading)
            return Action::kFallBack;
        m_status = status == BookReader::Status::kMissing ? ImageDecoder::Status::kMissing : ImageDecoder::Status::kFailed;
        m_bitmap.Reset();
        Invalidate();
        return Action::kNone;
    }

    TakeFrame(frame);
    if (!wasReloading)
    {
        m_progressDirty = true;
        SetTimer(m_hWnd, kViewerProgressTimer, kProgressDelayMs, nullptr);
    }
    return Action::kNone;
}

void Viewer::GoToPage(size_t page, bool atBottom)
{
    if (!m_book || m_pageCount == 0)
        return;
    page = std::min(page, m_pageCount - 1);
    if (page == m_page)
        return;
    m_page = page;
    m_pageAtBottom = atBottom;
    m_reader.Show(page);
    Invalidate();
}

void Viewer::TurnPage(int delta, bool atBottom)
{
    long long target = static_cast<long long>(m_page) + delta;
    if (target < 0 || m_pageCount == 0 || target >= static_cast<long long>(m_pageCount))
        return;
    GoToPage(static_cast<size_t>(target), atBottom);
}

bool Viewer::ScrollPage(double pixels)
{
    if (!m_bitmap)
        return false;
    double before = m_y;
    m_y += pixels / Zoom();
    KeepInView();
    if (std::abs(m_y - before) < 0.01)
        return false;
    Invalidate();
    return true;
}

// Fits the page again, from its top.
void Viewer::SetPageFit(PageFit fit)
{
    m_pageFit = fit;
    SaveSetting(kFitWidthValue, fit == PageFit::kWidth);
    m_fit = true;
    m_y = 0.0;
    KeepInView();
    Invalidate();
}

// Leaving a book saves the page only when it changed: a book glanced at while stepping through the files keeps
// when it was last really read.
void Viewer::SaveProgress(bool leaving)
{
    KillTimer(m_hWnd, kViewerProgressTimer);
    // After an EPUB's pages are laid out again, the same page number is other text: its place in the text tells.
    bool saved = m_reflow ? m_location == m_locationSaved : m_pageShown == m_pageSaved;
    if (m_progressDirty && m_book && m_pageCount > 0 && m_index < m_paths.size() && m_savePage && !(leaving && saved))
    {
        m_savePage(m_paths[m_index], m_pageShown, m_pageCount, m_location);
        m_pageSaved = m_pageShown;
        m_locationSaved = m_location;
    }
    m_progressDirty = false;
}

// The viewer in pixels and the screen's scale, which an EPUB's pages fill.
void Viewer::ViewSize(UINT &width, UINT &height, float &scale) const
{
    width = static_cast<UINT>(std::max(1L, m_bounds.right - m_bounds.left));
    height = static_cast<UINT>(std::max(1L, m_bounds.bottom - m_bounds.top));
    scale = static_cast<float>(Scale(960)) / 960.0f;
}

// The page bar sits where a video's control bar does: the page and the count, the slider, then the fit and the
// direction buttons.
Viewer::PageBar Viewer::LayoutPageBar() const
{
    PageBar b;
    int margin = Scale(theme::kViewerMargin);
    int height = Scale(theme::kViewerBarHeight);
    int width = std::min(Width(m_bounds) - 2 * margin, Scale(theme::kViewerPageBarWidth));
    if (width <= 0 || !m_renderer)
        return b;
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    b.bar = { left, m_bounds.bottom - margin - height, left + width, m_bounds.bottom - margin };

    int pad = Scale(4);
    int button = Scale(theme::kViewerButton);
    int top = b.bar.top + (height - button) / 2;
    int right = b.bar.right - pad;
    b.direction = { right - button, top, right, top + button };
    // An EPUB's pages fit the view whatever: no fit to choose.
    if (!m_reflow)
        b.fit = { b.direction.left - button, top, b.direction.left, top + button };
    right = (m_reflow ? b.direction.left : b.fit.left) - Scale(10);

    // As wide as the longest the count can make it, so it doesn't jiggle.
    std::wstring longest = FormatCount(m_pageCount) + L" / " + FormatCount(m_pageCount);
    for (wchar_t &c : longest)
    {
        if (c >= L'1' && c <= L'9')
            c = L'0';
    }
    int x = b.bar.left + Scale(12);
    int textWidth = static_cast<int>(std::ceil(m_renderer->TextWidth(longest, Font::kMono)));
    // A narrow bar keeps the slider.
    if (right - x - textWidth - Scale(10) >= Scale(80))
    {
        b.pages = { x, b.bar.top, x + textWidth, b.bar.bottom };
        x = b.pages.right + Scale(10);
    }
    b.slider = { x, b.bar.top, std::max(x, right), b.bar.bottom };
    return b;
}

// The page at 'x' on the slider, which runs right to left too when the pages do.
size_t Viewer::PageAt(int x) const
{
    PageBar b = LayoutPageBar();
    if (Width(b.slider) <= 0 || m_pageCount == 0)
        return m_page;
    double fraction = std::max(0.0, std::min(1.0, static_cast<double>(x - b.slider.left) / Width(b.slider)));
    if (m_rightToLeft)
        fraction = 1.0 - fraction;
    return static_cast<size_t>(std::lround(fraction * (m_pageCount - 1)));
}

void Viewer::PaintPageBar(Renderer &renderer, float level) const
{
    PageBar b = LayoutPageBar();
    if (Width(b.bar) <= 0)
        return;
    renderer.FillRoundedRect(ToRectF(b.bar), static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kOverlay, level));
    if (Width(b.pages) > 0)
    {
        std::wstring pages = FormatCount(m_page + 1) + L" / " + FormatCount(m_pageCount);
        renderer.DrawTextLine(pages, Font::kMono, b.pages, Faded(theme::kText1, level));
    }
    double fraction = m_pageCount > 1 ? static_cast<double>(m_page) / (m_pageCount - 1) : 1.0;
    PaintSlider(renderer, b.slider, fraction, theme::kYuri, level, m_rightToLeft);
    if (ButtonShown(kFitButton))
        PaintBarButton(renderer, kFitButton, level);
    PaintBarButton(renderer, kDirectionButton, level);
}

// The fit button shows how the page fits now: a whole page, or arrows out to both sides. The direction button
// points the way the pages turn.
void Viewer::PaintBarButton(Renderer &renderer, int button, float level) const
{
    D2D1_RECT_F rect = ToRectF(ButtonRect(button));
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    float hover = m_hover.Level(button);
    if (hover > 0.0f)
        renderer.FillRoundedRect(rect, radius, Faded(theme::kOverlayHover, level * hover));

    D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
    float s = static_cast<float>(Scale(5));
    float width = Scale(3) / 2.0f;
    D2D1_COLOR_F color = Faded(theme::kText1, level);
    if (button == kFitButton)
    {
        if (m_pageFit == PageFit::kPage)
            renderer.DrawRoundedRect(D2D1::RectF(c.x - s * 0.75f, c.y - s, c.x + s * 0.75f, c.y + s), static_cast<float>(Scale(1)),
                color, width);
        else
        {
            renderer.DrawLine(D2D1::Point2F(c.x - s * 1.3f, c.y - s), D2D1::Point2F(c.x - s * 1.3f, c.y + s), color, width);
            renderer.DrawLine(D2D1::Point2F(c.x + s * 1.3f, c.y - s), D2D1::Point2F(c.x + s * 1.3f, c.y + s), color, width);
            renderer.DrawLine(D2D1::Point2F(c.x - s * 0.8f, c.y), D2D1::Point2F(c.x + s * 0.8f, c.y), color, width);
            for (float side : { -1.0f, 1.0f })
            {
                const D2D1_POINT_2F head[] = { { c.x + side * s * 0.4f, c.y - s * 0.4f }, { c.x + side * s * 0.8f, c.y },
                    { c.x + side * s * 0.4f, c.y + s * 0.4f } };
                renderer.DrawPolyline(head, 3, color, width);
            }
        }
    }
    else
    {
        float side = m_rightToLeft ? -1.0f : 1.0f;
        renderer.DrawLine(D2D1::Point2F(c.x - side * s, c.y), D2D1::Point2F(c.x + side * s, c.y), color, width);
        const D2D1_POINT_2F head[] = { { c.x + side * s * 0.3f, c.y - s * 0.7f }, { c.x + side * s, c.y },
            { c.x + side * s * 0.3f, c.y + s * 0.7f } };
        renderer.DrawPolyline(head, 3, color, width);
    }
}
