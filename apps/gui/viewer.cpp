#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    // One wheel notch, or one press of + or -, zooms by this much.
    const double kZoomStep = 1.25;
    const double kMaxZoom = 32.0;
    // A picture that takes longer than this to decode says "Loading" meanwhile; a quicker one just appears.
    const UINT kLoadingDelayMs = 250;
    // While the window is minimized, an animation only checks this often whether it is back.
    const UINT kMinimizedFrameMs = 250;
    // A frame that went up later than this after it was due starts the animation's clock again, rather than
    // rushing the frames after it to catch up.
    const ULONGLONG kLateFrameMs = 50;

    // "photo.jpg" for C:\Pictures\photo.jpg.
    std::wstring FileName(const std::string &path)
    {
        std::wstring wide = Utf8ToWide(path);
        size_t slash = wide.find_last_of(L"\\/");
        return slash == std::wstring::npos ? wide : wide.substr(slash + 1);
    }
}

int Viewer::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void Viewer::Invalidate() const
{
    if (m_hWnd && m_open)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void Viewer::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    KeepInView();
}

// ---- Opening and stepping ----

void Viewer::Open(std::vector<std::string> paths, size_t index)
{
    if (paths.empty())
        return;
    m_paths = std::move(paths);
    m_open = true;
    m_first = true;
    m_overlayFrom = m_overlayTo = 1.0f;
    Show(std::min(index, m_paths.size() - 1));
    ShowOverlays();
}

void Viewer::Close()
{
    if (!m_open)
        return;
    m_open = false;
    m_decoder.Close();
    if (m_dragging)
        ReleaseCapture();
    m_dragging = false;
    for (UINT_PTR timer : { kViewerIdleTimer, kViewerFadeTimer, kViewerFrameTimer, kViewerLoadingTimer })
        KillTimer(m_hWnd, timer);
    m_hover.Reset(m_hWnd, kViewerHoverTimer);

    m_paths.clear();
    m_status = ImageDecoder::Status::kNothing;
    m_frame = DecodedFrame();
    m_next = DecodedFrame();
    m_bitmap.Reset();
    m_upload = m_animated = m_hasNext = m_due = m_reloading = m_loadingShown = false;
}

// Starts decoding the file. The picture before stays up until the new one arrives, so stepping through quick
// pictures doesn't flash the backdrop; if it takes a while, it gives way to "Loading".
void Viewer::Show(size_t index)
{
    m_index = index;
    m_status = ImageDecoder::Status::kNothing;
    m_loadingShown = false;
    m_reloading = false;
    m_animated = m_hasNext = m_due = false;
    m_next = DecodedFrame();
    m_nextDue = 0;
    KillTimer(m_hWnd, kViewerFrameTimer);

    m_decoder.Open(Utf8ToWide(m_paths[m_index]), m_renderer ? m_renderer->MaxBitmapSize() : 4096);
    SetTimer(m_hWnd, kViewerLoadingTimer, kLoadingDelayMs, nullptr);
    Invalidate();
}

// Moves 'delta' files on, stopping at the first and last.
void Viewer::Step(int delta)
{
    long long target = static_cast<long long>(m_index) + delta;
    target = std::max(0LL, std::min(target, static_cast<long long>(m_paths.size()) - 1));
    if (static_cast<size_t>(target) == m_index)
        return;
    m_first = false;
    Show(static_cast<size_t>(target));
    ShowOverlays();
}

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

    // A new picture, fitted, unless it is the one on screen decoded again.
    if (!m_reloading)
        m_fit = true;
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
    return Action::kNone;
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

void Viewer::NextFrame()
{
    m_frame = std::move(m_next);
    m_next = DecodedFrame();
    m_upload = true;
    StartFrame();
    Invalidate();
}

// ---- Zoom and pan ----

double Viewer::FitZoom() const
{
    double width = m_bounds.right - m_bounds.left;
    double height = m_bounds.bottom - m_bounds.top;
    if (m_frame.sourceWidth == 0 || m_frame.sourceHeight == 0 || width <= 0 || height <= 0)
        return 1.0;
    return std::min(1.0, std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight));
}

POINT Viewer::Center() const
{
    return { (m_bounds.left + m_bounds.right) / 2, (m_bounds.top + m_bounds.bottom) / 2 };
}

