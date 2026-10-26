#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

Viewer::Action Viewer::OnDecoded()
{
    DecodedFrame frame;
    ImageDecoder::Status status = m_decoder.Take(frame);
    if (!m_open || status == ImageDecoder::Status::kNothing)
        return Action::kNone;

    if (status != ImageDecoder::Status::kFrame)
    {
        KillTimer(m_hWnd, kViewerLoadingTimer);
        // A picture that was on screen and fails to decode again keeps the viewer up.
        bool wasReloading = m_reloading;
        m_reloading = false;
        if (status == ImageDecoder::Status::kFailed && m_first && !wasReloading)
            return Action::kFallBack;
        m_status = status;
        m_bitmap.Reset();
        Invalidate();
        return Action::kNone;
    }

    // The next frame of the animation on screen.
    if (m_status == ImageDecoder::Status::kFrame && m_animated && !m_reloading)
    {
        m_next = std::move(frame);
        m_hasNext = true;
        if (m_due)
            NextFrame();
        return Action::kNone;
    }

    TakeFrame(frame);
    return Action::kNone;
}

// A new picture or page, fitted, unless it is the one on screen made again. A page shows its top, or its bottom
// when the reader scrolled back to it.
void Viewer::TakeFrame(DecodedFrame &frame)
{
    if (!m_reloading)
    {
        m_fit = true;
        if (m_book)
        {
            m_x = 0.0;
            m_y = m_pageAtBottom ? 1e12 : 0.0;
        }
    }
    m_reloading = false;
    KillTimer(m_hWnd, kViewerLoadingTimer);
    m_status = ImageDecoder::Status::kFrame;
    m_frame = std::move(frame);
    m_upload = true;
    m_animated = m_frame.delayMs > 0;
    if (m_animated)
        StartFrame();
    KeepInView();
    Invalidate();
}

// The frame in m_frame is going up: waits its delay, and has the decoder make the next one meanwhile. Each frame is
// due a delay after the one before was due, not after it went up: timers fire on the system's ticks (about 15.6 ms),
// and the time lost to that would add up, so 20 ms frames would play at two thirds of their speed.
void Viewer::StartFrame()
{
    m_hasNext = false;
    m_due = false;
    ULONGLONG now = GetTickCount64();
    if (m_nextDue == 0 || now > m_nextDue + kLateFrameMs)
        m_nextDue = now;
    m_nextDue += m_frame.delayMs;
    SetTimer(m_hWnd, kViewerFrameTimer, m_nextDue > now ? static_cast<UINT>(m_nextDue - now) : USER_TIMER_MINIMUM,
        nullptr);
    m_decoder.WantNext();
}

// The last frame of an animation that ends stays up as a still picture, its pixels dropped once uploaded.
void Viewer::NextFrame()
{
    m_frame = std::move(m_next);
    m_next = DecodedFrame();
    m_upload = true;
    if (m_frame.last)
    {
        m_animated = m_hasNext = m_due = false;
        KillTimer(m_hWnd, kViewerFrameTimer);
    }
    else
        StartFrame();
    Invalidate();
}

double Viewer::FitZoom() const
{
    double width = m_bounds.right - m_bounds.left;
    double height = m_bounds.bottom - m_bounds.top;
    if (m_frame.sourceWidth == 0 || m_frame.sourceHeight == 0 || width <= 0 || height <= 0)
        return 1.0;
    if (m_book)
        return m_pageFit == PageFit::kWidth ? width / m_frame.sourceWidth :
            std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight);
    return std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight);
}

// A picture fits the view even when that makes it bigger, so it can still go down to its actual size.
double Viewer::MinZoom() const
{
    double width = m_bounds.right - m_bounds.left;
    double height = m_bounds.bottom - m_bounds.top;
    if (!m_book)
        return std::min(FitZoom(), 1.0);
    if (m_frame.sourceWidth == 0 || m_frame.sourceHeight == 0 || width <= 0 || height <= 0)
        return FitZoom();
    return std::min(FitZoom(), std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight));
}

POINT Viewer::Center() const
{
    return { (m_bounds.left + m_bounds.right) / 2, (m_bounds.top + m_bounds.bottom) / 2 };
}

// Zooms keeping the point of the picture under 'pt' where it is. Fitted, or actual size when that is smaller, is as
// small as it gets.
void Viewer::ZoomAt(POINT pt, double zoom)
{
    if (m_frame.sourceWidth == 0)
        return;
    double fit = FitZoom();
    zoom = std::max(MinZoom(), std::min(zoom, std::max(kMaxZoom, fit)));

    POINT center = Center();
    double before = Zoom();
    double x = m_x + (pt.x - center.x) / before;
    double y = m_y + (pt.y - center.y) / before;

    m_fit = std::abs(zoom - fit) <= fit * 0.0001;
    m_zoom = zoom;
    double after = Zoom();
    m_x = x - (pt.x - center.x) / after;
    m_y = y - (pt.y - center.y) / after;
    KeepInView();
    Invalidate();
}

void Viewer::ZoomToFit()
{
    m_fit = true;
    KeepInView();
    Invalidate();
}

// Between fitted and actual size; a picture that fits at actual size, or a page that fits at it or below, goes to
// twice that.
void Viewer::ToggleActualSize(POINT pt)
{
    if (!m_fit)
    {
        ZoomToFit();
        return;
    }
    double fit = FitZoom();
    bool atActualSize = m_book ? fit >= 1.0 : std::abs(fit - 1.0) <= 0.0001;
    ZoomAt(pt, atActualSize ? 2.0 : 1.0);
}

// A picture smaller than the view sits in its middle; a bigger one can't be dragged past its edges.
void Viewer::KeepInView()
{
    double zoom = Zoom();
    auto keep = [zoom](double &at, double size, double view)
    {
        if (size * zoom <= view)
            at = size / 2.0;
        else
            at = std::max(view / 2.0 / zoom, std::min(at, size - view / 2.0 / zoom));
    };
    keep(m_x, m_frame.sourceWidth, m_bounds.right - m_bounds.left);
    keep(m_y, m_frame.sourceHeight, m_bounds.bottom - m_bounds.top);
}

D2D1_RECT_F Viewer::PictureRect() const
{
    double zoom = Zoom();
    POINT center = Center();
    // On whole pixels, so a picture at 100% stays sharp.
    float left = std::floor(static_cast<float>(center.x - m_x * zoom) + 0.5f);
    float top = std::floor(static_cast<float>(center.y - m_y * zoom) + 0.5f);
    return D2D1::RectF(left, top, left + static_cast<float>(m_frame.sourceWidth * zoom),
        top + static_cast<float>(m_frame.sourceHeight * zoom));
}
