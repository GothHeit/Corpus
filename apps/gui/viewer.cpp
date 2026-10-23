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
#include <utility>

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

    // How often the time and the seek bar move on while the controls over a video show.
    const UINT kVideoTimeMs = 250;
    // The arrows seek this far into a video, and the wheel and the up and down arrows change its volume this much.
    const double kSeekStepSeconds = 5.0;
    const int kVolumeStep = 5;

    // A book: a click this close to either side (a fraction of the width) turns the page, unless it drags. The
    // up and down arrows scroll a page this much of the view, a wheel notch this much, and Page Up and Page Down
    // a view at a time less this much of it, which shows again.
    const double kTurnZone = 0.3;
    const double kArrowScroll = 0.15;
    const double kWheelScroll = 0.2;
    const double kPageOverlap = 0.1;
    // The page reached is saved once the reader stayed on it this long, and when the book is left.
    const UINT kProgressDelayMs = 1000;

    // How books are read is kept for next time, next to the window's settings.
    const wchar_t *kSettingsKey = L"Software\\Corpus";
    const wchar_t *kFitWidthValue = L"ReaderFitWidth";
    const wchar_t *kRightToLeftValue = L"ReaderRightToLeft";

    bool LoadSetting(const wchar_t *name)
    {
        DWORD value = 0;
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS && value != 0;
    }

    void SaveSetting(const wchar_t *name, bool on)
    {
        DWORD value = on ? 1 : 0;
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &value, sizeof(value));
    }

    // "photo.jpg" for C:\Pictures\photo.jpg.
    std::wstring FileName(const std::string &path)
    {
        std::wstring wide = Utf8ToWide(path);
        size_t slash = wide.find_last_of(L"\\/");
        return slash == std::wstring::npos ? wide : wide.substr(slash + 1);
    }

    // "4:05", or "1:02:09" for a video an hour long or more.
    std::wstring FormatTime(double seconds, bool hours)
    {
        long long total = seconds > 0.0 ? static_cast<long long>(seconds) : 0;
        wchar_t text[32];
        if (hours)
            swprintf(text, 32, L"%lld:%02lld:%02lld", total / 3600, total / 60 % 60, total % 60);
        else
            swprintf(text, 32, L"%lld:%02lld", total / 60, total % 60);
        return text;
    }

    int Width(const RECT &r)
    {
        return r.right - r.left;
    }
}

int Viewer::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

// Over a video, the overlays are drawn again too, once for all the changes made meanwhile.
void Viewer::Invalidate() const
{
    if (!m_hWnd || !m_open)
        return;
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
    if (VideoShown() && !m_overlaysQueued)
        m_overlaysQueued = PostMessage(m_hWnd, kViewerOverlays, 0, 0) != FALSE;
}

// What an event made the window do, plus laying it out again if the viewer grew or shrank meanwhile.
Viewer::Action Viewer::Result(Action action)
{
    if (m_relayout && m_open)
    {
        m_relayout = false;
        if (action == Action::kNone)
            return Action::kLayout;
    }
    return action;
}

void Viewer::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_video.SetBounds(bounds);
    KeepInView();
    Invalidate();
}

void Viewer::SetReading(LastPage lastPage, SavePage savePage)
{
    m_lastPage = std::move(lastPage);
    m_savePage = std::move(savePage);
}

void Viewer::Start(HWND hWnd)
{
    m_decoder.Start(hWnd);
    m_reader.Start(hWnd);
    m_pageFit = LoadSetting(kFitWidthValue) ? PageFit::kWidth : PageFit::kPage;
    m_rightToLeft = LoadSetting(kRightToLeftValue);
}

void Viewer::Stop()
{
    SaveProgress();
    m_decoder.Stop();
    m_reader.Stop();
    m_video.Shutdown();
}

void Viewer::SetFullscreen(bool fullscreen)
{
    if (fullscreen == m_fullscreen)
        return;
    m_fullscreen = fullscreen;
    m_relayout = true;
}

// ---- Opening and stepping ----

Viewer::Action Viewer::Open(std::vector<std::string> paths, size_t index)
{
    if (paths.empty())
        return Action::kNone;
    SaveProgress();
    m_paths = std::move(paths);
    m_open = true;
    m_first = true;
    m_overlayFrom = m_overlayTo = 1.0f;
    Action action = Show(std::min(index, m_paths.size() - 1));
    ShowOverlays();
    return action;
}

