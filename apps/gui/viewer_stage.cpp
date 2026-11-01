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
#include <cstdlib>
#include <unordered_set>

// The Now Playing stage of music in the viewer: see Viewer in viewer.hpp.

namespace
{
    // The least cover worth showing, and the room between it and the text under it.
    const int kMinCover = 48;
    const int kCoverGap = 28;
    // The cover is at most this share of the stage's width, and never shown past this many times its own size.
    const int kCoverShare = 60;
    const int kMaxUpscale = 2;
    // The text block under the cover: at least this wide, and the gaps between its lines.
    const int kBlockMin = 320;
    const int kLineGap = 2;
    const int kFormatGap = 8;
    const int kSeekGap = 18;
    const int kSeekRow = 16;
    const int kControlsGap = 12;
    const int kControlsRow = 44;
    // The transport buttons, the play button, the gap between them, and the volume slider.
    const int kButton = 32;
    const int kPlayWidth = 44;
    const int kPlayHeight = 36;
    const int kButtonGap = 12;
    const int kVolumeWidth = 72;
    // The rail: the side padding, the heading's distance from the top, the rows.
    const int kRailPad = 20;
    const int kRailTop = 64;
    const int kRailRowHeight = 56;
    const int kRailThumb = 40;
    const int kRailClearWidth = 56;
    const int kRailRemove = 24;
    // The rail's headings: the queue's, the list's under a queue, and the list's alone (a title and a caption).
    const int kRailQueueHead = 36;
    const int kRailListHead = 44;
    const int kRailListHeadAlone = 48;
    const int kRailEndRow = 40;
    // The "Play on" switch: its track, its knob, and the room round it and its label that a click counts in.
    const int kSwitchWidth = 30;
    const int kSwitchHeight = 16;
    const int kSwitchHit = 96;
    // How long the knob takes to slide over.
    const ULONGLONG kSwitchSlideMs = 120;
    // The backdrop is made at this width, which Direct2D stretches over the viewer: blur costs nothing at that size.
    const UINT kBackdropWidth = 192;
    const UINT kBackdropMaxHeight = 320;
    const UINT kCoverReadSide = 128;
    const int kBlurRadius = 14;
    // How much the corners of the backdrop darken, and how much more color it gets.
    const float kVignette = 0.38f;
    const float kSaturation = 1.35f;
    // The cover's shadow, in two layers like a real object's: a wide soft one (ambient light) and a tight one under
    // the edge (contact). Each is a blur radius, the distance it falls below the cover, and a darkness, all in DIPs.
    struct ShadowLayer { float sigma; float drop; float alpha; };
    const ShadowLayer kShadowLayers[2] = { { 28.0f, 24.0f, 0.62f }, { 6.0f, 5.0f, 0.42f } };
    const int kShadowReach = 112;  // how far past the cover the shadow bitmap reaches
    const int kShadowShrink = 4;   // the shadow is made at 1/4 size: it is smooth, so Direct2D stretches it
    const int kCoverTriesMax = 12;
    // The backdrop is darkened more towards the bottom, where the controls are.
    const float kShadeTop = 0.55f;
    const float kShadeBottom = 0.78f;

    // 'rect' fitted to a shape: as wide as it for wide, as tall for tall, in its middle.
    RECT Fitted(const RECT &slot, int width, int height)
    {
        if (width <= 0 || height <= 0 || Width(slot) <= 0 || Height(slot) <= 0)
            return slot;
        int w = Width(slot), h = Height(slot);
        if (static_cast<long long>(width) * h >= static_cast<long long>(height) * w)
            h = std::max(1, static_cast<int>(std::lround(static_cast<double>(w) * height / width)));
        else
            w = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * width / height)));
        int left = slot.left + (Width(slot) - w) / 2;
        int top = slot.top + (Height(slot) - h) / 2;
        return { left, top, left + w, top + h };
    }

    // Blurs one plane of floats with three passes of a box blur (close to a Gaussian). Outside the plane counts as
    // 'edge' when extend is true (the backdrop) and as 0 when not (the shadow).
    void BlurPlane(std::vector<float> &plane, int width, int height, int radius, bool extend)
    {
        if (radius < 1)
            return;
        std::vector<float> temp(plane.size());
        auto pass = [&](const std::vector<float> &from, std::vector<float> &to, bool horizontal)
        {
            int length = horizontal ? width : height;
            int lines = horizontal ? height : width;
            size_t step = horizontal ? 1 : static_cast<size_t>(width);
            size_t lineStep = horizontal ? static_cast<size_t>(width) : 1;
            float inverse = 1.0f / static_cast<float>(2 * radius + 1);
            for (int line = 0; line < lines; line++)
            {
                size_t base = static_cast<size_t>(line) * lineStep;
                auto at = [&](int i)
                {
                    if (i < 0 || i >= length)
                    {
                        if (!extend)
                            return 0.0f;
                        i = std::max(0, std::min(length - 1, i));
                    }
                    return from[base + static_cast<size_t>(i) * step];
                };
                float sum = 0.0f;
                for (int i = -radius; i <= radius; i++)
                    sum += at(i);
                for (int i = 0; i < length; i++)
                {
                    to[base + static_cast<size_t>(i) * step] = sum * inverse;
                    sum += at(i + radius + 1) - at(i - radius);
                }
            }
        };
        for (int i = 0; i < 3; i++)
        {
            pass(plane, temp, true);
            pass(temp, plane, false);
        }
    }

    // A box radius whose three passes make a blur of about this sigma.
    int RadiusForSigma(float sigma)
    {
        return std::max(1, static_cast<int>(std::lround((std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f) / 2.0f)));
    }

    // Noise in [-0.5, 0.5) that is the same every time, added before rounding to 8 bits so dark gradients don't band.
    float Dither(int x, int y)
    {
        unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFF) / 65536.0f - 0.5f;
    }

    BYTE ToByte(float value, int x, int y)
    {
        return static_cast<BYTE>(std::max(0.0f, std::min(255.0f, value + Dither(x, y))));
    }
}

// Fits 'slot' to the cover playing; a music file without a picture keeps the square its tile fills.
RECT Viewer::FitCover(const RECT &slot) const
{
    if (!m_video.HasCover())
        return slot;
    return Fitted(slot, m_video.Width(), m_video.Height());
}

