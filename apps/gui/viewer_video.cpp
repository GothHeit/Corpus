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
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
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
}

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
