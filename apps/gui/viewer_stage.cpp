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
    const int kBlockMin = 460;
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
    // The backdrop is made at this width, which Direct2D stretches over the viewer: blur costs nothing at that size.
    const UINT kBackdropWidth = 96;
    const UINT kBackdropMaxHeight = 160;
    const UINT kCoverReadSide = 128;
    const int kBlurRadius = 5;
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

    // Blurs a BGRA picture with three passes of a box blur, edges extended.
    void Blur(std::vector<BYTE> &pixels, UINT width, UINT height, int radius)
    {
        std::vector<BYTE> temp(pixels.size());
        auto pass = [&](const std::vector<BYTE> &from, std::vector<BYTE> &to, bool horizontal)
        {
            int length = static_cast<int>(horizontal ? width : height);
            int lines = static_cast<int>(horizontal ? height : width);
            size_t step = horizontal ? 4 : static_cast<size_t>(width) * 4;
            size_t lineStep = horizontal ? static_cast<size_t>(width) * 4 : 4;
            for (int line = 0; line < lines; line++)
            {
                size_t base = static_cast<size_t>(line) * lineStep;
                for (int channel = 0; channel < 3; channel++)
                {
                    auto at = [&](int i) { return static_cast<int>(from[base + static_cast<size_t>(std::max(0, std::min(length - 1, i))) * step + channel]); };
                    int sum = 0;
                    for (int i = -radius; i <= radius; i++)
                        sum += at(i);
                    for (int i = 0; i < length; i++)
                    {
                        to[base + static_cast<size_t>(i) * step + channel] = static_cast<BYTE>(sum / (2 * radius + 1));
                        sum += at(i + radius + 1) - at(i - radius);
                    }
                }
            }
        };
        for (int i = 0; i < 3; i++)
        {
            pass(pixels, temp, true);
            pass(temp, pixels, false);
        }
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

    int titleHeight = static_cast<int>(std::ceil(renderer.LineHeight(Font::kTitle)));
    int subtitleHeight = static_cast<int>(std::ceil(renderer.LineHeight(Font::kBody)));
    int formatHeight = static_cast<int>(std::ceil(renderer.LineHeight(Font::kMono)));
    int seekRow = Scale(kSeekRow);
    int controlsRow = Scale(kControlsRow);
    int blockHeight = titleHeight + Scale(kLineGap) + subtitleHeight + Scale(kFormatGap) + formatHeight + Scale(kSeekGap) +
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
    y = s.title.bottom + Scale(kLineGap);
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
    if (volumeLeft - Scale(4) - small >= x)
    {
        c.volume = { volumeLeft, middle - sliderHeight / 2, right, middle - sliderHeight / 2 + sliderHeight };
        c.mute = { volumeLeft - Scale(4) - small, middle - small / 2, volumeLeft - Scale(4), middle - small / 2 + small };
    }

    if (railWidth > 0)
    {
        int pad = Scale(kRailPad);
        int headTop = m_bounds.top + Scale(kRailTop);
        s.head = { s.rail.left + pad, headTop, s.rail.right - pad, headTop + static_cast<int>(std::ceil(renderer.LineHeight(Font::kBodyStrong))) };
        s.caption = { s.head.left, s.head.bottom + Scale(2), s.head.right,
            s.head.bottom + Scale(2) + static_cast<int>(std::ceil(renderer.LineHeight(Font::kCaption))) };
        s.list = { s.head.left, s.caption.bottom + Scale(12), s.head.right, s.rail.bottom - Scale(8) };
        s.rows = std::max(0, Height(s.list) / Scale(kRailRowHeight));
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

RECT Viewer::RailRowRect(const Stage &stage, int row) const
{
    int height = Scale(kRailRowHeight);
    return { stage.list.left, stage.list.top + row * height, stage.list.right, stage.list.top + (row + 1) * height };
}

// The control or the rail's row under 'pt', or -1.
int Viewer::StageButtonAt(const Stage &stage, POINT pt) const
{
    int button = StripButtonAt(stage.controls, pt);
    if (button >= 0)
        return button;
    if (stage.rows > 0 && !m_queue.Empty())
    {
        RECT clear = RailClearRect(stage);
        if (PtInRect(&clear, pt))
            return kRailClear;
    }
    if (stage.rows > 0 && PtInRect(&stage.list, pt))
    {
        int row = (pt.y - stage.list.top) / Scale(kRailRowHeight);
        size_t total = 0;
        std::vector<RailRow> rows = UpNext(static_cast<size_t>(m_railScroll), static_cast<size_t>(stage.rows), total);
        if (row >= 0 && static_cast<size_t>(row) < rows.size())
            return kRailRow + row;
    }
    return -1;
}

// "Clear" at the right of the rail's heading, with a queue.
RECT Viewer::RailClearRect(const Stage &stage) const
{
    int width = Scale(kRailClearWidth);
    return { stage.head.right - width, stage.head.top, stage.head.right, stage.head.bottom };
}

// The ✕ at the right end of a queued row.
RECT Viewer::RailRemoveRect(const Stage &stage, int row) const
{
    RECT r = RailRowRect(stage, row);
    int side = Scale(kRailRemove);
    return { r.right - side, r.top + (Height(r) - side) / 2, r.right, r.top + (Height(r) - side) / 2 + side };
}

// What comes after the song playing. The queue goes first, then a divider line (which turns "play on from the list"
// on and off), then the rest of the list in the order it plays in.
std::vector<Viewer::RailRow> Viewer::UpNext(size_t skip, size_t count, size_t &total) const
{
    std::vector<RailRow> rows;
    const PlayOrder &order = SongOrder();
    const std::vector<size_t> &sequence = order.Order();
    const std::vector<std::string> &paths = SongPaths();
    size_t at = order.PositionOf(ListCurrent());
    size_t first = at == PlayOrder::kNone ? 0 : at + 1;
    size_t listCount = sequence.size() > first ? sequence.size() - first : 0;
    size_t queued = m_queue.Size();
    size_t head = queued > 0 ? queued + 1 : 0;
    total = head + listCount;
    for (size_t i = skip; i < total && rows.size() < count; i++)
    {
        RailRow row;
        if (i < queued)
        {
            row.kind = RailRow::kQueued;
            row.at = i;
            row.path = m_queue.Paths()[i];
        }
        else if (i < head)
            row.kind = RailRow::kDivider;
        else
        {
            row.kind = RailRow::kListed;
            row.at = sequence[first + (i - head)];
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
    if (stage.rows > 0 && m_wantCovers)
    {
        size_t total = 0;
        std::vector<RailRow> rows = UpNext(static_cast<size_t>(m_railScroll), static_cast<size_t>(stage.rows), total);
        std::vector<std::string> wanted;
        for (const RailRow &row : rows)
        {
            if (row.kind != RailRow::kDivider)
                wanted.push_back(row.path);
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
    std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4, 255);
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
            float color[3];
            for (int c = 0; c < 3; c++)
            {
                auto at = [&](UINT px, UINT py) { return static_cast<double>(m_coverPixels[(static_cast<size_t>(py) * m_coverWidth + px) * 4 + c]); };
                color[c] = static_cast<float>((at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy);
            }
            // A little more color: blurring and darkening wash it out.
            float luma = 0.114f * color[0] + 0.587f * color[1] + 0.299f * color[2];
            for (int c = 0; c < 3; c++)
                pixels[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<BYTE>(std::max(0.0f, std::min(255.0f, luma + (color[c] - luma) * 1.2f)));
        }
    }
    Blur(pixels, w, h, kBlurRadius);
    m_backdrop = renderer.CreateBitmap(w, h, pixels.data());
    m_backdropGeneration = renderer.Generation();
    m_backdropAspect = aspect;
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

// The songs coming up: a heading, what order they are in, and a row each with its cover, title and artist. Queued
// songs come first, with a ✕ under the pointer; a line then says what happens after them.
void Viewer::PaintRail(Renderer &renderer, const Stage &stage) const
{
    renderer.FillRect(ToRectF(stage.rail), theme::kRailFill);
    renderer.FillRect(D2D1::RectF(static_cast<float>(stage.rail.left), static_cast<float>(stage.rail.top),
        stage.rail.left + 1.0f, static_cast<float>(stage.rail.bottom)), theme::kStroke);
    bool queued = !m_queue.Empty();
    renderer.DrawTextLine(strings::kUpNext, Font::kBodyStrong, stage.head, theme::kText1);
    if (queued)
    {
        float hover = m_hover.Level(kRailClear);
        RECT clear = RailClearRect(stage);
        renderer.DrawTextLine(strings::kQueueClear, Font::kCaption, clear, hover > 0.5f ? theme::kText1 : theme::kText2,
            DWRITE_TEXT_ALIGNMENT_TRAILING);
        std::wstring count = std::to_wstring(m_queue.Size()) + (m_queue.Size() == 1 ? strings::kQueueOneSong : strings::kQueueSongs);
        renderer.DrawTextLine(count, Font::kCaption, stage.caption, theme::kText3);
    }
    else
        renderer.DrawTextLine(m_shuffle ? strings::kUpNextShuffled : strings::kUpNextGrid, Font::kCaption, stage.caption, theme::kText3);

    size_t total = 0;
    std::vector<RailRow> rows = UpNext(static_cast<size_t>(m_railScroll), static_cast<size_t>(stage.rows), total);
    if (rows.empty())
    {
        RECT row = RailRowRect(stage, 0);
        renderer.DrawTextLine(strings::kUpNextEnd, Font::kCaption, row, theme::kText3);
        return;
    }
    renderer.PushClip(stage.list);
    for (size_t i = 0; i < rows.size(); i++)
    {
        RECT row = RailRowRect(stage, static_cast<int>(i));
        const RailRow &entry = rows[i];
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

        if (entry.kind == RailRow::kDivider)
        {
            renderer.DrawTextLine(m_queueStops ? strings::kQueueStops : strings::kQueueThenList, Font::kCaption, row,
                hover > 0.5f ? theme::kText2 : theme::kText3);
            continue;
        }

        const std::string &path = entry.path;
        int thumb = Scale(kRailThumb);
        RECT box = { row.left, row.top + (Height(row) - thumb) / 2, row.left + thumb, row.top + (Height(row) - thumb) / 2 + thumb };
        ID2D1Bitmap *bitmap = m_coverOf ? m_coverOf(*m_renderer, path) : nullptr;
        if (bitmap)
        {
            D2D1_SIZE_U size = bitmap->GetPixelSize();
            RECT fitted = Fitted(box, static_cast<int>(size.width), static_cast<int>(size.height));
            renderer.DrawBitmap(bitmap, ToRectF(fitted), static_cast<float>(Scale(3)), 1.0f);
        }
        else
            PaintMusicTile(renderer, box, L"");

        auto known = m_songTags.find(path);
        std::wstring title = known != m_songTags.end() && !known->second.title.empty() ? known->second.title : SongTitle(path);
        std::wstring artist = known != m_songTags.end() ? known->second.artist : std::wstring();
        int textLeft = box.right + Scale(12);
        int textRight = row.right;
        bool queuedRow = entry.kind == RailRow::kQueued;
        if (queuedRow)
            textRight -= Scale(kRailRemove) + Scale(4);
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
        if (queuedRow && hover > 0.0f)
        {
            RECT x = RailRemoveRect(stage, static_cast<int>(i));
            renderer.DrawTextLine(L"\u2715", Font::kBody, x, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
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
            // Layers of black widening and fading out, a little lower than the cover.
            float offset = static_cast<float>(Scale(18));
            for (int i = 0; i < 12; i++)
            {
                float grow = static_cast<float>(Scale(3) * (12 - i));
                D2D1_RECT_F layer = ToRectF(stage.cover);
                layer.left -= grow;
                layer.right += grow;
                layer.top += offset - grow;
                layer.bottom += offset + grow;
                renderer.FillRoundedRect(layer, static_cast<float>(Scale(3)) + grow, D2D1::ColorF(0, 0.05f));
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
Viewer::Action Viewer::OnStageClick(POINT pt)
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
    if (button >= kRailRow)
    {
        size_t total = 0;
        std::vector<RailRow> rows = UpNext(static_cast<size_t>(m_railScroll), static_cast<size_t>(stage.rows), total);
        size_t row = static_cast<size_t>(button - kRailRow);
        if (row >= rows.size())
            return Action::kNone;
        if (rows[row].kind == RailRow::kDivider)
        {
            m_queueStops = !m_queueStops;
            SavePlayback();
            Invalidate();
        }
        else if (rows[row].kind == RailRow::kListed)
            PlayAudioAt(rows[row].at);
        else
        {
            RECT x = RailRemoveRect(stage, static_cast<int>(row));
            if (PtInRect(&x, pt))
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
    // The text and the room round the cover play and pause, as everywhere in the viewer.
    m_video.TogglePause();
    Invalidate();
    return Result(Action::kNone);
}

// The pointer moved with a queued row pressed: past a few pixels it is a drag, dropping where the pointer is.
void Viewer::OnRailDrag(POINT pt)
{
    if (!m_railDragging && std::abs(pt.y - m_railPressAt.y) < Scale(4))
        return;
    m_railDragging = true;
    Stage stage = LayoutStage();
    int row = (pt.y - stage.list.top) / Scale(kRailRowHeight);
    int last = static_cast<int>(std::min<size_t>(m_queue.Size(), static_cast<size_t>(stage.rows))) - 1;
    m_railDrop = std::max(0, std::min(row, std::min(last, static_cast<int>(m_queue.Size()) - 1 - m_railScroll)));
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
    size_t at = static_cast<size_t>(press + m_railScroll);
    if (dragged)
    {
        m_queue.Move(at, static_cast<size_t>(std::max(0, drop + m_railScroll)));
        SaveQueue();
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