// The stage is the viewer less the rail. In it, one group centered in the room the close button and the bottom edge
// leave: the cover, then the title, "artist · album", the format line, the seek bar and the controls.
Viewer::Stage Viewer::LayoutStage() const
{
    Stage s;
    if (!m_renderer || Width(m_bounds) <= 0)
        return s;
    Renderer &renderer = *m_renderer;
    int margin = Scale(theme::kViewerMargin);
    int railWidth = Width(m_bounds) >= Scale(theme::kViewerRailMin) ?
        std::min(Scale(theme::kViewerRailMax), Width(m_bounds) * 24 / 100) : 0;
    s.area = { m_bounds.left, m_bounds.top, m_bounds.right - railWidth, m_bounds.bottom };
    if (railWidth > 0)
        s.rail = { s.area.right, m_bounds.top, m_bounds.right, m_bounds.bottom };

    // Music with no artist or album leaves out the line for them, so the title stays by the format line.
    const VideoPlayer::Tags &songTags = m_video.MusicTags();
    bool hasBy = !songTags.artist.empty() || !songTags.album.empty();
    int titleHeight = static_cast<int>(std::ceil(renderer.LineHeight(Font::kTitle)));
    int subtitleHeight = hasBy ? static_cast<int>(std::ceil(renderer.LineHeight(Font::kBody))) : 0;
    int lineGap = hasBy ? Scale(kLineGap) : 0;
    int formatHeight = static_cast<int>(std::ceil(renderer.LineHeight(Font::kMono)));
    int seekRow = Scale(kSeekRow);
    int controlsRow = Scale(kControlsRow);
    int blockHeight = titleHeight + lineGap + subtitleHeight + Scale(kFormatGap) + formatHeight + Scale(kSeekGap) +
        seekRow + Scale(kControlsGap) + controlsRow;

    int top = m_bounds.top + margin + Scale(theme::kViewerButton) + Scale(8);
    int bottom = m_bounds.bottom - Scale(40);
    int gap = Scale(kCoverGap);
    int side = std::min(bottom - top - gap - blockHeight, Width(s.area) * kCoverShare / 100);
    // A small picture is not blown up past twice its size.
    if (m_video.HasCover() && m_video.Width() > 0 && m_video.Height() > 0)
        side = std::min(side, kMaxUpscale * std::max(m_video.Width(), m_video.Height()));
    if (side < Scale(kMinCover))
        side = 0;
    int used = blockHeight + (side > 0 ? side + gap : 0);
    int groupTop = top + std::max(0, bottom - top - used) / 2;
    int centerX = (s.area.left + s.area.right) / 2;
    s.slot = { centerX - side / 2, groupTop, centerX - side / 2 + side, groupTop + side };
    s.cover = FitCover(s.slot);
    // The cover filling the viewer, as a picture does: fitted to all of it.
    if (CoverFull())
        s.cover = Fitted(m_bounds, m_video.Width(), m_video.Height());

    int y = groupTop + (side > 0 ? side + gap : 0);
    int blockWidth = std::max(0, std::min(std::max(side, Scale(kBlockMin)), Width(s.area) - 2 * margin));
    int left = centerX - blockWidth / 2;
    int right = left + blockWidth;
    s.title = { left, y, right, y + titleHeight };
    y = s.title.bottom + lineGap;
    s.subtitle = { left, y, right, y + subtitleHeight };
    y = s.subtitle.bottom + Scale(kFormatGap);
    s.format = { left, y, right, y + formatHeight };
    y = s.format.bottom + Scale(kSeekGap);

    Strip &c = s.controls;
    bool hours = m_video.Duration() >= 3600.0;
    int timeWidth = static_cast<int>(std::ceil(renderer.TextWidth(hours ? L"0:00:00" : L"00:00", Font::kMono)));
    int timeGap = Scale(10);
    c.elapsed = { left, y, left + timeWidth, y + seekRow };
    c.duration = { right - timeWidth, y, right, y + seekRow };
    c.seek = { c.elapsed.right + timeGap, y, std::max(c.elapsed.right + timeGap, c.duration.left - timeGap), y + seekRow };
    y += seekRow + Scale(kControlsGap);

    int small = Scale(kButton), playWidth = Scale(kPlayWidth), playHeight = Scale(kPlayHeight), buttonGap = Scale(kButtonGap);
    int middle = y + controlsRow / 2;
    int x = centerX - (4 * small + playWidth + 4 * buttonGap) / 2;
    auto place = [&](RECT &rect, int width, int height)
    {
        rect = { x, middle - height / 2, x + width, middle - height / 2 + height };
        x += width + buttonGap;
    };
    place(c.shuffle, small, small);
    place(c.previous, small, small);
    place(c.play, playWidth, playHeight);
    place(c.next, small, small);
    place(c.repeat, small, small);
    // The volume at the right of the row, when the buttons leave it room.
    int volumeWidth = Scale(kVolumeWidth);
    int sliderHeight = Scale(kSeekRow);
    int volumeLeft = right - volumeWidth;
    if (volumeLeft - Scale(4) - small >= x + buttonGap)
    {
        c.volume = { volumeLeft, middle - sliderHeight / 2, right, middle - sliderHeight / 2 + sliderHeight };
        c.mute = { volumeLeft - Scale(4) - small, middle - small / 2, volumeLeft - Scale(4), middle - small / 2 + small };
    }

    if (railWidth > 0)
    {
        int pad = Scale(kRailPad);
        s.list = { s.rail.left + pad, m_bounds.top + Scale(kRailTop), s.rail.right - pad, s.rail.bottom - Scale(8) };
        s.fit = std::max(0, Height(s.list) / Scale(kRailRowHeight));
        s.rows = Height(s.list) / Scale(std::min(kRailQueueHead, kRailEndRow)) + 2;
    }
    return s;
}

// In the middle of the stage, where the cover's window goes.
RECT Viewer::CoverRect() const
{
    return LayoutStage().cover;
}

bool Viewer::ExpandShown() const
{
    return StageShown() && m_video.HasCover() && Width(CoverRect()) > 0;
}

// In the bottom right corner of the cover, inside it, where libmpv's window ends.
RECT Viewer::ExpandRect() const
{
    RECT cover = CoverRect();
    int size = Scale(theme::kViewerButton);
    int inset = Scale(10);
    return { cover.right - inset - size, cover.bottom - inset - size, cover.right - inset, cover.bottom - inset };
}