// Zooms keeping the point of the picture under 'pt' where it is. Fitted is as small as it gets.
void Viewer::ZoomAt(POINT pt, double zoom)
{
    if (m_frame.sourceWidth == 0)
        return;
    double fit = FitZoom();
    zoom = std::max(fit, std::min(zoom, kMaxZoom));

    POINT center = Center();
    double before = Zoom();
    double x = m_x + (pt.x - center.x) / before;
    double y = m_y + (pt.y - center.y) / before;

    m_fit = zoom <= fit * 1.0001;
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

// ---- Overlays ----

float Viewer::OverlayLevel() const
{
    if (!AnimationsEnabled())
        return m_overlayTo;
    float progress = std::min(1.0f, static_cast<float>(GetTickCount64() - m_overlayStart) / theme::kPaneMs);
    return m_overlayFrom + (m_overlayTo - m_overlayFrom) * progress;
}

bool Viewer::CursorHidden() const
{
    return m_open && !m_dragging && m_overlayTo == 0.0f && OverlayLevel() == 0.0f;
}

// Shows the name and buttons, and hides them again once the pointer stands still for a while.
void Viewer::ShowOverlays()
{
    if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    FadeOverlays(1.0f);
    SetTimer(m_hWnd, kViewerIdleTimer, theme::kOverlayIdleMs, nullptr);
}

void Viewer::FadeOverlays(float to)
{
    if (to == m_overlayTo)
        return;
    m_overlayFrom = OverlayLevel();
    m_overlayTo = to;
    m_overlayStart = GetTickCount64();
    if (AnimationsEnabled())
        SetTimer(m_hWnd, kViewerFadeTimer, theme::kFrameMs, nullptr);
    else if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
        SetCursor(nullptr);
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
    default:
        return { m_bounds.right - margin - size, top, m_bounds.right - margin, top + size };
    }
}

bool Viewer::ButtonShown(int button) const
{
    return button == kCloseButton || m_paths.size() > 1;
}

bool Viewer::ButtonEnabled(int button) const
{
    if (button == kPreviousButton)
        return m_index > 0;
    if (button == kNextButton)
        return m_index + 1 < m_paths.size();
    return true;
}

// Hidden buttons can't be clicked.
int Viewer::ButtonAt(POINT pt) const
{
    if (!m_open || m_overlayTo == 0.0f)
        return -1;
    for (int button = 0; button < kButtonCount; button++)
    {
        RECT r = ButtonRect(button);
        if (ButtonShown(button) && PtInRect(&r, pt))
            return button;
    }
    return -1;
}

// ---- Painting ----

void Viewer::Paint(Renderer &renderer)
{
    if (!m_open)
        return;
    renderer.FillRect(ToRectF(m_bounds), theme::kViewerBackdrop);

    // The graphics device was lost with the bitmap. An animation still has its frame; a still picture had its
    // pixels dropped once uploaded, and is decoded again.
    unsigned generation = renderer.Generation();
    if (m_bitmap && m_bitmapGeneration != generation)
    {
        m_bitmap.Reset();
        if (m_animated)
            m_upload = true;
        else if (m_status == ImageDecoder::Status::kFrame && !m_reloading)
        {
            m_reloading = true;
            m_decoder.Open(Utf8ToWide(m_paths[m_index]), renderer.MaxBitmapSize());
        }
    }

    if (m_upload)
    {
        m_upload = false;
        if (m_bitmap && m_bitmapGeneration == generation && m_bitmap->GetPixelSize().width == m_frame.width &&
            m_bitmap->GetPixelSize().height == m_frame.height)
            m_bitmap->CopyFromMemory(nullptr, m_frame.pixels.data(), m_frame.width * 4);
        else
        {
            m_bitmap = renderer.CreateBitmap(m_frame.width, m_frame.height, m_frame.pixels.data());
            m_bitmapGeneration = generation;
        }
        if (!m_bitmap)
        {
            m_status = ImageDecoder::Status::kFailed;
            m_animated = false;
            KillTimer(m_hWnd, kViewerFrameTimer);
            m_decoder.Close();
        }
        if (!m_animated)
            std::vector<BYTE>().swap(m_frame.pixels);
    }

    renderer.PushClip(m_bounds);
    if (m_bitmap)
    {
        D2D1_RECT_F rect = PictureRect();
        // Resampled smoothly while shown smaller than the bitmap is.
        renderer.DrawImage(m_bitmap.Get(), rect, rect.right - rect.left < m_frame.width);
    }
    else
        PaintMessage(renderer);

    float level = OverlayLevel();
    if (level > 0.0f)
    {
        for (int button = 0; button < kButtonCount; button++)
        {
            if (ButtonShown(button))
                PaintButton(renderer, button, level);
        }
        PaintInfo(renderer, level);
    }
    renderer.PopClip();
}