void Viewer::Close()
{
    if (!m_open)
        return;
    SaveProgress();
    m_open = false;
    m_decoder.Close();
    m_reader.Close();
    m_book = false;
    m_pageCount = 0;
    m_video.Stop();
    m_fullscreen = m_relayout = false;
    m_menu = Menu::kNone;
    m_controlsState.clear();
    if (m_dragging || m_seeking || m_settingVolume || m_pageSeeking)
        ReleaseCapture();
    m_dragging = m_seeking = m_settingVolume = m_pageSeeking = m_clickTurn = false;
    for (UINT_PTR timer : { kViewerIdleTimer, kViewerFadeTimer, kViewerFrameTimer, kViewerLoadingTimer, kViewerVideoTimer })
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
// pictures doesn't flash the backdrop; if it takes a while, it gives way to "Loading". A video starts playing
// instead, once its window is up. A book opens on the page it was left at.
Viewer::Action Viewer::Show(size_t index)
{
    SaveProgress();
    m_index = index;
    m_status = ImageDecoder::Status::kNothing;
    m_loadingShown = false;
    m_reloading = false;
    m_animated = m_hasNext = m_due = false;
    m_next = DecodedFrame();
    m_nextDue = 0;
    m_menu = Menu::kNone;
    m_controlsState.clear();
    KillTimer(m_hWnd, kViewerFrameTimer);
    SetTimer(m_hWnd, kViewerLoadingTimer, kLoadingDelayMs, nullptr);

    m_book = IsBook(m_paths[m_index]);
    m_pageCount = 0;
    m_pageAtBottom = false;
    m_wheelTurn = 0;
    if (m_book)
    {
        m_decoder.Close();
        m_video.Stop();
        m_page = m_pageShown = m_lastPage ? m_lastPage(m_paths[m_index]) : 0;
        // Pages are made as wide as the screen, so the width of one fits sharp even in fullscreen.
        UINT maxSide = m_renderer ? m_renderer->MaxBitmapSize() : 4096;
        MONITORINFO monitor = { sizeof(monitor) };
        UINT width = 1920;
        if (GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
            width = static_cast<UINT>(Width(monitor.rcMonitor));
        m_reader.Open(Utf8ToWide(m_paths[m_index]), m_page, std::min(width, maxSide), maxSide);
        Invalidate();
        return Action::kNone;
    }
    m_reader.Close();

    if (IsPlayableVideo(m_paths[m_index]))
    {
        m_decoder.Close();
        m_frame = DecodedFrame();
        m_bitmap.Reset();
        m_upload = false;
        bool started = m_video.Open(m_hWnd, m_bounds, m_paths[m_index]);
        Invalidate();
        // libmpv couldn't be loaded: the file it was opened with goes to its default program.
        if (!started && m_first)
            return Action::kFallBack;
        return Action::kNone;
    }

    m_video.Stop();
    m_decoder.Open(Utf8ToWide(m_paths[m_index]), m_renderer ? m_renderer->MaxBitmapSize() : 4096);
    Invalidate();
    return Action::kNone;
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

// ---- Books ----

Viewer::Action Viewer::OnPageReady()
{
    DecodedFrame frame;
    size_t page = 0, count = 0;
    BookReader::Status status = m_reader.Take(frame, page, count);
    if (!m_open || !m_book || status == BookReader::Status::kNothing)
        return Action::kNone;

    bool wasReloading = m_reloading;
    m_pageCount = count;
    m_pageShown = page;
    // A book shorter than the page it was left at opens on its last page.
    if (m_page >= count)
        m_page = page;
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

void Viewer::SaveProgress()
{
    KillTimer(m_hWnd, kViewerProgressTimer);
    if (m_progressDirty && m_book && m_pageCount > 0 && m_index < m_paths.size() && m_savePage)
        m_savePage(m_paths[m_index], m_pageShown, m_pageCount);
    m_progressDirty = false;
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
    b.fit = { b.direction.left - button, top, b.direction.left, top + button };
    right = b.fit.left - Scale(10);

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

// ---- Video ----

Viewer::Action Viewer::OnVideoEvent()
{
    VideoPlayer::Status before = m_video.GetStatus();
    bool changed = m_video.OnEvent();
    if (!m_open)
        return Action::kNone;
    VideoPlayer::Status status = m_video.GetStatus();
    if (status != before)
    {
        KillTimer(m_hWnd, kViewerLoadingTimer);
        // A video that doesn't open where it was opened goes to its default program.
        if (status == VideoPlayer::Status::kFailed && m_first)
            return Action::kFallBack;
        if (status == VideoPlayer::Status::kPlaying)
            ShowOverlays();
        m_controlsState.clear();
        Invalidate();
    }
    else if (changed)
    {
        std::wstring state = ControlsState();
        if (state != m_controlsState)
            Invalidate();
    }
    // The menu lists tracks that may be gone.
    if (m_menu != Menu::kNone && MenuItems().empty())
        m_menu = Menu::kNone;
    return Action::kNone;
}

// What the overlays show of the video, to tell when they need drawing again.
std::wstring Viewer::ControlsState() const
{
    Controls c = LayoutControls();
    std::wstring state = FormatTime(m_video.Position(), true) + L"|" + FormatTime(m_video.Duration(), true);
    state += L"|" + std::to_wstring(static_cast<int>(m_video.Duration() > 0.0 ?
        Width(c.seek) * m_video.Position() / m_video.Duration() : 0.0));
    state += L"|" + std::to_wstring(m_video.Paused()) + std::to_wstring(m_video.Ended()) + std::to_wstring(m_video.Muted());
    state += L"|" + std::to_wstring(m_video.Volume()) + L"|" + std::to_wstring(m_video.Width()) + L"x" +
        std::to_wstring(m_video.Height());
    state += L"|" + std::to_wstring(m_video.Audio()) + L"/" + std::to_wstring(m_video.AudioTracks().size());
    state += L"|" + std::to_wstring(m_video.Subtitle()) + L"/" + std::to_wstring(m_video.SubtitleTracks().size());
    return state;
}

// Draws every overlay over the video into memory and hands it to libmpv; hidden ones are taken away.
void Viewer::OnOverlays()
{
    m_overlaysQueued = false;
    if (!m_open || !m_renderer || !VideoShown())
        return;
    m_controlsState = ControlsState();
    float level = OverlayLevel();
    Renderer &renderer = *m_renderer;

    auto put = [&](int id, bool shown, RECT rect, auto paint)
    {
        RECT visible;
        if (!shown || level <= 0.0f || !IntersectRect(&visible, &rect, &m_bounds) || !renderer.BeginLayer(rect))
        {
            m_video.HideOverlay(id);
            return;
        }
        paint();
        if (renderer.EndLayer(m_layer))
            m_video.ShowOverlay(id, rect.left - m_bounds.left, rect.top - m_bounds.top, m_layer,
                static_cast<UINT>(Width(rect)), static_cast<UINT>(rect.bottom - rect.top));
        else
            m_video.HideOverlay(id);
    };

    const std::pair<int, int> buttons[] = { { kCloseOverlay, kCloseButton }, { kPreviousOverlay, kPreviousButton },
        { kNextOverlay, kNextButton } };
    for (const std::pair<int, int> &item : buttons)
    {
        int button = item.second;
        put(item.first, ButtonShown(button), ButtonRect(button), [&] { PaintButton(renderer, button, level); });
    }

    Info info = LayoutInfo(renderer);
    RECT infoRect = { static_cast<LONG>(std::floor(info.panel.left)), static_cast<LONG>(std::floor(info.panel.top)),
        static_cast<LONG>(std::ceil(info.panel.right)), static_cast<LONG>(std::ceil(info.panel.bottom)) };
    // An open menu stands where the name is.
    put(kInfoOverlay, info.shown && m_menu == Menu::kNone, infoRect, [&] { PaintInfo(renderer, level); });
    put(kControlsOverlay, true, LayoutControls().bar, [&] { PaintControls(renderer, level); });
    put(kMenuOverlay, m_menu != Menu::kNone, MenuRect(), [&] { PaintMenu(renderer, level); });
}

Viewer::Controls Viewer::LayoutControls() const
{
    Controls c;
    int margin = Scale(theme::kViewerMargin);
    int height = Scale(theme::kViewerBarHeight);
    int width = std::min(Width(m_bounds) - 2 * margin, Scale(theme::kViewerBarWidth));
    if (width <= 0)
        return c;
    int left = (m_bounds.left + m_bounds.right - width) / 2;
    c.bar = { left, m_bounds.bottom - margin - height, left + width, m_bounds.bottom - margin };

    int pad = Scale(4);
    int button = Scale(theme::kViewerButton);
    int top = c.bar.top + (height - button) / 2;
    auto place = [&](int x, int w) { return RECT{ x, top, x + w, top + button }; };

    int x = c.bar.left + pad;
    c.play = place(x, button);
    x = c.play.right + Scale(6);

    int right = c.bar.right - pad;
    c.fullscreen = place(right - button, button);
    right = c.fullscreen.left;
    if (m_video.AudioTracks().size() > 1)
    {
        c.audio = place(right - button, button);
        right = c.audio.left;
    }
    if (!m_video.SubtitleTracks().empty())
    {
        c.subtitles = place(right - button, button);
        right = c.subtitles.left;
    }

    // The times take the width of the longest they can be, in the mono font, so they don't jiggle.
    bool hours = m_video.Duration() >= 3600.0;
    int timeWidth = m_renderer ? static_cast<int>(std::ceil(m_renderer->TextWidth(hours ? L"0:00:00" : L"00:00", Font::kMono))) : 0;
    int gap = Scale(10);
    int volumeWidth = Scale(theme::kViewerVolumeWidth);
    int minSeek = Scale(80);

    // The volume slider, then the times, go when the bar is too short for them and a usable seek bar.
    bool withVolume = right - x - button - volumeWidth - 2 * (timeWidth + gap) - gap >= minSeek;
    bool withTimes = right - x - button - (withVolume ? volumeWidth : 0) - 2 * (timeWidth + gap) - gap >= minSeek;
    if (withVolume)
    {
        c.volume = { right - Scale(6) - volumeWidth, c.bar.top, right - Scale(6), c.bar.bottom };
        right = c.volume.left;
    }
    c.mute = place(right - button, button);
    right = c.mute.left - gap;
    if (withTimes)
    {
        c.elapsed = { x, c.bar.top, x + timeWidth, c.bar.bottom };
        x = c.elapsed.right + gap;
        c.duration = { right - timeWidth, c.bar.top, right, c.bar.bottom };
        right = c.duration.left - gap;
    }
    c.seek = { x, c.bar.top, std::max(x, right), c.bar.bottom };
    return c;
}

// The time at 'x' on the seek bar.
double Viewer::SeekTime(int x) const
{
    Controls c = LayoutControls();
    if (Width(c.seek) <= 0)
        return 0.0;
    double fraction = static_cast<double>(x - c.seek.left) / Width(c.seek);
    return std::max(0.0, std::min(1.0, fraction)) * m_video.Duration();
}

int Viewer::VolumeAt(int x) const
{
    Controls c = LayoutControls();
    if (Width(c.volume) <= 0)
        return m_video.Volume();
    double fraction = static_cast<double>(x - c.volume.left) / Width(c.volume);
    return static_cast<int>(std::lround(std::max(0.0, std::min(1.0, fraction)) * 100));
}

// The tracks the open menu offers; subtitles start with "Off".
std::vector<std::wstring> Viewer::MenuItems() const
{
    std::vector<std::wstring> items;
    if (m_menu == Menu::kSubtitles)
    {
        if (!m_video.SubtitleTracks().empty())
            items.push_back(strings::kSubtitlesOff);
        for (const VideoPlayer::Track &track : m_video.SubtitleTracks())
            items.push_back(track.label);
    }
    else if (m_menu == Menu::kAudio)
    {
        for (const VideoPlayer::Track &track : m_video.AudioTracks())
            items.push_back(track.label);
    }
    return items;
}

// The item of the track playing, or -1.
int Viewer::MenuSelected() const
{
    if (m_menu == Menu::kSubtitles)
    {
        if (m_video.Subtitle() == 0)
            return 0;
        const std::vector<VideoPlayer::Track> &tracks = m_video.SubtitleTracks();
        for (size_t i = 0; i < tracks.size(); i++)
        {
            if (tracks[i].id == m_video.Subtitle())
                return static_cast<int>(i) + 1;
        }
    }
    else if (m_menu == Menu::kAudio)
    {
        const std::vector<VideoPlayer::Track> &tracks = m_video.AudioTracks();
        for (size_t i = 0; i < tracks.size(); i++)
        {
            if (tracks[i].id == m_video.Audio())
                return static_cast<int>(i);
        }
    }
    return -1;
}

void Viewer::ChooseMenuItem(int item)
{
    if (m_menu == Menu::kSubtitles)
    {
        const std::vector<VideoPlayer::Track> &tracks = m_video.SubtitleTracks();
        if (item == 0)
            m_video.SetSubtitle(0);
        else if (item > 0 && static_cast<size_t>(item) <= tracks.size())
            m_video.SetSubtitle(tracks[item - 1].id);
    }
    else if (m_menu == Menu::kAudio)
    {
        const std::vector<VideoPlayer::Track> &tracks = m_video.AudioTracks();
        if (item >= 0 && static_cast<size_t>(item) < tracks.size())
            m_video.SetAudio(tracks[item].id);
    }
    m_menu = Menu::kNone;
    m_hover.Reset(m_hWnd, kViewerHoverTimer);
    Invalidate();
}

// Above its button, as wide as its longest track, and kept inside the viewer.
RECT Viewer::MenuRect() const
{
    std::vector<std::wstring> items = MenuItems();
    Controls c = LayoutControls();
    const RECT &button = m_menu == Menu::kSubtitles ? c.subtitles : c.audio;
    if (items.empty() || !m_renderer || Width(button) <= 0)
        return {};
    float widest = 0.0f;
    for (const std::wstring &item : items)
        widest = std::max(widest, m_renderer->TextWidth(item, Font::kBody));
    int pad = Scale(12);
    int width = std::max(Scale(160), static_cast<int>(std::ceil(widest)) + 2 * pad + Scale(theme::kViewerMenuMark));
    width = std::min(width, Width(m_bounds) - 2 * Scale(theme::kViewerMargin));
    int height = static_cast<int>(items.size()) * Scale(theme::kViewerMenuRow) + 2 * Scale(4);
    int right = std::min(static_cast<int>(button.right) + Scale(4), static_cast<int>(m_bounds.right) - Scale(theme::kViewerMargin));
    int bottom = c.bar.top - Scale(6);
    RECT rect = { right - width, bottom - height, right, bottom };
    if (rect.top < m_bounds.top + Scale(theme::kViewerMargin))
        rect.top = m_bounds.top + Scale(theme::kViewerMargin);
    return rect;
}

// ---- Zoom and pan ----

double Viewer::FitZoom() const
{
    double width = m_bounds.right - m_bounds.left;
    double height = m_bounds.bottom - m_bounds.top;
    if (m_frame.sourceWidth == 0 || m_frame.sourceHeight == 0 || width <= 0 || height <= 0)
        return 1.0;
    if (m_book)
        return m_pageFit == PageFit::kWidth ? width / m_frame.sourceWidth :
            std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight);
    return std::min(1.0, std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight));
}

double Viewer::MinZoom() const
{
    double width = m_bounds.right - m_bounds.left;
    double height = m_bounds.bottom - m_bounds.top;
    if (!m_book || m_frame.sourceWidth == 0 || m_frame.sourceHeight == 0 || width <= 0 || height <= 0)
        return FitZoom();
    return std::min(FitZoom(), std::min(width / m_frame.sourceWidth, height / m_frame.sourceHeight));
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
    zoom = std::max(MinZoom(), std::min(zoom, kMaxZoom));

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
    return m_open && !m_dragging && !m_seeking && !m_settingVolume && !m_pageSeeking && m_overlayTo == 0.0f &&
        OverlayLevel() == 0.0f;
}

// Shows the name and buttons, and hides them again once the pointer stands still for a while. Over a video, the
// time shown moves on meanwhile.
void Viewer::ShowOverlays()
{
    if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    FadeOverlays(1.0f);
    SetTimer(m_hWnd, kViewerIdleTimer, theme::kOverlayIdleMs, nullptr);
    if (VideoShown())
    {
        m_video.ReadPosition();
        SetTimer(m_hWnd, kViewerVideoTimer, kVideoTimeMs, nullptr);
    }
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
    return -1;
}

// ---- Painting ----

void Viewer::Paint(Renderer &renderer)
{
    if (!m_open)
        return;
    renderer.FillRect(ToRectF(m_bounds), theme::kViewerBackdrop);
    // The video's window covers the viewer, and libmpv draws the overlays on it.
    if (VideoShown())
        return;

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
            if (m_book)
                m_reader.Show(m_pageShown, true);
            else
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
        for (int button : { kCloseButton, kPreviousButton, kNextButton })
        {
            if (ButtonShown(button))
                PaintButton(renderer, button, level);
        }
        if (PageBarShown())
            PaintPageBar(renderer, level);
        PaintInfo(renderer, level);
    }
    renderer.PopClip();
}

// Why there is no picture: it can't be shown, the file is gone, or it is still being decoded (or opened, for a
// video).
void Viewer::PaintMessage(Renderer &renderer) const
{
    bool failed = m_status == ImageDecoder::Status::kFailed;
    bool missing = m_status == ImageDecoder::Status::kMissing;
    if (IsVideo())
    {
        failed = m_video.GetStatus() == VideoPlayer::Status::kFailed;
        missing = m_video.GetStatus() == VideoPlayer::Status::kMissing;
    }

    const wchar_t *message = nullptr;
    const wchar_t *hint = nullptr;
    if (failed)
    {
        message = strings::kCantShow;
        hint = strings::kOpenHint;
    }
    else if (missing)
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

    float middle = (m_bounds.left + m_bounds.right) / 2.0f;
    float bottom = m_bounds.bottom - margin;
    if (VideoShown())
        bottom = static_cast<float>(LayoutControls().bar.top - Scale(theme::kViewerBarGap));
    else if (PageBarShown())
        bottom = static_cast<float>(LayoutPageBar().bar.top - Scale(theme::kViewerBarGap));
    info.panel = D2D1::RectF(std::floor(middle - width / 2), bottom - Scale(theme::kViewerInfoHeight),
        std::floor(middle - width / 2) + std::ceil(width), bottom);
    info.shown = true;
    return info;
}

void Viewer::PaintInfo(Renderer &renderer, float level) const
{
    Info info = LayoutInfo(renderer);
    if (!info.shown)
        return;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(info.panel, radius, Faded(theme::kOverlay, level));

    LONG left = static_cast<LONG>(info.panel.left + Scale(12));
    RECT nameRect = { left, static_cast<LONG>(info.panel.top), left + static_cast<LONG>(info.nameWidth) + 1,
        static_cast<LONG>(info.panel.bottom) };
    renderer.DrawTextLine(info.name, Font::kBody, nameRect, Faded(theme::kText1, level));
    if (!info.detail.empty())
    {
        RECT detailRect = { nameRect.right + Scale(10), nameRect.top, static_cast<LONG>(info.panel.right), nameRect.bottom };
        renderer.DrawTextLine(info.detail, Font::kMono, detailRect, Faded(theme::kText2, level));
    }
}

// The control bar: play, the time played, the seek bar, the length, the volume, the tracks and the button that
// covers the window.
void Viewer::PaintControls(Renderer &renderer, float level) const
{
    Controls c = LayoutControls();
    if (Width(c.bar) <= 0)
        return;
    renderer.FillRoundedRect(ToRectF(c.bar), static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kOverlay, level));

    for (int button : { kPlayButton, kMuteButton, kSubtitleButton, kAudioButton, kFullscreenButton })
    {
        if (ButtonShown(button))
            PaintControlButton(renderer, button, level);
    }

    bool hours = m_video.Duration() >= 3600.0;
    if (Width(c.elapsed) > 0)
    {
        renderer.DrawTextLine(FormatTime(m_video.Position(), hours), Font::kMono, c.elapsed, Faded(theme::kText1, level),
            DWRITE_TEXT_ALIGNMENT_TRAILING);
        renderer.DrawTextLine(FormatTime(m_video.Duration(), hours), Font::kMono, c.duration, Faded(theme::kText3, level));
    }
    double played = m_video.Duration() > 0.0 ? m_video.Position() / m_video.Duration() : 0.0;
    PaintSlider(renderer, c.seek, played, theme::kYuri, level);
    if (Width(c.volume) > 0)
        PaintSlider(renderer, c.volume, m_video.Muted() ? 0.0 : m_video.Volume() / 100.0, theme::kText2, level);
}

// A thin track filled up to 'fraction', with a short upright handle there; filled from its right end when
// 'fromRight', for pages that turn right to left.
void Viewer::PaintSlider(Renderer &renderer, const RECT &area, double fraction, const D2D1_COLOR_F &fill, float level,
    bool fromRight) const
{
    if (Width(area) <= 0)
        return;
    fraction = std::max(0.0, std::min(1.0, fraction));
    if (fromRight)
        fraction = 1.0 - fraction;
    float middle = (area.top + area.bottom) / 2.0f;
    float thickness = static_cast<float>(Scale(4));
    float left = static_cast<float>(area.left);
    float right = static_cast<float>(area.right);
    float at = std::floor(left + static_cast<float>((right - left) * fraction) + 0.5f);
    float radius = thickness / 4.0f;
    D2D1_RECT_F track = D2D1::RectF(left, middle - thickness / 2, right, middle + thickness / 2);
    renderer.FillRoundedRect(track, radius, Faded(theme::kSliderTrack, level));
    if (fromRight && at < right)
        renderer.FillRoundedRect(D2D1::RectF(at, track.top, right, track.bottom), radius, Faded(fill, level));
    else if (!fromRight && at > left)
        renderer.FillRoundedRect(D2D1::RectF(left, track.top, at, track.bottom), radius, Faded(fill, level));
    float handle = static_cast<float>(Scale(3));
    float tall = static_cast<float>(Scale(6));
    renderer.FillRoundedRect(D2D1::RectF(at - handle / 2, middle - tall, at + handle / 2, middle + tall), radius,
        Faded(theme::kText1, level));
}

// The buttons of the control bar, drawn with lines like the viewer's others.
void Viewer::PaintControlButton(Renderer &renderer, int button, float level) const
{
    D2D1_RECT_F rect = ToRectF(ButtonRect(button));
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    bool open = (button == kSubtitleButton && m_menu == Menu::kSubtitles) || (button == kAudioButton && m_menu == Menu::kAudio);
    float hover = std::max(m_hover.Level(button), open ? 1.0f : 0.0f);
    if (hover > 0.0f)
        renderer.FillRoundedRect(rect, radius, Faded(theme::kOverlayHover, level * hover));

    D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
    float s = static_cast<float>(Scale(5));
    float width = Scale(3) / 2.0f;
    D2D1_COLOR_F color = Faded(theme::kText1, level);
    switch (button)
    {
    case kPlayButton:
        if (m_video.Paused() || m_video.Ended())
        {
            const D2D1_POINT_2F triangle[] = { { c.x - s * 0.7f, c.y - s }, { c.x + s, c.y }, { c.x - s * 0.7f, c.y + s } };
            renderer.FillPolygon(triangle, 3, color);
        }
        else
        {
            float bar = static_cast<float>(Scale(3));
            renderer.FillRect(D2D1::RectF(c.x - s * 0.7f, c.y - s, c.x - s * 0.7f + bar, c.y + s), color);
            renderer.FillRect(D2D1::RectF(c.x + s * 0.7f - bar, c.y - s, c.x + s * 0.7f, c.y + s), color);
        }
        break;

    case kMuteButton:
    {
        // A speaker, with waves, or a cross when muted.
        float x = c.x - s * 0.4f;
        const D2D1_POINT_2F speaker[] = { { x - s * 0.8f, c.y - s * 0.4f }, { x - s * 0.2f, c.y - s * 0.4f },
            { x + s * 0.5f, c.y - s }, { x + s * 0.5f, c.y + s }, { x - s * 0.2f, c.y + s * 0.4f }, { x - s * 0.8f, c.y + s * 0.4f } };
        renderer.DrawPolyline(speaker, 6, color, width, true);
        if (m_video.Muted() || m_video.Volume() == 0)
        {
            renderer.DrawLine(D2D1::Point2F(x + s * 1.1f, c.y - s * 0.4f), D2D1::Point2F(x + s * 1.9f, c.y + s * 0.4f), color, width);
            renderer.DrawLine(D2D1::Point2F(x + s * 1.1f, c.y + s * 0.4f), D2D1::Point2F(x + s * 1.9f, c.y - s * 0.4f), color, width);
        }
        else
        {
            const D2D1_POINT_2F inner[] = { { x + s * 1.0f, c.y - s * 0.4f }, { x + s * 1.25f, c.y }, { x + s * 1.0f, c.y + s * 0.4f } };
            renderer.DrawPolyline(inner, 3, color, width);
            if (m_video.Volume() > 50)
            {
                const D2D1_POINT_2F outer[] = { { x + s * 1.5f, c.y - s * 0.8f }, { x + s * 1.95f, c.y }, { x + s * 1.5f, c.y + s * 0.8f } };
                renderer.DrawPolyline(outer, 3, color, width);
            }
        }
        break;
    }

    case kSubtitleButton:
    {
        // A screen with two lines of text in it, dimmed while the subtitles are off.
        D2D1_COLOR_F lines = m_video.Subtitle() == 0 ? Faded(theme::kText3, level) : color;
        renderer.DrawRoundedRect(D2D1::RectF(c.x - s * 1.3f, c.y - s, c.x + s * 1.3f, c.y + s), static_cast<float>(Scale(2)),
            lines, width);
        renderer.DrawLine(D2D1::Point2F(c.x - s * 0.7f, c.y + s * 0.35f), D2D1::Point2F(c.x + s * 0.1f, c.y + s * 0.35f), lines, width);
        renderer.DrawLine(D2D1::Point2F(c.x + s * 0.45f, c.y + s * 0.35f), D2D1::Point2F(c.x + s * 0.7f, c.y + s * 0.35f), lines, width);
        renderer.DrawLine(D2D1::Point2F(c.x - s * 0.7f, c.y - s * 0.3f), D2D1::Point2F(c.x - s * 0.35f, c.y - s * 0.3f), lines, width);
        renderer.DrawLine(D2D1::Point2F(c.x, c.y - s * 0.3f), D2D1::Point2F(c.x + s * 0.7f, c.y - s * 0.3f), lines, width);
        break;
    }

    case kAudioButton:
    {
        // A sound wave.
        const float heights[] = { 0.4f, 0.9f, 0.6f, 1.0f, 0.5f };
        for (int i = 0; i < 5; i++)
        {
            float x = c.x + (i - 2) * s * 0.5f;
            renderer.DrawLine(D2D1::Point2F(x, c.y - s * heights[i]), D2D1::Point2F(x, c.y + s * heights[i]), color, width);
        }
        break;
    }

    case kFullscreenButton:
    {
        // Four corners pointing out, or in to leave.
        float in = m_fullscreen ? 1.0f : 0.0f;
        for (int corner = 0; corner < 4; corner++)
        {
            float dx = corner % 2 ? 1.0f : -1.0f;
            float dy = corner / 2 ? 1.0f : -1.0f;
            D2D1_POINT_2F tip = D2D1::Point2F(c.x + dx * s * (in ? 0.3f : 1.0f), c.y + dy * s * (in ? 0.3f : 1.0f));
            float arm = s * 0.6f * (in ? 1.0f : -1.0f);
            const D2D1_POINT_2F points[] = { { tip.x, tip.y + dy * arm }, tip, { tip.x + dx * arm, tip.y } };
            renderer.DrawPolyline(points, 3, color, width);
        }
        break;
    }
    }
}

// The open menu of tracks: the one playing is marked, like a required tag.
void Viewer::PaintMenu(Renderer &renderer, float level) const
{
    RECT menu = MenuRect();
    std::vector<std::wstring> items = MenuItems();
    if (items.empty() || Width(menu) <= 0)
        return;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(ToRectF(menu), radius, Faded(theme::kOverlay, level));

    int selected = MenuSelected();
    int pad = Scale(12);
    int mark = Scale(theme::kViewerMenuMark);
    for (int item = 0; item < static_cast<int>(items.size()); item++)
    {
        RECT row = ButtonRect(kMenuItem + item);
        if (row.bottom > menu.bottom)
            break;
        float hover = m_hover.Level(kMenuItem + item);
        if (hover > 0.0f)
        {
            D2D1_RECT_F fill = ToRectF(row);
            fill.left += Scale(4);
            fill.right -= Scale(4);
            renderer.FillRoundedRect(fill, radius, Faded(theme::kOverlayHover, level * hover));
        }
        if (item == selected)
        {
            float size = static_cast<float>(Scale(7));
            float x = static_cast<float>(row.left + pad);
            float y = (row.top + row.bottom - size) / 2.0f;
            renderer.FillRoundedRect(D2D1::RectF(x, y, x + size, y + size), static_cast<float>(Scale(1)), Faded(theme::kYuri, level));
        }
        RECT text = { row.left + pad + mark, row.top, row.right - pad, row.bottom };
        renderer.DrawTextLine(items[item], Font::kBody, text, Faded(item == selected ? theme::kText1 : theme::kText2, level));
    }
}

// ---- Input ----

Viewer::Action Viewer::OnKeyDown(WPARAM key, bool repeat)
{
    // Over a video: the left and right arrows seek, up and down set the volume, K plays and pauses, M mutes, and
    // Esc closes the menu first. Page Up and Page Down still step through the files.
    if (VideoShown())
    {
        switch (key)
        {
        case VK_ESCAPE:
            if (!repeat && m_menu != Menu::kNone)
            {
                m_menu = Menu::kNone;
                Invalidate();
                return Action::kNone;
            }
            break;
        case VK_LEFT:
        case VK_RIGHT:
            // ShowOverlays reads where the video is first.
            ShowOverlays();
            m_video.Seek(m_video.Position() + (key == VK_LEFT ? -kSeekStepSeconds : kSeekStepSeconds), true);
            Invalidate();
            return Action::kNone;
        case VK_UP:
        case VK_DOWN:
            m_video.SetVolume(m_video.Volume() + (key == VK_UP ? kVolumeStep : -kVolumeStep));
            ShowOverlays();
            Invalidate();
            return Action::kNone;
        case 'K':
            if (!repeat)
            {
                m_video.TogglePause();
                ShowOverlays();
                Invalidate();
            }
            return Action::kNone;
        case 'M':
            if (!repeat)
            {
                m_video.ToggleMute();
                ShowOverlays();
                Invalidate();
            }
            return Action::kNone;
        }
    }

    // In a book: the left and right arrows turn the page that way, up and down scroll it and turn it at its ends,
    // Page Up and Page Down scroll a view at a time, Home and End go to the first and last page, W switches how it
    // fits and R which way the pages turn. With Ctrl, the arrows, Page Up, Page Down, Home and End step through the
    // files instead.
    if (PageBarShown() && !VideoShown())
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        double view = m_bounds.bottom - m_bounds.top;
        switch (key)
        {
        case VK_LEFT:
        case VK_RIGHT:
            if (ctrl)
                Step(key == VK_LEFT ? -1 : 1);
            else
                TurnPage(SideStep(key == VK_LEFT ? -1 : 1));
            return Result(Action::kNone);
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        {
            bool back = key == VK_UP || key == VK_PRIOR;
            if (ctrl && (key == VK_PRIOR || key == VK_NEXT))
                Step(back ? -1 : 1);
            else if (!ScrollPage((back ? -view : view) * (key == VK_UP || key == VK_DOWN ? kArrowScroll : 1.0 - kPageOverlap)))
                TurnPage(back ? -1 : 1, back);
            return Result(Action::kNone);
        }
        case VK_HOME:
        case VK_END:
            if (ctrl)
                Step(key == VK_HOME ? -static_cast<int>(m_index) : static_cast<int>(m_paths.size() - 1 - m_index));
            else
                GoToPage(key == VK_HOME ? 0 : m_pageCount - 1);
            return Result(Action::kNone);
        case 'W':
            if (!repeat)
                SetPageFit(m_pageFit == PageFit::kPage ? PageFit::kWidth : PageFit::kPage);
            return Action::kNone;
        case 'R':
            if (!repeat)
            {
                m_rightToLeft = !m_rightToLeft;
                SaveSetting(kRightToLeftValue, m_rightToLeft);
                Invalidate();
            }
            return Action::kNone;
        }
    }

    switch (key)
    {
    // Esc first leaves the whole window to the rest of Corpus again.
    case VK_ESCAPE:
        if (repeat)
            return Action::kNone;
        if (!m_fullscreen)
            return Action::kClose;
        SetFullscreen(false);
        break;
    case VK_SPACE:
        return repeat ? Action::kNone : Action::kClose;
    case VK_RETURN:
        return repeat ? Action::kNone : Action::kOpenExternally;
    case 'F':
        if (!repeat)
            SetFullscreen(!m_fullscreen);
        break;
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
    return Result(Action::kNone);
}

Viewer::Action Viewer::OnLButtonDown(POINT pt, bool doubleClick)
{
    if (!Contains(pt))
        return Action::kNone;

    int button = ButtonAt(pt);
    // A click anywhere but in it closes the open menu, and does nothing else.
    if (m_menu != Menu::kNone && (button < kMenuItem))
    {
        bool onItsButton = (button == kSubtitleButton && m_menu == Menu::kSubtitles) ||
            (button == kAudioButton && m_menu == Menu::kAudio);
        m_menu = Menu::kNone;
        Invalidate();
        if (onItsButton || button < 0)
            return Action::kNone;
    }

    switch (button)
    {
    case -1:
        break;
    case kCloseButton:
        return doubleClick ? Action::kNone : Action::kClose;
    case kPreviousButton:
    case kNextButton:
    {
        int side = button == kPreviousButton ? -1 : 1;
        if (m_book)
            TurnPage(SideStep(side));
        else
            Step(side);
        return Result(Action::kNone);
    }
    case kFitButton:
        SetPageFit(m_pageFit == PageFit::kPage ? PageFit::kWidth : PageFit::kPage);
        return Action::kNone;
    case kDirectionButton:
        m_rightToLeft = !m_rightToLeft;
        SaveSetting(kRightToLeftValue, m_rightToLeft);
        Invalidate();
        return Action::kNone;
    case kPlayButton:
        m_video.TogglePause();
        Invalidate();
        return Action::kNone;
    case kMuteButton:
        m_video.ToggleMute();
        Invalidate();
        return Action::kNone;
    case kSubtitleButton:
        if (!doubleClick)
            m_menu = Menu::kSubtitles;
        Invalidate();
        return Action::kNone;
    case kAudioButton:
        if (!doubleClick)
            m_menu = Menu::kAudio;
        Invalidate();
        return Action::kNone;
    case kFullscreenButton:
        SetFullscreen(!m_fullscreen);
        return Result(Action::kNone);
    default:
        ChooseMenuItem(button - kMenuItem);
        return Action::kNone;
    }

    if (VideoShown())
    {
        // The seek bar and the volume slider follow the pointer while the button is down.
        Controls c = LayoutControls();
        if (m_overlayTo > 0.0f && PtInRect(&c.seek, pt) && m_video.Duration() > 0.0)
        {
            m_seeking = true;
            m_video.Seek(SeekTime(pt.x), false);
            SetCapture(m_hWnd);
            Invalidate();
            return Action::kNone;
        }
        if (m_overlayTo > 0.0f && PtInRect(&c.volume, pt))
        {
            m_settingVolume = true;
            m_video.SetVolume(VolumeAt(pt.x));
            SetCapture(m_hWnd);
            Invalidate();
            return Action::kNone;
        }
        if (m_overlayTo > 0.0f && PtInRect(&c.bar, pt))
            return Action::kNone;

        // A click plays or pauses; a double-click covers the window, or leaves it, and undoes its first click.
        m_video.TogglePause();
        if (doubleClick)
            SetFullscreen(!m_fullscreen);
        Invalidate();
        return Result(Action::kNone);
    }

    // A book: the slider goes to the page under the pointer, and follows it while the button is down. A click near
    // either side turns the page that way once the button goes up, unless it dragged; a double-click there turns
    // it again, and one in the middle switches how the page fits.
    if (PageBarShown())
    {
        PageBar b = LayoutPageBar();
        if (m_overlayTo > 0.0f && PtInRect(&b.slider, pt))
        {
            m_pageSeeking = true;
            GoToPage(PageAt(pt.x));
            SetCapture(m_hWnd);
            return Action::kNone;
        }
        if (m_overlayTo > 0.0f && PtInRect(&b.bar, pt))
            return Action::kNone;

        double width = Width(m_bounds);
        int side = pt.x < m_bounds.left + width * kTurnZone ? -1 : pt.x >= m_bounds.right - width * kTurnZone ? 1 : 0;
        if (doubleClick)
        {
            if (side != 0)
                TurnPage(SideStep(side));
            else
                SetPageFit(m_pageFit == PageFit::kPage ? PageFit::kWidth : PageFit::kPage);
            return Action::kNone;
        }
        m_clickTurn = side != 0;
        m_pressedAt = pt;
        if (m_clickTurn && !m_bitmap)
            SetCapture(m_hWnd);
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

    if (m_pageSeeking)
    {
        if (moved)
            GoToPage(PageAt(pt.x));
        return;
    }
    // A press that drags no longer turns the page.
    if (m_clickTurn && (std::abs(pt.x - m_pressedAt.x) > Scale(4) || std::abs(pt.y - m_pressedAt.y) > Scale(4)))
        m_clickTurn = false;
    if (m_dragging)
    {
        double zoom = Zoom();
        m_x = m_dragX - (pt.x - m_dragFrom.x) / zoom;
        m_y = m_dragY - (pt.y - m_dragFrom.y) / zoom;
        KeepInView();
        Invalidate();
        return;
    }
    if (m_seeking)
    {
        if (moved)
        {
            m_video.Seek(SeekTime(pt.x), false);
            Invalidate();
        }
        return;
    }
    if (m_settingVolume)
    {
        if (moved)
        {
            m_video.SetVolume(VolumeAt(pt.x));
            Invalidate();
        }
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
    // Lands on the very frame let go at.
    if (m_seeking)
        m_video.Seek(SeekTime(m_pointer.x), true);
    bool turn = m_clickTurn && m_open && m_book;
    m_clickTurn = false;
    if (m_dragging || m_seeking || m_settingVolume || m_pageSeeking || GetCapture() == m_hWnd)
        ReleaseCapture();
    m_dragging = m_seeking = m_settingVolume = m_pageSeeking = false;
    if (turn)
        TurnPage(SideStep(m_pressedAt.x < (m_bounds.left + m_bounds.right) / 2 ? -1 : 1));
}

// A click to turn the page outlives the capture: the grid lets go of the pointer before the viewer hears of the
// button going up.
void Viewer::OnCaptureChanged()
{
    m_dragging = m_seeking = m_settingVolume = m_pageSeeking = false;
}

void Viewer::OnMouseWheel(POINT pt, int delta)
{
    if (!Contains(pt))
        return;
    if (VideoShown())
    {
        m_video.SetVolume(m_video.Volume() + kVolumeStep * delta / WHEEL_DELTA);
        ShowOverlays();
        Invalidate();
    }
    // A book scrolls the page, and turns it once the wheel goes on past its end by a notch; a page that fits
    // turns at each notch. Ctrl zooms.
    else if (PageBarShown() && (GetKeyState(VK_CONTROL) & 0x8000) == 0)
    {
        double view = m_bounds.bottom - m_bounds.top;
        if (ScrollPage(-view * kWheelScroll * delta / WHEEL_DELTA))
        {
            m_wheelTurn = 0;
            return;
        }
        // Back the other way: start over.
        if ((m_wheelTurn < 0) != (delta < 0))
            m_wheelTurn = 0;
        m_wheelTurn += delta;
        if (std::abs(m_wheelTurn) >= WHEEL_DELTA)
        {
            bool back = m_wheelTurn > 0;
            m_wheelTurn = 0;
            TurnPage(back ? -1 : 1, back);
        }
    }
    else
        ZoomAt(pt, Zoom() * std::pow(kZoomStep, delta / static_cast<double>(WHEEL_DELTA)));
}

void Viewer::OnTimer(UINT_PTR timer)
{
    switch (timer)
    {
    case kViewerIdleTimer:
        KillTimer(m_hWnd, kViewerIdleTimer);
        // Not while the pointer rests on a button, or drags something, or while a video is paused or has a menu
        // open.
        if (m_dragging || m_seeking || m_settingVolume || m_pageSeeking || ButtonAt(m_pointer) >= 0 ||
            (VideoShown() && (m_video.Paused() || m_video.Ended() || m_menu != Menu::kNone)))
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

    case kViewerVideoTimer:
        // Only while the controls show: a video watched with them hidden doesn't wake the window.
        if (!VideoShown() || (m_overlayTo == 0.0f && OverlayLevel() == 0.0f))
        {
            KillTimer(m_hWnd, kViewerVideoTimer);
            break;
        }
        m_video.ReadPosition();
        if (ControlsState() != m_controlsState)
            Invalidate();
        break;

    case kViewerProgressTimer:
        SaveProgress();
        break;

    case kViewerLoadingTimer:
        KillTimer(m_hWnd, kViewerLoadingTimer);
        if (IsVideo() ? m_video.GetStatus() == VideoPlayer::Status::kLoading : m_status == ImageDecoder::Status::kNothing)
        {
            m_loadingShown = true;
            m_bitmap.Reset();
            Invalidate();
        }
        break;
    }
}