// Corner arrows pointing out to fill the viewer, or in to give it back.
void Viewer::PaintExpandButton(Renderer &renderer, float level) const
{
    D2D1_RECT_F rect = ToRectF(ExpandRect());
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(rect, radius, Faded(theme::kOverlay, level));
    float hover = m_hover.Level(kExpandButton);
    if (hover > 0.0f)
        renderer.FillRoundedRect(rect, radius, Faded(theme::kOverlayHover, level * hover));
    float cx = (rect.left + rect.right) / 2.0f, cy = (rect.top + rect.bottom) / 2.0f;
    bool full = CoverFull();
    float reach = static_cast<float>(Scale(5));
    float arm = static_cast<float>(Scale(4));
    float width = Scale(3) / 2.0f;
    D2D1_COLOR_F color = Faded(theme::kText1, level);
    // Two corner marks at opposite corners: at the outer corners pointing in to the middle for "fill", and the same
    // pair near the middle with their arms out for "give back".
    float k = full ? static_cast<float>(Scale(2)) : reach;
    float d = full ? 1.0f : -1.0f;
    for (float sign : { -1.0f, 1.0f })
    {
        D2D1_POINT_2F corner = D2D1::Point2F(cx + sign * k, cy - sign * k);
        const D2D1_POINT_2F points[] = { D2D1::Point2F(corner.x + sign * d * arm, corner.y), corner,
            D2D1::Point2F(corner.x, corner.y - sign * d * arm) };
        renderer.DrawPolyline(points, 3, color, width);
    }
}

void Viewer::SetCoverFull(bool full)
{
    if (full == m_coverFull)
        return;
    m_coverFull = full;
    // The cover's window takes its new place, and the button its own.
    m_video.SetBounds(CoverRect());
    m_hover.Reset(m_hWnd, kViewerHoverTimer);
    Invalidate();
}

int Viewer::RailRowHeight(const RailRow &row) const
{
    switch (row.kind)
    {
    case RailRow::kQueueHeader: return Scale(kRailQueueHead);
    case RailRow::kListHeader:  return Scale(m_queue.Empty() ? kRailListHeadAlone : kRailListHead);
    case RailRow::kEnd:         return Scale(kRailEndRow);
    default:                    return Scale(kRailRowHeight);
    }
}

std::vector<RECT> Viewer::RailRects(const Stage &stage, const std::vector<RailRow> &rows) const
{
    std::vector<RECT> rects;
    int y = stage.list.top;
    for (const RailRow &row : rows)
    {
        int height = RailRowHeight(row);
        rects.push_back({ stage.list.left, y, stage.list.right, y + height });
        y += height;
    }
    return rects;
}

std::vector<Viewer::RailRow> Viewer::RailShown(const Stage &stage, std::vector<RECT> &rects) const
{
    size_t total = 0;
    std::vector<RailRow> rows = UpNext(static_cast<size_t>(m_railScroll), static_cast<size_t>(stage.rows), total);
    rects = RailRects(stage, rows);
    return rows;
}

bool Viewer::ListedSongAt(POINT pt, std::string &path) const
{
    if (!StageShown())
        return false;
    Stage stage = LayoutStage();
    int button = StageButtonAt(stage, pt);
    if (button < kRailRow)
        return false;
    std::vector<RECT> rects;
    std::vector<RailRow> rows = RailShown(stage, rects);
    size_t row = static_cast<size_t>(button - kRailRow);
    if (row >= rows.size() || rows[row].kind != RailRow::kListed)
        return false;
    path = rows[row].path;
    return true;
}

bool Viewer::QueuedSongAt(POINT pt, std::string &path) const
{
    if (!StageShown())
        return false;
    Stage stage = LayoutStage();
    int button = StageButtonAt(stage, pt);
    if (button < kRailRow)
        return false;
    std::vector<RECT> rects;
    std::vector<RailRow> rows = RailShown(stage, rects);
    size_t row = static_cast<size_t>(button - kRailRow);
    if (row >= rows.size() || rows[row].kind != RailRow::kQueued)
        return false;
    path = rows[row].path;
    return true;
}

// "Play now" on a queued song: it leaves the queue and plays, as a click on its row does.
void Viewer::PlayQueuedNow(const std::string &song)
{
    std::string path;
    const std::vector<std::string> &queued = m_queue.Paths();
    size_t at = static_cast<size_t>(std::find(queued.begin(), queued.end(), song) - queued.begin());
    if (at < queued.size() && m_queue.TakeAt(at, path))
    {
        SaveQueue();
        if (!PlayQueued(path))
            StepAudio(1);
    }
    Invalidate();
}

void Viewer::RemoveFromQueue(const std::string &song)
{
    const std::vector<std::string> &queued = m_queue.Paths();
    auto at = std::find(queued.begin(), queued.end(), song);
    if (at != queued.end())
        RemoveQueued(static_cast<size_t>(at - queued.begin()));
}

// The control or the rail's row under 'pt', or -1. The headings are not rows to click, but hold "Clear" and the switch.
int Viewer::StageButtonAt(const Stage &stage, POINT pt) const
{
    int button = StripButtonAt(stage.controls, pt);
    if (button >= 0)
        return button;
    if (stage.rows <= 0 || !PtInRect(&stage.list, pt))
        return -1;
    std::vector<RECT> rects;
    std::vector<RailRow> rows = RailShown(stage, rects);
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (!PtInRect(&rects[i], pt))
            continue;
        switch (rows[i].kind)
        {
        case RailRow::kQueueHeader:
        {
            RECT clear = RailClearRect(rects[i]);
            return PtInRect(&clear, pt) ? kRailClear : -1;
        }
        case RailRow::kListHeader:
        {
            RECT toggle = RailSwitchRect(rects[i]);
            return !m_queue.Empty() && PtInRect(&toggle, pt) ? kRailSwitch : -1;
        }
        case RailRow::kEnd:
            return -1;
        default:
            return kRailRow + static_cast<int>(i);
        }
    }
    return -1;
}

// "Clear" at the right of the queue's heading.
RECT Viewer::RailClearRect(const RECT &header) const
{
    int width = Scale(kRailClearWidth);
    return { header.right - width, header.top, header.right, header.bottom };
}

// The label and switch at the right of the list's heading, which a click on either works. The heading sits at the foot
// of its row, below the room that sets it off from the queue.
RECT Viewer::RailSwitchRect(const RECT &header) const
{
    return { header.right - Scale(kSwitchHit), header.bottom - Scale(kRailQueueHead), header.right, header.bottom };
}

// The button at the right end of a song's row: the ✕ of a queued one, the + of a listed one.
RECT Viewer::RailRemoveRect(const RECT &r) const
{
    int side = Scale(kRailRemove);
    return { r.right - side, r.top + (Height(r) - side) / 2, r.right, r.top + (Height(r) - side) / 2 + side };
}