// Why there is no picture: it can't be shown, the file is gone, or it is still being decoded.
void Viewer::PaintMessage(Renderer &renderer) const
{
    const wchar_t *message = nullptr;
    const wchar_t *hint = nullptr;
    if (m_status == ImageDecoder::Status::kFailed)
    {
        message = strings::kCantShow;
        hint = strings::kOpenHint;
    }
    else if (m_status == ImageDecoder::Status::kMissing)
        message = strings::kNotAtPath;
    else if (m_loadingShown)
        message = strings::kLoading;
    if (!message)
        return;

    int middle = Center().y;
    int line = static_cast<int>(renderer.LineHeight(Font::kBody) + 0.5f);
    RECT row = { m_bounds.left, middle - line, m_bounds.right, middle };
    if (!hint)
        OffsetRect(&row, 0, line / 2);
    renderer.DrawTextLine(message, Font::kBody, row, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (hint)
    {
        RECT below = { m_bounds.left, middle + Scale(4), m_bounds.right, middle + Scale(4) + line };
        renderer.DrawTextLine(hint, Font::kCaption, below, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
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

// The name under the picture, with where it is in the list, its size and the zoom: "3 of 20 · 1920 × 1080 · 50%".
// A picture too big to keep at full size says it was reduced, so 100% isn't taken for its own pixels.
void Viewer::PaintInfo(Renderer &renderer, float level) const
{
    std::wstring name = FileName(m_paths[m_index]);
    std::wstring detail;
    const std::wstring separator = L" \u00B7 ";
    if (m_paths.size() > 1)
        detail = FormatCount(m_index + 1) + L" " + strings::kOf + L" " + FormatCount(m_paths.size());
    if (m_status == ImageDecoder::Status::kFrame)
    {
        if (!detail.empty())
            detail += separator;
        detail += std::to_wstring(m_frame.sourceWidth) + L" \u00D7 " + std::to_wstring(m_frame.sourceHeight) + separator;
        if (m_frame.width < m_frame.sourceWidth || m_frame.height < m_frame.sourceHeight)
            detail += strings::kReduced + separator;
        detail += std::to_wstring(static_cast<int>(std::lround(Zoom() * 100))) + L"%";
    }

    float pad = static_cast<float>(Scale(12));
    float gap = static_cast<float>(Scale(10));
    float margin = static_cast<float>(Scale(theme::kViewerMargin));
    float room = (m_bounds.right - m_bounds.left) - 2 * margin - 2 * pad;
    float detailWidth = detail.empty() ? 0.0f : renderer.TextWidth(detail, Font::kMono);
    float nameRoom = room - (detail.empty() ? 0.0f : gap + detailWidth);
    // A narrow window keeps the name.
    if (nameRoom < Scale(48))
    {
        detail.clear();
        detailWidth = 0.0f;
        nameRoom = room;
    }
    if (nameRoom <= 0.0f)
        return;
    float nameWidth = std::min(std::ceil(renderer.TextWidth(name, Font::kBody)), nameRoom);
    float width = 2 * pad + nameWidth + (detail.empty() ? 0.0f : gap + detailWidth);

    float middle = (m_bounds.left + m_bounds.right) / 2.0f;
    float bottom = m_bounds.bottom - margin;
    D2D1_RECT_F panel = D2D1::RectF(std::floor(middle - width / 2), bottom - Scale(theme::kViewerInfoHeight),
        std::floor(middle - width / 2) + std::ceil(width), bottom);
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(panel, radius, Faded(theme::kOverlay, level));

    LONG left = static_cast<LONG>(panel.left + pad);
    RECT nameRect = { left, static_cast<LONG>(panel.top), left + static_cast<LONG>(nameWidth) + 1, static_cast<LONG>(panel.bottom) };
    renderer.DrawTextLine(name, Font::kBody, nameRect, Faded(theme::kText1, level));
    if (!detail.empty())
    {
        RECT detailRect = { nameRect.right + static_cast<LONG>(gap), nameRect.top, static_cast<LONG>(panel.right), nameRect.bottom };
        renderer.DrawTextLine(detail, Font::kMono, detailRect, Faded(theme::kText2, level));
    }
}

// ---- Input ----

Viewer::Action Viewer::OnKeyDown(WPARAM key, bool repeat)
{
    switch (key)
    {
    case VK_ESCAPE:
    case VK_SPACE:
        return repeat ? Action::kNone : Action::kClose;
    case VK_RETURN:
        return repeat ? Action::kNone : Action::kOpenExternally;
    case VK_LEFT:
    case VK_UP:
    case VK_PRIOR:
        Step(-1);
        break;
    case VK_RIGHT:
    case VK_DOWN:
    case VK_NEXT:
        Step(1);
        break;
    case VK_HOME:
        Step(-static_cast<int>(m_index));
        break;
    case VK_END:
        Step(static_cast<int>(m_paths.size() - 1 - m_index));
        break;
    case VK_OEM_PLUS:
    case VK_ADD:
        ZoomAt(Center(), Zoom() * kZoomStep);
        break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        ZoomAt(Center(), Zoom() / kZoomStep);
        break;
    case '0':
    case VK_NUMPAD0:
        ZoomToFit();
        break;
    }
    return Action::kNone;
}

Viewer::Action Viewer::OnLButtonDown(POINT pt, bool doubleClick)
{
    if (!Contains(pt))
        return Action::kNone;

    switch (ButtonAt(pt))
    {
    case kCloseButton:
        return doubleClick ? Action::kNone : Action::kClose;
    case kPreviousButton:
        Step(-1);
        return Action::kNone;
    case kNextButton:
        Step(1);
        return Action::kNone;
    }

    // Between fitted and actual size; a picture that fits at actual size goes to twice that.
    if (doubleClick)
    {
        if (m_fit)
            ZoomAt(pt, FitZoom() < 1.0 ? 1.0 : 2.0);
        else
            ZoomToFit();
        return Action::kNone;
    }

    if (m_bitmap)
    {
        m_dragging = true;
        m_dragFrom = pt;
        m_dragX = m_x;
        m_dragY = m_y;
        SetCapture(m_hWnd);
    }
    return Action::kNone;
}

void Viewer::OnMouseMove(POINT pt)
{
    bool moved = pt.x != m_pointer.x || pt.y != m_pointer.y;
    m_pointer = pt;
    if (!m_open)
        return;

    if (m_dragging)
    {
        double zoom = Zoom();
        m_x = m_dragX - (pt.x - m_dragFrom.x) / zoom;
        m_y = m_dragY - (pt.y - m_dragFrom.y) / zoom;
        KeepInView();
        Invalidate();
        return;
    }

    if (!PtInRect(&m_bounds, pt))
    {
        if (m_hover.Set(m_hWnd, kViewerHoverTimer, -1))
            Invalidate();
        return;
    }
    // Windows also sends a move when nothing moved (a window closing above, say): that one doesn't count.
    if (moved)
        ShowOverlays();
    int button = ButtonAt(pt);
    if (m_hover.Set(m_hWnd, kViewerHoverTimer, button >= 0 && ButtonEnabled(button) ? button : -1))
        Invalidate();
}

void Viewer::OnLButtonUp()
{
    if (m_dragging)
        ReleaseCapture();
    m_dragging = false;
}

void Viewer::OnCaptureChanged()
{
    m_dragging = false;
}

void Viewer::OnMouseWheel(POINT pt, int delta)
{
    if (Contains(pt))
        ZoomAt(pt, Zoom() * std::pow(kZoomStep, delta / static_cast<double>(WHEEL_DELTA)));
}

void Viewer::OnTimer(UINT_PTR timer)
{
    switch (timer)
    {
    case kViewerIdleTimer:
        KillTimer(m_hWnd, kViewerIdleTimer);
        // Not while the pointer rests on a button, or drags the picture.
        if (m_dragging || ButtonAt(m_pointer) >= 0)
            SetTimer(m_hWnd, kViewerIdleTimer, theme::kOverlayIdleMs, nullptr);
        else
            FadeOverlays(0.0f);
        break;

    case kViewerFadeTimer:
        Invalidate();
        if (OverlayLevel() == m_overlayTo)
        {
            KillTimer(m_hWnd, kViewerFadeTimer);
            if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
                SetCursor(nullptr);
        }
        break;

    case kViewerHoverTimer:
        m_hover.Step(m_hWnd, kViewerHoverTimer);
        Invalidate();
        break;

    case kViewerFrameTimer:
        KillTimer(m_hWnd, kViewerFrameTimer);
        // Nothing to see while minimized: the animation waits.
        if (IsIconic(m_hWnd))
            SetTimer(m_hWnd, kViewerFrameTimer, kMinimizedFrameMs, nullptr);
        else if (m_hasNext)
            NextFrame();
        else
            m_due = true;
        break;

    case kViewerLoadingTimer:
        KillTimer(m_hWnd, kViewerLoadingTimer);
        if (m_status == ImageDecoder::Status::kNothing)
        {
            m_loadingShown = true;
            m_bitmap.Reset();
            Invalidate();
        }
        break;
    }
}
