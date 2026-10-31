#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include "file_kind.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    // Between the cover and the name under it, and the least cover worth showing.
    const int kCoverGap = 14;
    const int kMinCover = 48;
    // The strip of music playing with the viewer closed: its side padding and the gap between its three parts, the
    // buttons (the play button is bigger), the cover (smaller in a narrow strip) with its margin and the gap to the
    // text, the widest and narrowest the middle part gets, the width from which the right part shows (and the room it
    // needs), the row of the seek bar, and the volume slider.
    const int kStripPadding = 16;
    const int kStripGap = 16;
    const int kStripButton = 28;
    const int kStripPlaySize = 32;
    const int kStripCover = 52;
    const int kStripCoverNarrow = 44;
    const int kStripCoverMargin = 8;
    const int kStripCoverGap = 12;
    const int kStripMiddle = 560;
    const int kStripMiddleMin = 280;
    const int kStripWideWindow = 800;
    const int kStripRightMin = 180;
    const int kStripSeekRow = 14;
    const int kStripVolumeWidth = 84;
    // Previous goes back a song only in the first seconds of one.
    const double kRestartSeconds = 3.0;

    // "song" for C:\Music\song.flac.
    std::wstring Title(const std::string &path)
    {
        std::wstring wide = Utf8ToWide(path);
        size_t slash = wide.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            wide.erase(0, slash + 1);
        size_t dot = wide.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0)
            wide.erase(dot);
        return wide;
    }
}

// In the middle of what the close button and the bar leave, with the name and details under it.
RECT Viewer::CoverRect() const
{
    if (!m_renderer)
        return {};
    int margin = Scale(theme::kViewerMargin);
    int button = Scale(theme::kViewerButton);
    int top = m_bounds.top + margin + button;
    int bottom = m_bounds.bottom - Scale(theme::kViewerShade);
    int meta = static_cast<int>(std::ceil(m_renderer->LineHeight(Font::kBodyStrong) + m_renderer->LineHeight(Font::kMono)));
    int gap = Scale(kCoverGap);
    int side = std::min({ Scale(theme::kViewerCover), bottom - top - gap - meta, Width(m_bounds) - 4 * margin - 2 * button });
    if (side < Scale(kMinCover))
        side = 0;
    int groupTop = top + std::max(0, bottom - top - (side + gap + meta)) / 2;
    int left = (m_bounds.left + m_bounds.right - side) / 2;
    return { left, groupTop, left + side, groupTop + side };
}