float Viewer::PlayOnLevel() const
{
    float target = m_queueStops ? 0.0f : 1.0f;
    if (m_switchSince == 0)
        return target;
    float t = std::min(1.0f, static_cast<float>(GetTickCount64() - m_switchSince) / static_cast<float>(kSwitchSlideMs));
    t = t * (2.0f - t);
    return (1.0f - target) + (target - (1.0f - target)) * t;
}

void Viewer::TogglePlayOn()
{
    m_queueStops = !m_queueStops;
    m_switchSince = GetTickCount64();
    SetTimer(m_hWnd, kViewerSwitchTimer, 16, nullptr);
    SavePlayback();
    Invalidate();
}

bool Viewer::ListSkips(const std::string &path) const
{
    return m_queuePlayed.count(path) > 0 ||
        std::find(m_queue.Paths().begin(), m_queue.Paths().end(), path) != m_queue.Paths().end();
}

// What comes after the song playing: the queue under its heading, then the heading of the list and the rest of the
// list in the order it plays in. A song that is queued (or already played from the queue) is not in the list: it plays
// once, from the queue.
std::vector<Viewer::RailRow> Viewer::UpNext(size_t skip, size_t count, size_t &total) const
{
    std::vector<RailRow> rows;
    const PlayOrder &order = SongOrder();
    const std::vector<size_t> &sequence = order.Order();
    const std::vector<std::string> &paths = SongPaths();
    size_t at = order.PositionOf(ListCurrent());
    size_t first = at == PlayOrder::kNone ? 0 : at + 1;
    size_t after = sequence.size() > first ? sequence.size() - first : 0;
    size_t queued = m_queue.Size();

    // The songs of the list that are left, only worked out when some are skipped.
    bool skipping = queued > 0 || !m_queuePlayed.empty();
    std::vector<size_t> listed;
    if (skipping)
    {
        std::unordered_set<std::string> queuedSet(m_queue.Paths().begin(), m_queue.Paths().end());
        for (size_t i = first; i < sequence.size(); i++)
        {
            const std::string &path = paths[sequence[i]];
            if (queuedSet.count(path) == 0 && m_queuePlayed.count(path) == 0)
                listed.push_back(sequence[i]);
        }
        after = listed.size();
    }

    size_t queueHead = queued > 0 ? 1 : 0;
    size_t listHead = queueHead + queued;
    total = listHead + 1 + (after > 0 ? after : 1);
    // A scroll left past the end (after Clear, say) shows the last row instead of nothing.
    if (skip >= total)
        skip = total - 1;
    for (size_t i = skip; i < total && rows.size() < count; i++)
    {
        RailRow row;
        if (i < queueHead)
            row.kind = RailRow::kQueueHeader;
        else if (i < listHead)
        {
            row.kind = RailRow::kQueued;
            row.at = i - queueHead;
            row.path = m_queue.Paths()[row.at];
        }
        else if (i == listHead)
            row.kind = RailRow::kListHeader;
        else if (after == 0)
            row.kind = RailRow::kEnd;
        else
        {
            row.kind = RailRow::kListed;
            row.at = skipping ? listed[i - listHead - 1] : sequence[first + (i - listHead - 1)];
            row.path = paths[row.at];
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Viewer::SetCovers(CoverOf coverOf, WantCovers wantCovers)
{
    m_coverOf = std::move(coverOf);
    m_wantCovers = std::move(wantCovers);
}

void Viewer::OnCoversReady()
{
    if (StageShown() && Width(LayoutStage().rail) > 0)
        Invalidate();
}

// The cover's window is fitted to the picture once libmpv says how big it is, and again if that changes.
void Viewer::RefitCover()
{
    if (m_video.Width() == m_fitWidth && m_video.Height() == m_fitHeight)
        return;
    m_fitWidth = m_video.Width();
    m_fitHeight = m_video.Height();
    if (m_background || (m_parked && !m_displaced))
        PlaceSongCover();
    else if (m_open && m_audio)
        m_video.SetBounds(CoverRect());
}

// What the rail shows of songs already played: their tags, which libmpv reads only when a song opens.
void Viewer::NoteTags()
{
    if (!m_audio && !m_parked)
        return;
    const std::vector<std::string> &paths = SongPaths();
    if (SongIndex() >= paths.size() || m_video.GetStatus() != VideoPlayer::Status::kPlaying)
        return;
    const VideoPlayer::Tags &tags = m_video.MusicTags();
    if (tags.title.empty() && tags.artist.empty())
        return;
    SongTags &known = m_songTags[paths[SongIndex()]];
    known.title = tags.title;
    known.artist = tags.artist;
}

// Takes the cover for the backdrop from libmpv: not before it has decoded one, so it is tried on a few events and ticks.
void Viewer::TakeCover()
{
    if (!StageShown() || m_paths.empty())
        return;
    const std::string &path = m_paths[m_index];
    if (path != m_coverFor)
    {
        m_coverFor = path;
        m_coverRead = false;
        m_coverTries = 0;
        std::vector<BYTE>().swap(m_coverPixels);
        m_backdrop.Reset();
    }
    if (m_coverRead || !m_video.HasCover() || m_video.Width() <= 0 || m_video.Height() <= 0 || m_coverTries >= kCoverTriesMax)
        return;
    m_coverTries++;
    UINT width = 0, height = 0;
    int sourceWidth = 0, sourceHeight = 0;
    // The picture of the file before the one playing may still be up for a moment: its size must be this one's.
    if (!m_video.ReadCover(kCoverReadSide, m_coverPixels, width, height, sourceWidth, sourceHeight) ||
        sourceWidth != m_video.Width() || sourceHeight != m_video.Height())
        return;
    m_coverWidth = width;
    m_coverHeight = height;
    m_coverRead = true;
    m_backdrop.Reset();
    Invalidate();
}

// Makes the backdrop: the cover shrunk and spread over the viewer's shape, a little more colorful, then blurred hard.
// At 96 pixels wide the blur is cheap, and the bitmap is made again only for another cover, window shape or device.
void Viewer::PrepareStage(Renderer &renderer)
{
    if (!StageShown() || m_paths.empty())
        return;
    if (m_paths[m_index] != m_coverFor)
    {
        m_coverFor = m_paths[m_index];
        m_coverRead = false;
        m_coverTries = 0;
        std::vector<BYTE>().swap(m_coverPixels);
        m_backdrop.Reset();
    }
    // The thumbnails the rail shows, asked for when the rows change.
    Stage stage = LayoutStage();
    PrepareShadow(renderer, stage.cover);
    if (stage.rows > 0 && m_wantCovers)
    {
        std::vector<RECT> rects;
        std::vector<RailRow> rows = RailShown(stage, rects);
        std::vector<std::string> wanted;
        for (size_t i = 0; i < rows.size(); i++)
        {
            if ((rows[i].kind == RailRow::kQueued || rows[i].kind == RailRow::kListed) && rects[i].top < stage.list.bottom)
                wanted.push_back(rows[i].path);
        }
        if (wanted != m_railWanted)
        {
            m_railWanted = wanted;
            m_wantCovers(wanted);
        }
    }

    if (!m_coverRead || m_coverPixels.empty() || Width(m_bounds) <= 0 || Height(m_bounds) <= 0)
        return;
    int aspect = static_cast<int>(1000LL * Width(m_bounds) / Height(m_bounds));
    if (m_backdrop && m_backdropGeneration == renderer.Generation() && m_backdropAspect == aspect)
        return;

    UINT w = kBackdropWidth;
    UINT h = std::max<UINT>(16, std::min<UINT>(kBackdropMaxHeight, static_cast<UINT>(static_cast<long long>(w) * Height(m_bounds) / Width(m_bounds))));
    std::vector<float> planes[3];
    for (std::vector<float> &plane : planes)
        plane.assign(static_cast<size_t>(w) * h, 0.0f);
    // The cover fills the backdrop, a little more than needed so the blur has no edges.
    double scale = std::max(static_cast<double>(w) / m_coverWidth, static_cast<double>(h) / m_coverHeight) * 1.15;
    for (UINT y = 0; y < h; y++)
    {
        for (UINT x = 0; x < w; x++)
        {
            double sx = std::max(0.0, std::min(m_coverWidth - 1.0, (x + 0.5 - w / 2.0) / scale + m_coverWidth / 2.0 - 0.5));
            double sy = std::max(0.0, std::min(m_coverHeight - 1.0, (y + 0.5 - h / 2.0) / scale + m_coverHeight / 2.0 - 0.5));
            UINT x0 = static_cast<UINT>(sx), y0 = static_cast<UINT>(sy);
            UINT x1 = std::min(x0 + 1, m_coverWidth - 1), y1 = std::min(y0 + 1, m_coverHeight - 1);
            double fx = sx - x0, fy = sy - y0;
            for (int c = 0; c < 3; c++)
            {
                auto at = [&](UINT px, UINT py) { return static_cast<double>(m_coverPixels[(static_cast<size_t>(py) * m_coverWidth + px) * 4 + c]); };
                planes[c][static_cast<size_t>(y) * w + x] = static_cast<float>((at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy);
            }
        }
    }
    for (std::vector<float> &plane : planes)
        BlurPlane(plane, static_cast<int>(w), static_cast<int>(h), kBlurRadius, true);
    std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4, 255);
    for (UINT y = 0; y < h; y++)
    {
        for (UINT x = 0; x < w; x++)
        {
            size_t i = static_cast<size_t>(y) * w + x;
            // A little more color: blurring and darkening wash it out. Then the corners darken, so the eye goes to
            // the middle where the cover is.
            float luma = 0.114f * planes[0][i] + 0.587f * planes[1][i] + 0.299f * planes[2][i];
            float dx = (x + 0.5f) / w * 2.0f - 1.0f, dy = (y + 0.5f) / h * 2.0f - 1.0f;
            float edge = std::min(1.0f, std::sqrt(dx * dx + dy * dy) / 1.41421f);
            float light = 1.0f - kVignette * edge * edge;
            for (int c = 0; c < 3; c++)
                pixels[i * 4 + c] = ToByte((luma + (planes[c][i] - luma) * kSaturation) * light, static_cast<int>(x), static_cast<int>(y));
        }
    }
    m_backdrop = renderer.CreateBitmap(w, h, pixels.data());
    m_backdropGeneration = renderer.Generation();
    m_backdropAspect = aspect;
}

// Makes the cover's shadow when its size changed: a black picture whose opacity is two blurs of the cover's shape,
// one tight and dark, one wide and light, both falling a little below the cover. It reaches kShadowReach past the cover, is made at a
// quarter of its size (it is all soft) and Direct2D stretches it, so this costs a few milliseconds, not per frame.
void Viewer::PrepareShadow(Renderer &renderer, const RECT &cover)
{
    int width = Width(cover), height = Height(cover);
    int reach = Scale(kShadowReach) / kShadowShrink * kShadowShrink;
    if (width <= 0 || height <= 0 || !m_video.HasCover())
    {
        m_shadow.Reset();
        m_shadowWidth = m_shadowHeight = 0;
        return;
    }
    if (m_shadow && m_shadowGeneration == renderer.Generation() && m_shadowWidth == width && m_shadowHeight == height &&
        m_shadowReach == reach)
        return;

    const int shrink = kShadowShrink;
    int gw = (width + 2 * reach) / shrink + 1;
    int gh = (height + 2 * reach) / shrink + 1;
    std::vector<float> alpha(static_cast<size_t>(gw) * gh, 1.0f);  // what is left uncovered, layer by layer
    for (const ShadowLayer &layer : kShadowLayers)
    {
        std::vector<float> plane(static_cast<size_t>(gw) * gh, 0.0f);
        int drop = static_cast<int>(std::lround(Scale(static_cast<int>(layer.drop)) / static_cast<float>(shrink)));
        int left = reach / shrink, top = reach / shrink + drop;
        int right = left + width / shrink, bottom = top + height / shrink;
        for (int y = std::max(0, top); y < std::min(gh, bottom); y++)
            for (int x = std::max(0, left); x < std::min(gw, right); x++)
                plane[static_cast<size_t>(y) * gw + x] = 1.0f;
        BlurPlane(plane, gw, gh, RadiusForSigma(Scale(static_cast<int>(layer.sigma)) / static_cast<float>(shrink)), false);
        for (size_t i = 0; i < plane.size(); i++)
            alpha[i] *= 1.0f - plane[i] * layer.alpha;
    }
    // Premultiplied black: only the alpha channel carries anything.
    std::vector<BYTE> pixels(static_cast<size_t>(gw) * gh * 4, 0);
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            pixels[(static_cast<size_t>(y) * gw + x) * 4 + 3] = ToByte((1.0f - alpha[static_cast<size_t>(y) * gw + x]) * 255.0f, x, y);
    m_shadow = renderer.CreateBitmap(static_cast<UINT>(gw), static_cast<UINT>(gh), pixels.data());
    m_shadowGeneration = renderer.Generation();
    m_shadowWidth = width;
    m_shadowHeight = height;
    m_shadowReach = reach;
}

// The cover's own colors, blurred and darkened; without a cover, a quiet violet-tinted dark.
void Viewer::PaintBackdrop(Renderer &renderer) const
{
    D2D1_RECT_F bounds = ToRectF(m_bounds);
    if (m_backdrop)
    {
        renderer.DrawImage(m_backdrop.Get(), bounds, true);
        renderer.FillGradient(bounds, Faded(theme::kStageShade, kShadeTop), Faded(theme::kStageShade, kShadeBottom));
    }
    else
        renderer.FillGradient(bounds, theme::kStageQuietTop, theme::kStageQuietBottom);
}

// "08 - THANATOS" and "02. FLY ME" go by their names alone in the rail: a leading track number is noise there.
static std::wstring RailTitle(const std::string &path)
{
    std::wstring title = SongTitle(path);
    size_t digits = 0;
    while (digits < title.size() && digits < 3 && title[digits] >= L'0' && title[digits] <= L'9')
        digits++;
    size_t after = digits;
    bool separated = false;
    while (after < title.size() && (title[after] == L' ' || title[after] == L'.' || title[after] == L'-' || title[after] == L'_' || title[after] == L')'))
    {
        if (title[after] != L' ')
            separated = true;
        after++;
    }
    // Only a real separator ("08 - ", "08. ") marks a track number: "99 Problems" stays whole.
    if (digits > 0 && separated && after < title.size())
        title.erase(0, after);
    return title;
}

// A + or ✕ drawn as lines, in a square 'box' with a plate under it when the pointer is over it.
void Viewer::PaintRailAction(Renderer &renderer, const RECT &box, bool cross) const
{
    bool hot = PtInRect(&box, m_pointer) != FALSE;
    if (hot)
        renderer.FillRoundedRect(ToRectF(box), static_cast<float>(Scale(3)), theme::kHover);
    D2D1_COLOR_F color = hot ? theme::kText1 : theme::kText2;
    float cx = (box.left + box.right) / 2.0f, cy = (box.top + box.bottom) / 2.0f;
    float width = Scale(3) / 2.0f;
    if (cross)
    {
        float r = Scale(9) / 2.0f;
        renderer.DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), color, width);
        renderer.DrawLine(D2D1::Point2F(cx - r, cy + r), D2D1::Point2F(cx + r, cy - r), color, width);
    }
    else
    {
        float r = Scale(11) / 2.0f;
        renderer.DrawLine(D2D1::Point2F(cx - r, cy), D2D1::Point2F(cx + r, cy), color, width);
        renderer.DrawLine(D2D1::Point2F(cx, cy - r), D2D1::Point2F(cx, cy + r), color, width);
    }
}

// The "Play on" switch: a small track with a knob that slides to the right when on. It is the one pill in the app, by
// choice.
void Viewer::PaintPlayOn(Renderer &renderer, const RECT &header) const
{
    RECT hit = RailSwitchRect(header);
    float hover = m_hover.Level(kRailSwitch);
    float level = PlayOnLevel();
    float width = static_cast<float>(Scale(kSwitchWidth)), height = static_cast<float>(Scale(kSwitchHeight));
    float middle = (hit.top + hit.bottom) / 2.0f;
    D2D1_RECT_F track = D2D1::RectF(hit.right - width, middle - height / 2, static_cast<float>(hit.right), middle + height / 2);
    D2D1_COLOR_F off = theme::kSliderTrack;
    D2D1_COLOR_F on = theme::kYuri;
    D2D1_COLOR_F fill = D2D1::ColorF(off.r + (on.r - off.r) * level, off.g + (on.g - off.g) * level,
        off.b + (on.b - off.b) * level, off.a + (on.a - off.a) * level);
    renderer.FillRoundedRect(track, height / 2, fill);
    float knob = height - 2.0f * Scale(2);
    float travel = width - height;
    float x = track.left + Scale(2) + knob / 2 + travel * level;
    renderer.FillCircle(D2D1::Point2F(x, middle), knob / 2, theme::kText1);
    RECT label = { hit.left, hit.top, static_cast<LONG>(track.left) - Scale(8), hit.bottom };
    renderer.DrawTextLine(strings::kPlayOn, Font::kCaption, label, hover > 0.5f || level > 0.5f ? theme::kText1 : theme::kText2,
        DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// The songs coming up, in two sections: the queue (with "Clear") and the list that follows it (with the "Play on"
// switch). Each song has its cover, title and artist, a ▶ over the cover and a + (or ✕, in the queue) under the pointer.
void Viewer::PaintRail(Renderer &renderer, const Stage &stage) const
{
    renderer.FillRect(ToRectF(stage.rail), theme::kRailFill);
    renderer.FillRect(D2D1::RectF(static_cast<float>(stage.rail.left), static_cast<float>(stage.rail.top),
        stage.rail.left + 1.0f, static_cast<float>(stage.rail.bottom)), theme::kStroke);

    std::vector<RECT> rects;
    std::vector<RailRow> rows = RailShown(stage, rects);
    bool queued = !m_queue.Empty();
    renderer.PushClip(stage.list);
    for (size_t i = 0; i < rows.size(); i++)
    {
        RECT row = rects[i];
        if (row.top >= stage.list.bottom)
            break;
        const RailRow &entry = rows[i];
        if (entry.kind == RailRow::kQueueHeader)
        {
            std::wstring heading = std::wstring(strings::kQueue) + L" · " + std::to_wstring(m_queue.Size());
            renderer.DrawTextLine(heading, Font::kBodyStrong, row, theme::kText1);
            float hover = m_hover.Level(kRailClear);
            RECT clear = RailClearRect(row);
            if (hover > 0.0f)
            {
                D2D1_RECT_F plate = D2D1::RectF(static_cast<float>(clear.left), static_cast<float>(clear.top + Scale(6)),
                    static_cast<float>(clear.right + Scale(8)), static_cast<float>(clear.bottom - Scale(6)));
                renderer.FillRoundedRect(plate, static_cast<float>(Scale(3)), Faded(theme::kHover, hover));
            }
            renderer.DrawTextLine(strings::kQueueClear, Font::kCaption, clear, hover > 0.5f ? theme::kText1 : theme::kText2,
                DWRITE_TEXT_ALIGNMENT_TRAILING);
            continue;
        }
        if (entry.kind == RailRow::kListHeader)
        {
            std::wstring order = m_shuffle ? strings::kOrderShuffled : strings::kOrderGrid;
            if (queued)
            {
                RECT line = { row.left, row.bottom - Scale(kRailQueueHead), row.right, row.bottom };
                RECT text = line;
                text.right = RailSwitchRect(row).right - Scale(kSwitchHit) + Scale(24);
                renderer.DrawTextLine(std::wstring(strings::kThenFrom) + order, Font::kCaption, text, theme::kText3);
                PaintPlayOn(renderer, row);
            }
            else
            {
                RECT title = { row.left, row.top, row.right, row.top + Scale(24) };
                RECT caption = { row.left, title.bottom, row.right, row.bottom };
                renderer.DrawTextLine(strings::kUpNext, Font::kBodyStrong, title, theme::kText1);
                renderer.DrawTextLine(m_shuffle ? strings::kUpNextShuffled : strings::kUpNextGrid, Font::kCaption, caption, theme::kText3);
            }
            continue;
        }
        if (entry.kind == RailRow::kEnd)
        {
            renderer.DrawTextLine(strings::kUpNextEnd, Font::kCaption, row, theme::kText3);
            continue;
        }

        float hover = m_hover.Level(kRailRow + static_cast<int>(i));
        bool dragged = m_railDragging && m_railPress == static_cast<int>(i);
        if (hover > 0.0f || dragged)
        {
            D2D1_RECT_F plate = ToRectF(row);
            plate.left -= Scale(8);
            plate.right += Scale(8);
            renderer.FillRoundedRect(plate, static_cast<float>(Scale(theme::kChipRadius)), Faded(theme::kHover, dragged ? 1.0f : hover));
        }
        if (m_railDragging && m_railDrop == static_cast<int>(i) && !dragged)
        {
            float y = static_cast<float>(m_railDrop > m_railPress ? row.bottom : row.top);
            renderer.FillRect(D2D1::RectF(static_cast<float>(row.left), y - 1.0f, static_cast<float>(row.right), y + 1.0f), theme::kYuri);
        }

        const std::string &path = entry.path;
        int thumb = Scale(kRailThumb);
        RECT box = { row.left, row.top + (Height(row) - thumb) / 2, row.left + thumb, row.top + (Height(row) - thumb) / 2 + thumb };
        ID2D1Bitmap *bitmap = m_coverOf ? m_coverOf(*m_renderer, path) : nullptr;
        RECT shown = box;
        if (bitmap)
        {
            D2D1_SIZE_U size = bitmap->GetPixelSize();
            shown = Fitted(box, static_cast<int>(size.width), static_cast<int>(size.height));
            renderer.DrawBitmap(bitmap, ToRectF(shown), static_cast<float>(Scale(3)), 1.0f);
        }
        else
            PaintMusicTile(renderer, box, L"");
        // A light wash and a ▶ over the cover say that a click plays this song; the cover shows round the triangle.
        if (hover > 0.0f && !m_railDragging)
        {
            renderer.FillRoundedRect(ToRectF(shown), static_cast<float>(Scale(3)), D2D1::ColorF(0, 0.18f * hover));
            float cx = (shown.left + shown.right) / 2.0f + Scale(1), cy = (shown.top + shown.bottom) / 2.0f;
            float h = std::min(static_cast<float>(Scale(7)), Width(shown) / 4.0f), w = h * 1.7f;
            const D2D1_POINT_2F shadow[] = { { cx - w / 2, cy - h + 1.0f }, { cx + w / 2, cy + 1.0f }, { cx - w / 2, cy + h + 1.0f } };
            const D2D1_POINT_2F tri[] = { { cx - w / 2, cy - h }, { cx + w / 2, cy }, { cx - w / 2, cy + h } };
            renderer.FillPolygon(shadow, 3, D2D1::ColorF(0, 0.35f * hover));
            renderer.FillPolygon(tri, 3, Faded(theme::kText1, hover));
        }

        auto known = m_songTags.find(path);
        std::wstring title = known != m_songTags.end() && !known->second.title.empty() ? known->second.title : RailTitle(path);
        std::wstring artist = known != m_songTags.end() ? known->second.artist : std::wstring();
        int textLeft = box.right + Scale(12);
        int textRight = row.right - Scale(kRailRemove) - Scale(4);
        bool queuedRow = entry.kind == RailRow::kQueued;
        int line = static_cast<int>(std::ceil(renderer.LineHeight(Font::kBody)));
        int small = static_cast<int>(std::ceil(renderer.LineHeight(Font::kCaption)));
        int textTop = row.top + (Height(row) - line - (artist.empty() ? 0 : small)) / 2;
        RECT name = { textLeft, textTop, textRight, textTop + line };
        bool dim = m_queueStops && !queuedRow;
        renderer.DrawTextLine(title, Font::kBody, name, dim ? theme::kText3 : theme::kText1);
        if (!artist.empty())
        {
            RECT by = { textLeft, name.bottom, textRight, name.bottom + small };
            renderer.DrawTextLine(artist, Font::kCaption, by, theme::kText3);
        }
        if (hover > 0.0f && !m_railDragging)
            PaintRailAction(renderer, RailRemoveRect(row), queuedRow);
    }
    renderer.PopClip();
}

// The backdrop, the cover (a soft shadow under its window, or the music tile where there is no cover), the rail, and
// the title, tags, format and controls.
void Viewer::PaintAudio(Renderer &renderer) const
{
    if (!StageShown())
    {
        PaintMessage(renderer);
        return;
    }
    // The cover fills the viewer: libmpv's window shows it on the plain backdrop Paint laid.
    if (CoverFull())
        return;
    Stage stage = LayoutStage();
    PaintBackdrop(renderer);
    if (Width(stage.rail) > 0)
        PaintRail(renderer, stage);

    if (Width(stage.slot) > 0)
    {
        if (m_video.HasCover())
        {
            // The shadow's bitmap covers the cover plus its reach all around; the cover is drawn over it.
            if (m_shadow)
            {
                float reach = static_cast<float>(Scale(kShadowReach) / kShadowShrink * kShadowShrink);
                D2D1_RECT_F area = ToRectF(stage.cover);
                // The bitmap is a whole number of shrunken pixels, so it is a little larger than cover + 2 * reach.
                D2D1_SIZE_F size = m_shadow->GetSize();
                float extraX = size.width * kShadowShrink - (Width(stage.cover) + 2 * reach);
                float extraY = size.height * kShadowShrink - (Height(stage.cover) + 2 * reach);
                area.left -= reach;
                area.top -= reach;
                area.right += reach + extraX;
                area.bottom += reach + extraY;
                renderer.DrawImage(m_shadow.Get(), area, true);
            }
        }
        else
        {
            std::wstring extension = Utf8ToWide(FileExtension(m_paths[m_index]));
            std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
            PaintMusicTile(renderer, stage.slot, extension);
        }
    }

    // The tags if the file has them; else the file's name alone.
    const VideoPlayer::Tags &tags = m_video.MusicTags();
    std::wstring title = tags.title.empty() ? SongTitle(m_paths[m_index]) : tags.title;
    std::wstring by = tags.artist;
    if (!tags.album.empty())
        by += (by.empty() ? L"" : L" \u00B7 ") + tags.album;
    renderer.DrawTextLine(title, Font::kTitle, stage.title, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (!by.empty())
        renderer.DrawTextLine(by, Font::kBody, stage.subtitle, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
    renderer.DrawTextLine(StripDetail(), Font::kMono, stage.format, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);

    const Strip &c = stage.controls;
    for (int button : { kStripShuffle, kStripPrevious, kStripPlay, kStripNext, kStripRepeat, kStripMute })
        PaintStripButton(renderer, c, button);
    bool hours = m_video.Duration() >= 3600.0;
    renderer.DrawTextLine(FormatTime(m_video.Position(), hours), Font::kMono, c.elapsed, theme::kText3, DWRITE_TEXT_ALIGNMENT_TRAILING);
    renderer.DrawTextLine(FormatTime(m_video.Duration(), hours), Font::kMono, c.duration, theme::kText3);
    double played = m_video.Duration() > 0.0 ? m_video.Position() / m_video.Duration() : 0.0;
    float seekHover = m_stripSeeking ? 1.0f : m_hover.Level(kStripSeek);
    PaintStripSlider(renderer, c.seek, played, seekHover);
    float volumeHover = m_stripVolume ? 1.0f : m_hover.Level(kStripVolume);
    PaintStripSlider(renderer, c.volume, m_video.Muted() ? 0.0 : m_video.Volume() / 100.0, volumeHover);
    PaintSeekTip(renderer, c.seek, m_bounds, hours, seekHover);
}

// A click on the stage: a control, a row of the rail (plays that song), or elsewhere on the stage (plays or pauses).
// A queued row waits for the button to go up: it plays then, or moves where it was dragged to (OnRailRelease).
Viewer::Action Viewer::OnStageClick(POINT pt, bool doubleClick)
{
    m_pointer = pt;
    // Any click on the full cover puts the stage back.
    if (CoverFull())
    {
        SetCoverFull(false);
        return Action::kNone;
    }
    RECT expand = ExpandRect();
    if (ExpandShown() && PtInRect(&expand, pt))
    {
        SetCoverFull(true);
        return Action::kNone;
    }
    Stage stage = LayoutStage();
    int button = StageButtonAt(stage, pt);
    if (button == kRailClear)
    {
        m_queue.Clear();
        SaveQueue();
        Invalidate();
        return Action::kNone;
    }
    if (button == kRailSwitch)
    {
        TogglePlayOn();
        return Action::kNone;
    }
    if (button >= kRailRow)
    {
        std::vector<RECT> rects;
        std::vector<RailRow> rows = RailShown(stage, rects);
        size_t row = static_cast<size_t>(button - kRailRow);
        if (row >= rows.size())
            return Action::kNone;
        RECT action = RailRemoveRect(rects[row]);
        if (rows[row].kind == RailRow::kListed)
        {
            // The + adds the song to the end of the queue; the rest of the row plays it.
            if (PtInRect(&action, pt))
                QueueSongs({ rows[row].path }, false);
            else
            {
                m_queuePlayed.clear();
                PlayAudioAt(rows[row].at);
            }
        }
        else if (rows[row].kind == RailRow::kQueued)
        {
            if (PtInRect(&action, pt))
                RemoveQueued(rows[row].at);
            else
            {
                m_railPress = static_cast<int>(row);
                m_railDrop = m_railPress;
                m_railPressAt = pt;
                m_railDragging = false;
                SetCapture(m_hWnd);
            }
        }
        return Action::kNone;
    }
    if (PlayerControl(stage.controls, button, pt))
        return Action::kNone;
    if (Width(stage.rail) > 0 && PtInRect(&stage.rail, pt))
        return Action::kNone;
    // The cover plays and pauses; the text and the room round it do nothing. The second press of a double-click does
    // not: it would pause and play again, and the music stutters.
    if (doubleClick || !PtInRect(&stage.cover, pt))
        return Action::kNone;
    m_video.TogglePause();
    Invalidate();
    return Result(Action::kNone);
}

// The pointer moved with a queued row pressed: past a few pixels it is a drag, dropping on the queued row it is over
// (the first or last one, when it is above or below them).
void Viewer::OnRailDrag(POINT pt)
{
    if (!m_railDragging && std::abs(pt.y - m_railPressAt.y) < Scale(4))
        return;
    m_railDragging = true;
    Stage stage = LayoutStage();
    std::vector<RECT> rects;
    std::vector<RailRow> rows = RailShown(stage, rects);
    int first = -1, last = -1, over = -1;
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (rows[i].kind != RailRow::kQueued)
            continue;
        if (first < 0)
            first = static_cast<int>(i);
        last = static_cast<int>(i);
        if (pt.y >= rects[i].top && pt.y < rects[i].bottom)
            over = static_cast<int>(i);
    }
    if (first >= 0)
        m_railDrop = over >= 0 ? over : pt.y < rects[static_cast<size_t>(first)].top ? first : last;
    Invalidate();
}

// The button went up on a pressed queued row: a plain click plays that song, a drag moves it.
void Viewer::OnRailRelease()
{
    int press = m_railPress, drop = m_railDrop;
    bool dragged = m_railDragging;
    m_railPress = m_railDrop = -1;
    m_railDragging = false;
    if (press < 0)
        return;
    Stage stage = LayoutStage();
    std::vector<RECT> rects;
    std::vector<RailRow> rows = RailShown(stage, rects);
    if (static_cast<size_t>(press) >= rows.size() || rows[static_cast<size_t>(press)].kind != RailRow::kQueued)
        return;
    size_t at = rows[static_cast<size_t>(press)].at;
    if (dragged)
    {
        if (drop >= 0 && static_cast<size_t>(drop) < rows.size() && rows[static_cast<size_t>(drop)].kind == RailRow::kQueued)
        {
            m_queue.Move(at, rows[static_cast<size_t>(drop)].at);
            SaveQueue();
        }
    }
    else
    {
        std::string path;
        if (m_queue.TakeAt(at, path))
        {
            SaveQueue();
            if (!PlayQueued(path))
                StepAudio(1);
        }
    }
    Invalidate();
}
