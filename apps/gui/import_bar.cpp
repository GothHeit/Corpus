#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "import_bar.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"

namespace
{
    const int kHeight = 32;
    const int kTrackHeight = 2;
    const int kCancelWidth = 64;
    const int kCancelPadding = 8;   // the label lines up with the page margin, the hover fill reaches past it
    const int kCancelHeight = 24;
    const int kGap = 8;             // between the text and the detail
    const float kSegment = 0.3f;    // share of the track the moving bar covers
    const ULONGLONG kSweepMs = 1500;  // one pass of the moving bar from left to right
}

int ImportBar::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

int ImportBar::Height() const
{
    return Scale(kHeight);
}

void ImportBar::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_bounds = bounds;
}

void ImportBar::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void ImportBar::ShowProgress(const std::wstring &text, const std::wstring &detail)
{
    if (m_state != State::kRunning)
    {
        m_state = State::kRunning;
        m_error = false;
        m_started = GetTickCount64();
    }
    else if (text == m_text && detail == m_detail)
        return;
    m_text = text;
    m_detail = detail;
    Invalidate();
}

void ImportBar::ShowResult(const std::wstring &text, bool error)
{
    m_state = State::kResult;
    m_text = text;
    m_error = error;
    m_detail.clear();
    m_hoverCancel.Reset(m_hWnd, kCancelHoverTimer);
    Invalidate();
}

void ImportBar::Hide()
{
    m_state = State::kHidden;
    m_hoverCancel.Reset(m_hWnd, kCancelHoverTimer);
}

void ImportBar::OnTimer()
{
    m_hoverCancel.Step(m_hWnd, kCancelHoverTimer);
    RECT cancel = CancelRect();
    InvalidateRect(m_hWnd, &cancel, FALSE);
}

void ImportBar::Animate()
{
    if (m_state != State::kRunning || !m_hWnd)
        return;
    RECT track = TrackRect();
    InvalidateRect(m_hWnd, &track, FALSE);
}

// The button sits at the right end, with room for a longer word than Cancel.
RECT ImportBar::CancelRect() const
{
    const int width = Scale(kCancelWidth);
    const int height = Scale(kCancelHeight);
    const int right = m_bounds.right - Scale(theme::kPageMargin) + Scale(kCancelPadding);
    const int top = m_bounds.top + (m_bounds.bottom - m_bounds.top - height) / 2;
    RECT r = { right - width, top, right, top + height };
    return r;
}

RECT ImportBar::TrackRect() const
{
    RECT r = { m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + Scale(kTrackHeight) };
    return r;
}

void ImportBar::OnHover(POINT pt)
{
    RECT cancel = CancelRect();
    bool hover = Running() && PtInRect(&cancel, pt);
    if (m_hoverCancel.Set(m_hWnd, kCancelHoverTimer, hover ? 0 : -1))
        InvalidateRect(m_hWnd, &cancel, FALSE);
}

bool ImportBar::OnLButtonDown(POINT pt, bool &cancel)
{
    cancel = false;
    if (!Visible() || !PtInRect(&m_bounds, pt))
        return false;
    RECT button = CancelRect();
    cancel = Running() && PtInRect(&button, pt);
    return true;
}

void ImportBar::Paint(Renderer &renderer) const
{
    if (!Visible())
        return;

    renderer.FillRect(ToRectF(m_bounds), theme::kLayer);

    // The track replaces the stroke between the grid and the bar. While running, a segment sweeps along it, or
    // the whole track is lit when Windows animations are off (the timer then doesn't redraw it).
    D2D1_RECT_F track = ToRectF(TrackRect());
    if (!Running())
        track.bottom = track.top + Scale(4) / 4.0f;
    renderer.FillRect(track, theme::kStroke);
    if (Running())
    {
        if (AnimationsEnabled())
        {
            float width = track.right - track.left;
            float segment = width * kSegment;
            float phase = static_cast<float>((GetTickCount64() - m_started) % kSweepMs) / kSweepMs;
            float left = track.left - segment + phase * (width + segment);
            D2D1_RECT_F lit = { left < track.left ? track.left : left, track.top,
                left + segment > track.right ? track.right : left + segment, track.bottom };
            renderer.FillRect(lit, theme::kOxblood);
        }
        else
            renderer.FillRect(track, theme::kOxbloodFill);
    }

    const int margin = Scale(theme::kPageMargin);
    RECT text = { m_bounds.left + margin, m_bounds.top, m_bounds.right - margin, m_bounds.bottom };
    if (Running())
    {
        RECT cancel = CancelRect();
        text.right = cancel.left - Scale(kGap);
        float hover = m_hoverCancel.Level(0);
        if (hover > 0.0f)
            renderer.FillRoundedRect(ToRectF(cancel), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kHover, hover));
        renderer.DrawTextLine(strings::kCancel, Font::kCaption, cancel, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // The text keeps its full width; the folder after it takes what is left, cut at the end if too long.
    float textWidth = renderer.TextWidth(m_text, Font::kCaption);
    renderer.DrawTextLine(m_text, Font::kCaption, text, m_error ? theme::kRose : theme::kText2);
    if (!m_detail.empty())
    {
        RECT detail = text;
        detail.left += static_cast<int>(textWidth + 0.5f) + Scale(kGap);
        if (detail.left < detail.right)
            renderer.DrawTextLine(m_detail, Font::kMono, detail, theme::kText3);
    }
}