// The tile the grid draws for music: a pair of notes, and the extension under them when 'badge' has it.
void Viewer::PaintMusicTile(Renderer &renderer, const RECT &cover, const std::wstring &badge) const
{
    float radius = static_cast<float>(Scale(3));
    renderer.FillRoundedRect(ToRectF(cover), radius, theme::kControl);
    float s = Width(cover) * 0.3f;
    float cx = (cover.left + cover.right) / 2.0f;
    float cy = cover.top + Width(cover) * (badge.empty() ? 0.5f : 0.42f);
    auto P = [&](float x, float y) { return D2D1::Point2F(cx + x * s, cy + y * s); };
    float line = 1.5f * Scale(4) / 4.0f;
    renderer.DrawLine(P(-0.18f, 0.24f), P(-0.18f, -0.34f), theme::kText3, line);
    renderer.DrawLine(P(0.3f, 0.16f), P(0.3f, -0.42f), theme::kText3, line);
    const D2D1_POINT_2F beam[] = { P(-0.18f, -0.34f), P(0.3f, -0.42f), P(0.3f, -0.3f), P(-0.18f, -0.22f) };
    renderer.FillPolygon(beam, 4, theme::kText3);
    renderer.FillCircle(P(-0.29f, 0.26f), 0.12f * s, theme::kText3);
    renderer.FillCircle(P(0.19f, 0.18f), 0.12f * s, theme::kText3);
    if (badge.empty())
        return;

    float pad = static_cast<float>(Scale(8));
    float width = std::ceil(renderer.TextWidth(badge, Font::kMono)) + 2 * pad;
    float height = static_cast<float>(Scale(20));
    float top = cy + s * 0.7f;
    D2D1_RECT_F plate = D2D1::RectF(std::floor(cx - width / 2), top, std::floor(cx - width / 2) + width, top + height);
    renderer.FillRoundedRect(plate, static_cast<float>(Scale(theme::kChipRadius)), theme::kChip);
    RECT text = { static_cast<LONG>(plate.left), static_cast<LONG>(plate.top), static_cast<LONG>(plate.right),
        static_cast<LONG>(plate.bottom) };
    renderer.DrawTextLine(badge, Font::kMono, text, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// The cover is the video's window over CoverRect; without one, the tile the grid draws for music stands there: a
// pair of notes and the extension. Under it, the name, where it is in the list and the format.
void Viewer::PaintAudio(Renderer &renderer) const
{
    if (!Playing())
    {
        PaintMessage(renderer);
        return;
    }
    RECT cover = CoverRect();
    if (Width(cover) > 0 && !m_video.HasCover())
    {
        std::wstring extension = Utf8ToWide(FileExtension(m_paths[m_index]));
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
        PaintMusicTile(renderer, cover, extension);
    }

    int margin = Scale(theme::kViewerMargin);
    int top = cover.bottom + (Width(cover) > 0 ? Scale(kCoverGap) : 0);
    int nameHeight = static_cast<int>(std::ceil(renderer.LineHeight(Font::kBodyStrong)));
    RECT name = { m_bounds.left + margin, top, m_bounds.right - margin, top + nameHeight };
    renderer.DrawTextLine(Title(m_paths[m_index]), Font::kBodyStrong, name, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);

    std::wstring detail;
    if (m_paths.size() > 1)
        detail = FormatCount(m_index + 1) + L" " + strings::kOf + L" " + FormatCount(m_paths.size()) + L" \u00B7 ";
    std::wstring extension = Utf8ToWide(FileExtension(m_paths[m_index]));
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    detail += extension;
    RECT details = { name.left, name.bottom, name.right, name.bottom + static_cast<int>(std::ceil(renderer.LineHeight(Font::kMono))) };
    renderer.DrawTextLine(detail, Font::kMono, details, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// What to do when the music ends: again for repeat one, else the next in the play order (or nothing, at its end).
void Viewer::PlayNextAudio()
{
    // An ended song starts over when played.
    if (m_repeat == Repeat::kOne)
        m_video.TogglePause();
    else
        StepAudio(1);
}

// Next or previous in the play order, which is the grid's unless shuffling. Only music: videos, pictures and books in
// the list are passed over. Past the end it stops, unless repeating all. Previous, as in other players, starts the
// song over once it is a few seconds in, and where nothing comes before it.
bool Viewer::StepAudio(int delta)
{
    if (delta < 0 && m_video.Position() > kRestartSeconds)
    {
        m_video.Seek(0.0, true);
        return true;
    }
    size_t song = 0;
    bool wrap = m_repeat == Repeat::kAll;
    PlayOrder &order = SongOrder();
    if (!(delta > 0 ? order.Next(SongIndex(), wrap, song) : order.Previous(SongIndex(), wrap, song)))
    {
        if (delta < 0)
            m_video.Seek(0.0, true);
        return false;
    }
    PlayAudioAt(song);
    return true;
}

void Viewer::PlayAudioAt(size_t index)
{
    if (m_open && !m_parked)
    {
        m_first = false;
        Show(index);
        ShowOverlays();
    }
    else
    {
        (m_parked ? m_song.index : m_index) = index;
        m_video.Open(m_hWnd, StripCoverRect(), SongPaths()[index]);
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
    }
}

// The music of the list in the grid's order, with the one playing found among them.
void Viewer::BuildOrder()
{
    std::vector<size_t> songs;
    for (size_t i = 0; i < m_paths.size(); i++)
    {
        if (IsPlayableAudio(m_paths[i]))
            songs.push_back(i);
    }
    m_order.Reset(std::move(songs), m_index, m_shuffle);
}

void Viewer::SetShuffle(bool shuffle)
{
    m_shuffle = shuffle;
    SongOrder().SetShuffle(shuffle, SongIndex());
    SavePlayback();
    InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
}

void Viewer::CycleRepeat()
{
    m_repeat = m_repeat == Repeat::kOff ? Repeat::kAll : m_repeat == Repeat::kAll ? Repeat::kOne : Repeat::kOff;
    SavePlayback();
    InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
}

RECT Viewer::ShadeRect() const
{
    return { m_bounds.left, std::max(m_bounds.top, m_bounds.bottom - Scale(theme::kViewerShade)), m_bounds.right, m_bounds.bottom };
}

bool Viewer::ShadeShown() const
{
    return Playing() || PageBarShown() || m_tagRow;
}

void Viewer::PaintShade(Renderer &renderer, float level) const
{
    renderer.FillGradient(ToRectF(ShadeRect()), D2D1::ColorF(0, 0.0f), D2D1::ColorF(0, 0.55f * level));
}

int Viewer::StripHeight() const
{
    return Scale(theme::kStripHeight);
}

// Where the cover of the music playing goes: at the strip's left, as LayoutStrip puts it.
RECT Viewer::StripCoverRect() const
{
    int side = std::min(Scale(kStripCover), Height(m_stripBounds) - Scale(2 * kStripCoverMargin));
    if (side <= 0)
        return {};
    int left = m_stripBounds.left + Scale(kStripPadding);
    int top = (m_stripBounds.top + m_stripBounds.bottom - side) / 2;
    return { left, top, left + side, top + side };
}

// The cover, name and details at the left; shuffle, previous, play, next, repeat and the seek bar in the middle, at
// most kStripMiddle wide and centered; the volume and the rest at the right. Under kStripWideWindow, or when the
// right part would be cramped, that part is only mute and the x, and the middle takes the rest of its room.
Viewer::Strip Viewer::LayoutStrip() const
{
    Strip strip;
    if (!m_renderer || SongPaths().empty() || Width(m_stripBounds) <= 0)
        return strip;
    const RECT &b = m_stripBounds;
    int pad = Scale(kStripPadding);
    int gap = Scale(kStripGap);
    int height = Height(b);
    int avail = std::max(0, Width(b) - 2 * pad - 2 * gap);

    int middle = std::max(Scale(kStripMiddleMin), std::min(Scale(kStripMiddle), avail * 9 / 20));
    int side = std::max(0, (avail - middle) / 2);
    bool wide = Width(b) >= Scale(kStripWideWindow) && side >= Scale(kStripRightMin);
    int leftWidth = side;
    // A narrow strip keeps mute and the x at its right end, so the music can always be silenced or stopped.
    int narrowRight = 2 * Scale(kStripButton) + Scale(4) + gap;
    if (!wide)
    {
        // The middle takes a little more than half of the room, the left the rest.
        int room = std::max(0, avail - narrowRight);
        middle = std::max(std::min(Scale(kStripMiddleMin), room), room * 13 / 23);
        leftWidth = room - middle;
    }
    int left = b.left + pad;
    int middleLeft = wide ? left + side + gap : left + leftWidth + gap;

    // Left: the cover (smaller when narrow), then the name over the details.
    strip.cover = StripCoverRect();
    if (!wide)
    {
        int side44 = std::min(Width(strip.cover), Scale(kStripCoverNarrow));
        strip.cover.right = strip.cover.left + side44;
        strip.cover.top = (b.top + b.bottom - side44) / 2;
        strip.cover.bottom = strip.cover.top + side44;
    }
    int textLeft = strip.cover.right + Scale(kStripCoverGap);
    int textRight = std::max(textLeft, left + leftWidth);
    int nameHeight = static_cast<int>(std::ceil(m_renderer->LineHeight(Font::kBodyStrong)));
    int detailHeight = static_cast<int>(std::ceil(m_renderer->LineHeight(Font::kMono)));
    int textTop = (b.top + b.bottom - nameHeight - detailHeight) / 2;
    strip.name = { textLeft, textTop, textRight, textTop + nameHeight };
    strip.detail = { textLeft, strip.name.bottom, textRight, strip.name.bottom + detailHeight };
    // The plate that lights up under the pointer hugs the text.
    int textWidth = static_cast<int>(std::ceil(std::max(m_renderer->TextWidth(Title(SongPaths()[SongIndex()]), Font::kBodyStrong),
        m_renderer->TextWidth(StripDetail(), Font::kMono))));
    int plate = Scale(4);
    strip.now = { strip.cover.left - plate, strip.cover.top - plate, std::min(textLeft + textWidth, textRight) + plate,
        strip.cover.bottom + plate };
    if (textRight - textLeft < Scale(20))
        strip.now.right = strip.cover.right + plate;
    strip.name.right = strip.detail.right = std::max(textLeft, std::min(textRight, textLeft + textWidth));

    // Middle: the buttons over the seek bar.
    int small = Scale(kStripButton);
    int big = Scale(kStripPlaySize);
    int buttonGap = Scale(6);
    int playMargin = Scale(4);
    int rowHeight = big + Scale(4) + Scale(kStripSeekRow);
    int rowTop = b.top + (height - rowHeight) / 2;
    int buttons = 4 * small + big + 2 * playMargin + 4 * buttonGap;
    // Too narrow for all five: shuffle and repeat go.
    bool all = middle >= buttons;
    if (!all)
        buttons = 2 * small + big + 2 * playMargin + 2 * buttonGap;
    int x = middleLeft + (middle - buttons) / 2;
    int smallTop = rowTop + (big - small) / 2;
    auto place = [&](RECT &rect, int size, int top) { rect = { x, top, x + size, top + size }; };
    if (all)
    {
        place(strip.shuffle, small, smallTop);
        x += small + buttonGap;
    }
    place(strip.previous, small, smallTop);
    x += small + buttonGap + playMargin;
    place(strip.play, big, rowTop);
    x += big + playMargin + buttonGap;
    place(strip.next, small, smallTop);
    x += small + buttonGap;
    if (all)
        place(strip.repeat, small, smallTop);

    bool hours = m_video.Duration() >= 3600.0;
    int timeWidth = static_cast<int>(std::ceil(m_renderer->TextWidth(hours ? L"0:00:00" : L"00:00", Font::kMono)));
    int seekTop = rowTop + big + Scale(4);
    int seekBottom = seekTop + Scale(kStripSeekRow);
    int timeGap = Scale(10);
    strip.elapsed = { middleLeft, seekTop, middleLeft + timeWidth, seekBottom };
    strip.duration = { middleLeft + middle - timeWidth, seekTop, middleLeft + middle, seekBottom };
    strip.seek = { strip.elapsed.right + timeGap, seekTop, std::max(strip.elapsed.right + timeGap, strip.duration.left - timeGap),
        seekBottom };
    int rightEdge = b.right - pad;
    int top = (b.top + b.bottom - small) / 2;
    if (!wide)
    {
        strip.close = { rightEdge - small, top, rightEdge, top + small };
        strip.mute = { strip.close.left - Scale(4) - small, top, strip.close.left - Scale(4), top + small };
        return strip;
    }

    // Right: the speed, mute and volume, then the buttons that open the viewer and stop the music.
    auto placeRight = [&](RECT &rect, int width, int rectTop, int rectHeight)
    {
        rect = { rightEdge - width, rectTop, rightEdge, rectTop + rectHeight };
        rightEdge = rect.left - Scale(4);
    };
    placeRight(strip.close, small, top, small);
    placeRight(strip.open, small, top, small);
    int sliderHeight = Scale(kStripSeekRow);
    placeRight(strip.volume, Scale(kStripVolumeWidth), (b.top + b.bottom - sliderHeight) / 2, sliderHeight);
    placeRight(strip.mute, small, top, small);
    if (m_video.Speed() != 1.0)
    {
        wchar_t label[32];
        swprintf(label, 32, L"%g×", m_video.Speed());
        int width = static_cast<int>(std::ceil(m_renderer->TextWidth(label, Font::kMono))) + 2 * Scale(6);
        placeRight(strip.speed, width, top, small);
    }
    return strip;
}

// "FLAC · 24-bit / 96 kHz · 5 of 12": the format, the sound for lossless music (the depth only when the file's
// header declares it, the rate once libmpv says it), and where it is among the music of the list.
std::wstring Viewer::StripDetail() const
{
    std::wstring detail = Utf8ToWide(FileExtension(SongPaths()[SongIndex()]));
    std::transform(detail.begin(), detail.end(), detail.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    int bits = m_video.BitDepth();
    int rate = m_video.SampleRate();
    if (bits > 0 || rate > 0)
    {
        wchar_t sound[48] = L" · ";
        int used = 3;
        if (bits > 0)
            used += swprintf(sound + used, 45, L"%d-bit%ls", bits, m_video.FloatDepth() ? L" float" : L"");
        if (bits > 0 && rate > 0)
            used += swprintf(sound + used, 45, L" / ");
        if (rate > 0)
            swprintf(sound + used, 45, L"%g kHz", rate / 1000.0);
        detail += sound;
    }
    const std::vector<size_t> &songs = SongOrder().Songs();
    auto at = std::lower_bound(songs.begin(), songs.end(), SongIndex());
    if (songs.size() > 1 && at != songs.end() && *at == SongIndex())
        detail += L" · " + FormatCount(static_cast<size_t>(at - songs.begin()) + 1) + L" " + strings::kOf + L" " +
            FormatCount(songs.size());
    return detail;
}

RECT Viewer::StripButtonRect(const Strip &strip, int button) const
{
    switch (button)
    {
    case kStripShuffle:  return strip.shuffle;
    case kStripPrevious: return strip.previous;
    case kStripPlay:     return strip.play;
    case kStripNext:     return strip.next;
    case kStripRepeat:   return strip.repeat;
    case kStripSeek:     return strip.seek;
    case kStripMute:     return strip.mute;
    case kStripVolume:   return strip.volume;
    case kStripOpen:     return strip.open;
    case kStripClose:    return strip.close;
    case kStripNow:      return strip.now;
    }
    return {};
}

int Viewer::StripButtonAt(const Strip &strip, POINT pt) const
{
    if (!StripContains(pt))
        return -1;
    for (int button = kStripShuffle; button <= kStripNow; button++)
    {
        RECT rect = StripButtonRect(strip, button);
        if (Width(rect) > 0 && PtInRect(&rect, pt))
            return button;
    }
    return -1;
}

double Viewer::StripSeekTime(const Strip &strip, int x) const
{
    if (Width(strip.seek) <= 0)
        return 0.0;
    double fraction = static_cast<double>(x - strip.seek.left) / Width(strip.seek);
    return std::max(0.0, std::min(1.0, fraction)) * m_video.Duration();
}

int Viewer::StripVolumeAt(const Strip &strip, int x) const
{
    if (Width(strip.volume) <= 0)
        return m_video.Volume();
    double fraction = static_cast<double>(x - strip.volume.left) / Width(strip.volume);
    return static_cast<int>(std::lround(std::max(0.0, std::min(1.0, fraction)) * 100));
}

namespace
{
    // Points along a cubic curve, for the bends of the strip's icons.
    void Curve(std::vector<D2D1_POINT_2F> &points, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, D2D1_POINT_2F d)
    {
        for (int i = 1; i <= 6; i++)
        {
            float t = i / 6.0f, u = 1.0f - t;
            points.push_back({ u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y });
        }
    }
}

// A line on a slider track: it grows and turns violet under the pointer, with a knob that comes up on it.
void Viewer::PaintStripSlider(Renderer &renderer, const RECT &area, double fraction, float hover) const
{
    if (Width(area) <= 0)
        return;
    fraction = std::max(0.0, std::min(1.0, fraction));
    float middle = (area.top + area.bottom) / 2.0f;
    float thin = static_cast<float>(Scale(3));
    float thick = static_cast<float>(Scale(5));
    float thickness = thin + (thick - thin) * hover;
    float left = static_cast<float>(area.left);
    float right = static_cast<float>(area.right);
    float at = std::floor(left + static_cast<float>((right - left) * fraction) + 0.5f);
    float radius = thickness / 2.0f;
    renderer.FillRoundedRect(D2D1::RectF(left, middle - radius, right, middle + radius), radius, theme::kPlate);
    if (at > left)
        renderer.FillRoundedRect(D2D1::RectF(left, middle - radius, at, middle + radius), radius, Mix(theme::kText2, theme::kYuri, hover));
    if (hover > 0.0f)
        renderer.FillCircle(D2D1::Point2F(at, middle), Scale(5) * hover, Faded(theme::kText1, hover));
}

// One of the strip's buttons: boxless line icons that light up under the pointer, and turn violet when on; play is
// the one filled square.
void Viewer::PaintStripButton(Renderer &renderer, const Strip &strip, int button) const
{
    RECT box = StripButtonRect(strip, button);
    if (Width(box) <= 0)
        return;
    D2D1_RECT_F rect = ToRectF(box);
    float hover = std::max(m_hover.Level(button), 0.0f);
    float radius = static_cast<float>(Scale(button == kStripPlay ? 4 : 3));
    bool on = (button == kStripShuffle && m_shuffle) || (button == kStripRepeat && m_repeat != Repeat::kOff);
    D2D1_COLOR_F color = on ? theme::kYuri : Mix(theme::kText2, theme::kText1, hover);
    if (button == kStripPlay)
    {
        renderer.FillRoundedRect(rect, radius, Mix(theme::kYuri, theme::kYuriHover, hover));
        color = theme::kOnAccent;
    }
    else if (hover > 0.0f)
        renderer.FillRoundedRect(rect, radius, Faded(theme::kHover, hover));

    // Icons are drawn on a 16-unit grid, as in the design.
    float u = Scale(16) / 16.0f;
    D2D1_POINT_2F origin = D2D1::Point2F((rect.left + rect.right) / 2.0f - 8 * u, (rect.top + rect.bottom) / 2.0f - 8 * u);
    auto P = [&](float x, float y) { return D2D1::Point2F(origin.x + x * u, origin.y + y * u); };
    float width = 1.5f * Scale(4) / 4.0f;
    auto line = [&](std::vector<D2D1_POINT_2F> &&points) { renderer.DrawPolyline(points.data(), points.size(), color, width); };
    auto arrow = [&](float x, float y, float dx)
    {
        line({ P(x - dx, y - 1.5f), P(x, y), P(x - dx, y + 1.5f) });
    };

    switch (button)
    {
    case kStripShuffle:
    {
        std::vector<D2D1_POINT_2F> down = { P(1.5f, 4), P(3.7f, 4) };
        Curve(down, P(3.7f, 4), P(6.7f, 4), P(7.7f, 12), P(10.7f, 12));
        down.push_back(P(13.5f, 12));
        line(std::move(down));
        line({ P(1.5f, 12), P(3.7f, 12), P(5, 11.3), P(6.4f, 9.7f) });
        std::vector<D2D1_POINT_2F> up = { P(8.5f, 6.3f) };
        Curve(up, P(8.5f, 6.3f), P(9.2f, 5), P(10, 4), P(11.3f, 4));
        up.push_back(P(13.5f, 4));
        line(std::move(up));
        arrow(14, 4, 1.5f);
        arrow(14, 12, 1.5f);
        break;
    }
    case kStripPrevious:
    {
        const D2D1_POINT_2F triangle[] = { P(13, 3.5f), P(13, 12.5f), P(6, 8) };
        renderer.FillPolygon(triangle, 3, color);
        renderer.DrawLine(P(3.5f, 3), P(3.5f, 13), color, width);
        break;
    }
    case kStripNext:
    {
        const D2D1_POINT_2F triangle[] = { P(3, 3.5f), P(3, 12.5f), P(10, 8) };
        renderer.FillPolygon(triangle, 3, color);
        renderer.DrawLine(P(12.5f, 3), P(12.5f, 13), color, width);
        break;
    }
    case kStripPlay:
        if (StripPaused())
        {
            const D2D1_POINT_2F triangle[] = { P(4.5f, 2.5f), P(4.5f, 13.5f), P(13, 8) };
            renderer.FillPolygon(triangle, 3, color);
        }
        else
        {
            renderer.FillRect(D2D1::RectF(P(3.5f, 2.5f).x, P(3.5f, 2.5f).y, P(6.5f, 13.5f).x, P(6.5f, 13.5f).y), color);
            renderer.FillRect(D2D1::RectF(P(9.5f, 2.5f).x, P(9.5f, 2.5f).y, P(12.5f, 13.5f).x, P(12.5f, 13.5f).y), color);
        }
        break;
    case kStripRepeat:
        line({ P(2, 7), P(2, 5.6f), P(2.6f, 4.6f), P(4, 4), P(13, 4) });
        line({ P(11, 2), P(13, 4), P(11, 6) });
        line({ P(14, 9), P(14, 10.4f), P(13.4f, 11.4f), P(12, 12), P(3, 12) });
        line({ P(5, 14), P(3, 12), P(5, 10) });
        // Repeat one carries a 1.
        if (m_repeat == Repeat::kOne)
            line({ P(6.8f, 6.6f), P(8.2f, 5.8f), P(8.2f, 10.2f) });
        break;
    case kStripMute:
    {
        const D2D1_POINT_2F speaker[] = { P(2, 6), P(2, 10), P(4.5f, 10), P(8, 13), P(8, 3), P(4.5f, 6) };
        renderer.DrawPolyline(speaker, 6, color, width, true);
        if (m_video.Muted() || m_video.Volume() == 0)
        {
            renderer.DrawLine(P(10.5f, 6), P(14.5f, 10), color, width);
            renderer.DrawLine(P(14.5f, 6), P(10.5f, 10), color, width);
        }
        else
        {
            line({ P(10.5f, 5.5f), P(11.5f, 8), P(10.5f, 10.5f) });
            if (m_video.Volume() > 50)
                line({ P(12.3f, 3.7f), P(13.8f, 8), P(12.3f, 12.3f) });
        }
        break;
    }
    case kStripOpen:
        line({ P(9.5f, 2.5f), P(13.5f, 2.5f), P(13.5f, 6.5f) });
        line({ P(13.5f, 2.5f), P(8.5f, 7.5f) });
        line({ P(6.5f, 3.5f), P(4, 3.5f), P(2.5f, 5), P(2.5f, 12), P(4, 13.5f), P(11, 13.5f), P(12.5f, 12), P(12.5f, 9.5f) });
        break;
    case kStripClose:
        renderer.DrawLine(P(4, 4), P(12, 12), color, width);
        renderer.DrawLine(P(12, 4), P(4, 12), color, width);
        break;
    }
}

// On the base color like the rest of Corpus's bars, in the three parts LayoutStrip makes. The cover of music that
// has one is the video's window, over the cover's place.
void Viewer::PaintStrip(Renderer &renderer) const
{
    if (!StripShown() || Width(m_stripBounds) <= 0)
        return;
    Strip strip = LayoutStrip();
    D2D1_RECT_F bounds = ToRectF(m_stripBounds);
    renderer.FillRect(bounds, theme::kBase);
    renderer.FillRect(D2D1::RectF(bounds.left, bounds.top, bounds.right, bounds.top + 1.0f), theme::kStroke);

    float now = m_hover.Level(kStripNow);
    if (now > 0.0f)
        renderer.FillRoundedRect(ToRectF(strip.now), static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kHover, now));
    if (!m_video.HasCover() && Width(strip.cover) > 0)
        PaintMusicTile(renderer, strip.cover, L"");
    renderer.DrawTextLine(Title(SongPaths()[SongIndex()]), Font::kBodyStrong, strip.name, theme::kText1);
    renderer.DrawTextLine(StripDetail(), Font::kMono, strip.detail, theme::kText3);

    for (int button : { kStripShuffle, kStripPrevious, kStripPlay, kStripNext, kStripRepeat, kStripMute, kStripOpen, kStripClose })
        PaintStripButton(renderer, strip, button);

    bool hours = m_video.Duration() >= 3600.0;
    renderer.DrawTextLine(FormatTime(m_video.Position(), hours), Font::kMono, strip.elapsed, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);
    renderer.DrawTextLine(FormatTime(m_video.Duration(), hours), Font::kMono, strip.duration, theme::kText3);
    double played = m_video.Duration() > 0.0 ? m_video.Position() / m_video.Duration() : 0.0;
    float seekHover = m_stripSeeking ? 1.0f : m_hover.Level(kStripSeek);
    PaintStripSlider(renderer, strip.seek, played, seekHover);
    float volumeHover = m_stripVolume ? 1.0f : m_hover.Level(kStripVolume);
    PaintStripSlider(renderer, strip.volume, m_video.Muted() ? 0.0 : m_video.Volume() / 100.0, volumeHover);
    if (Width(strip.speed) > 0)
    {
        wchar_t label[32];
        swprintf(label, 32, L"%g×", m_video.Speed());
        renderer.DrawTextLine(label, Font::kMono, strip.speed, theme::kYuri, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // The time under the pointer rides over the seek bar.
    if (seekHover > 0.0f && Width(strip.seek) > 0 && m_video.Duration() > 0.0)
    {
        std::wstring time = FormatTime(StripSeekTime(strip, m_pointer.x), hours);
        int padding = Scale(6);
        int width = static_cast<int>(std::ceil(renderer.TextWidth(time, Font::kMono))) + 2 * padding;
        int height = static_cast<int>(std::ceil(renderer.LineHeight(Font::kMono))) + Scale(4);
        int x = std::max<int>(strip.seek.left, std::min<int>(m_pointer.x, strip.seek.right));
        int left = std::max<int>(m_stripBounds.left, std::min<int>(x - width / 2, m_stripBounds.right - width));
        RECT tip = { left, strip.seek.top - Scale(6) - height, left + width, strip.seek.top - Scale(6) };
        renderer.FillRoundedRect(ToRectF(tip), static_cast<float>(Scale(3)), Faded(theme::kChip, seekHover));
        renderer.DrawTextLine(time, Font::kMono, tip, Faded(theme::kText1, seekHover), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

Viewer::Action Viewer::OnStripClick(POINT pt, bool doubleClick)
{
    if (!StripContains(pt) || doubleClick)
        return Action::kNone;
    m_pointer = pt;
    Strip strip = LayoutStrip();
    switch (StripButtonAt(strip, pt))
    {
    case kStripShuffle:
        SetShuffle(!m_shuffle);
        break;
    case kStripRepeat:
        CycleRepeat();
        break;
    case kStripPrevious:
        StepAudio(-1);
        break;
    case kStripNext:
        StepAudio(1);
        break;
    case kStripPlay:
        m_video.TogglePause();
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        break;
    case kStripMute:
        m_video.ToggleMute();
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        break;
    case kStripSeek:
        if (m_video.Duration() > 0.0)
        {
            m_stripSeeking = true;
            m_video.Seek(StripSeekTime(strip, pt.x), false);
            SetCapture(m_hWnd);
            InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        }
        break;
    case kStripVolume:
        m_stripVolume = true;
        m_video.SetVolume(StripVolumeAt(strip, pt.x));
        SetCapture(m_hWnd);
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        break;
    case kStripOpen:
    case kStripNow:
        return m_parked ? ShowSong() : Action::kReopen;
    case kStripClose:
        StopStrip();
        return Action::kStrip;
    }
    return Action::kNone;
}

// The pointer moved: what is under it lights up, and a seek or volume drag follows it.
void Viewer::OnStripMove(POINT pt)
{
    if (!StripShown())
        return;
    bool moved = pt.x != m_pointer.x || pt.y != m_pointer.y;
    m_pointer = pt;
    Strip strip = LayoutStrip();
    if (m_stripSeeking || m_stripVolume)
    {
        if (moved)
        {
            if (m_stripSeeking)
                m_video.Seek(StripSeekTime(strip, pt.x), false);
            else
                m_video.SetVolume(StripVolumeAt(strip, pt.x));
            InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        }
        return;
    }
    int button = StripButtonAt(strip, pt);
    bool changed = m_hover.Set(m_hWnd, kViewerHoverTimer, button);
    // The time over the seek bar moves with the pointer.
    if (changed || (moved && button == kStripSeek))
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
}

bool Viewer::StripTipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    if (!StripShown())
        return false;
    Strip strip = LayoutStrip();
    int button = StripButtonAt(strip, pt);
    switch (button)
    {
    case kStripShuffle:  text = m_shuffle ? strings::kTipShuffleOn : strings::kTipShuffleOff; break;
    case kStripPrevious: text = strings::kTipStripPrevious; break;
    case kStripPlay:     text = StripPaused() ? strings::kTipPlay : strings::kTipPause; break;
    case kStripNext:     text = strings::kTipStripNext; break;
    case kStripRepeat:
        text = m_repeat == Repeat::kAll ? strings::kTipRepeatAll : m_repeat == Repeat::kOne ? strings::kTipRepeatOne : strings::kTipRepeatOff;
        break;
    case kStripMute:     text = strings::kTipStripMute; break;
    case kStripOpen:     text = strings::kTipStripOpen; break;
    case kStripClose:    text = strings::kTipStripClose; break;
    default:
        return false;
    }
    anchor = StripButtonRect(strip, button);
    return true;
}

void Viewer::StopStrip()
{
    if (m_parked)
    {
        m_video.Stop();
        m_video.SetCovered(false);
        m_stripSeeking = m_stripVolume = false;
        if (GetCapture() == m_hWnd)
            ReleaseCapture();
        m_hover.Reset(m_hWnd, kViewerHoverTimer);
        KillTimer(m_hWnd, kViewerStripTimer);
        m_song = Playlist();
        m_parked = false;
        m_relayout = true;
        return;
    }
    m_background = false;
    m_stripSeeking = m_stripVolume = false;
    if (GetCapture() == m_hWnd)
        ReleaseCapture();
    m_hover.Reset(m_hWnd, kViewerHoverTimer);
    KillTimer(m_hWnd, kViewerStripTimer);
    m_video.Stop();
    m_video.SetCovered(false);
    m_paths.clear();
    m_audio = false;
}

bool Viewer::SongOn() const
{
    return m_audio && (m_video.GetStatus() == VideoPlayer::Status::kPlaying || m_video.GetStatus() == VideoPlayer::Status::kLoading);
}

// Music that plays while the file shown changes keeps its list in m_song; the viewer's takes the new files.
void Viewer::ParkSong()
{
    if (m_parked || m_paths.empty() || !SongOn())
        return;
    m_song = { m_paths, m_index, m_order };
    m_parked = true;
    m_relayout = true;
}

void Viewer::DropSong()
{
    if (!m_parked)
        return;
    m_parked = m_displaced = false;
    m_song = Playlist();
    KillTimer(m_hWnd, kViewerStripTimer);
    m_relayout = true;
    // The viewer's order may have missed a shuffle switched meanwhile.
    m_order.SetShuffle(m_shuffle, m_index);
}

void Viewer::DisplaceSong()
{
    if (!m_parked || m_displaced)
        return;
    m_video.ReadPosition();
    m_resumeAt = m_video.Position();
    m_resumePaused = m_video.Paused();
    // The video plays at its own speed, from 1x; the music gets its speed back with the player.
    m_resumeSpeed = m_video.Speed();
    m_video.SetSpeed(1.0);
    m_displaced = true;
    m_stripSeeking = m_stripVolume = false;
    KillTimer(m_hWnd, kViewerStripTimer);
    m_relayout = true;
}

void Viewer::ResumeSong()
{
    if (!m_parked)
        return;
    if (m_displaced)
    {
        m_displaced = false;
        m_video.SetSpeed(m_resumeSpeed);
        m_video.Open(m_hWnd, StripCoverRect(), m_song.paths[m_song.index], m_resumePaused, m_resumeAt);
        m_relayout = true;
    }
    if (m_open)
        SetTimer(m_hWnd, kViewerStripTimer, kStripTimeMs, nullptr);
    PlaceSongCover();
}

void Viewer::PlaceSongCover()
{
    m_video.SetBounds(StripShown() && Height(m_stripBounds) > 0 ? StripCoverRect() : RECT{});
}

// The strip's name or open button: the music takes the viewer over, and the file shown gives way.
Viewer::Action Viewer::ShowSong()
{
    SaveProgress(true);
    m_decoder.Close();
    m_reader.Close();
    m_book = m_reflow = false;
    m_pageCount = 0;
    m_location.clear();
    for (UINT_PTR timer : { kViewerFrameTimer, kViewerLoadingTimer, kViewerReflowTimer, kViewerStripTimer })
        KillTimer(m_hWnd, timer);
    m_paths = std::move(m_song.paths);
    m_index = m_song.index;
    m_order = std::move(m_song.order);
    m_song = Playlist();
    m_parked = false;
    m_audio = true;
    m_status = ImageDecoder::Status::kNothing;
    m_frame = DecodedFrame();
    m_next = DecodedFrame();
    m_bitmap.Reset();
    m_upload = m_animated = m_hasNext = m_due = m_reloading = m_loadingShown = false;
    m_first = false;
    RefreshTags();
    m_video.SetBounds(CoverRect());
    m_video.SetCovered(m_covered);
    ShowOverlays();
    Invalidate();
    return Action::kStrip;
}
