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
    // Inside the strip, at its ends and between its parts; its buttons, and the most its name takes.
    const int kStripPadding = 12;
    const int kStripGap = 10;
    const int kStripButton = 24;
    const int kStripNameWidth = 260;

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

    // A play triangle, or the two bars of pause.
    void PaintPlay(Renderer &renderer, const RECT &rect, bool paused, float s, const D2D1_COLOR_F &color)
    {
        D2D1_POINT_2F c = D2D1::Point2F((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
        if (paused)
        {
            const D2D1_POINT_2F triangle[] = { { c.x - s * 0.7f, c.y - s }, { c.x + s, c.y }, { c.x - s * 0.7f, c.y + s } };
            renderer.FillPolygon(triangle, 3, color);
            return;
        }
        float bar = s * 0.6f;
        renderer.FillRect(D2D1::RectF(c.x - s * 0.7f, c.y - s, c.x - s * 0.7f + bar, c.y + s), color);
        renderer.FillRect(D2D1::RectF(c.x + s * 0.7f - bar, c.y - s, c.x + s * 0.7f, c.y + s), color);
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
        float radius = static_cast<float>(Scale(3));
        renderer.FillRoundedRect(ToRectF(cover), radius, theme::kControl);
        float s = Width(cover) * 0.3f;
        float cx = (cover.left + cover.right) / 2.0f;
        float cy = cover.top + Width(cover) * 0.42f;
        auto P = [&](float x, float y) { return D2D1::Point2F(cx + x * s, cy + y * s); };
        float line = 1.5f * Scale(4) / 4.0f;
        renderer.DrawLine(P(-0.18f, 0.24f), P(-0.18f, -0.34f), theme::kText3, line);
        renderer.DrawLine(P(0.3f, 0.16f), P(0.3f, -0.42f), theme::kText3, line);
        const D2D1_POINT_2F beam[] = { P(-0.18f, -0.34f), P(0.3f, -0.42f), P(0.3f, -0.3f), P(-0.18f, -0.22f) };
        renderer.FillPolygon(beam, 4, theme::kText3);
        renderer.FillCircle(P(-0.29f, 0.26f), 0.12f * s, theme::kText3);
        renderer.FillCircle(P(0.19f, 0.18f), 0.12f * s, theme::kText3);

        std::wstring extension = Utf8ToWide(FileExtension(m_paths[m_index]));
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
        float pad = static_cast<float>(Scale(8));
        float width = std::ceil(renderer.TextWidth(extension, Font::kMono)) + 2 * pad;
        float height = static_cast<float>(Scale(20));
        float top = cy + s * 0.7f;
        D2D1_RECT_F badge = D2D1::RectF(std::floor(cx - width / 2), top, std::floor(cx - width / 2) + width, top + height);
        renderer.FillRoundedRect(badge, static_cast<float>(Scale(theme::kChipRadius)), theme::kChip);
        RECT text = { static_cast<LONG>(badge.left), static_cast<LONG>(badge.top), static_cast<LONG>(badge.right),
            static_cast<LONG>(badge.bottom) };
        renderer.DrawTextLine(extension, Font::kMono, text, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
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

// Only the next music: videos, pictures and books in the list are passed over, and the last one stays at its end.
void Viewer::PlayNextAudio()
{
    for (size_t i = m_index + 1; i < m_paths.size(); i++)
    {
        if (!IsPlayableAudio(m_paths[i]))
            continue;
        if (m_open)
        {
            m_first = false;
            Show(i);
            ShowOverlays();
        }
        else
        {
            m_index = i;
            m_video.Open(m_hWnd, CoverRect(), m_paths[i]);
            InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        }
        return;
    }
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

// Play at the left, then the name and the time, and the x at the right end.
Viewer::Strip Viewer::LayoutStrip() const
{
    Strip strip;
    if (!m_renderer || m_paths.empty())
        return strip;
    const RECT &b = m_stripBounds;
    int button = Scale(kStripButton);
    int top = (b.top + b.bottom - button) / 2;
    int pad = Scale(kStripPadding);
    int gap = Scale(kStripGap);
    strip.play = { b.left + pad, top, b.left + pad + button, top + button };
    strip.close = { b.right - pad - button, top, b.right - pad, top + button };
    bool hours = m_video.Duration() >= 3600.0;
    std::wstring longest = hours ? L"0:00:00 / 0:00:00" : L"00:00 / 00:00";
    int timeWidth = static_cast<int>(std::ceil(m_renderer->TextWidth(longest, Font::kMono)));
    int nameLeft = strip.play.right + gap;
    int nameRight = std::min<int>(nameLeft + Scale(kStripNameWidth), strip.close.left - gap - timeWidth - gap);
    // A narrow strip drops the time first.
    if (nameRight - nameLeft < Scale(80))
    {
        nameRight = strip.close.left - gap;
        timeWidth = 0;
    }
    int nameWidth = static_cast<int>(std::ceil(m_renderer->TextWidth(Title(m_paths[m_index]), Font::kCaption)));
    nameRight = std::max(nameLeft, std::min(nameRight, nameLeft + nameWidth));
    strip.name = { nameLeft, b.top, nameRight, b.bottom };
    if (timeWidth > 0)
        strip.time = { nameRight + gap, b.top, nameRight + gap + timeWidth, b.bottom };
    return strip;
}

// On the base color like the rest of Corpus's bars, with how far it played as a line along its top edge.
void Viewer::PaintStrip(Renderer &renderer) const
{
    if (!m_background || Width(m_stripBounds) <= 0)
        return;
    Strip strip = LayoutStrip();
    D2D1_RECT_F bounds = ToRectF(m_stripBounds);
    renderer.FillRect(bounds, theme::kBase);
    renderer.FillRect(D2D1::RectF(bounds.left, bounds.top, bounds.right, bounds.top + 1.0f), theme::kStroke);
    double played = m_video.Duration() > 0.0 ? std::min(1.0, m_video.Position() / m_video.Duration()) : 0.0;
    if (played > 0.0)
        renderer.FillRect(D2D1::RectF(bounds.left, bounds.top, bounds.left + static_cast<float>((bounds.right - bounds.left) * played),
            bounds.top + Scale(2)), theme::kYuri);

    PaintPlay(renderer, strip.play, m_video.Paused() || m_video.Ended() || !Playing(), static_cast<float>(Scale(5)), theme::kText1);
    renderer.DrawTextLine(Title(m_paths[m_index]), Font::kCaption, strip.name, theme::kText1);
    if (Width(strip.time) > 0)
    {
        bool hours = m_video.Duration() >= 3600.0;
        renderer.DrawTextLine(FormatTime(m_video.Position(), hours) + L" / " + FormatTime(m_video.Duration(), hours), Font::kMono,
            strip.time, theme::kText3);
    }
    D2D1_POINT_2F c = D2D1::Point2F((strip.close.left + strip.close.right) / 2.0f, (strip.close.top + strip.close.bottom) / 2.0f);
    float s = static_cast<float>(Scale(4));
    float width = Scale(3) / 2.0f;
    renderer.DrawLine(D2D1::Point2F(c.x - s, c.y - s), D2D1::Point2F(c.x + s, c.y + s), theme::kText3, width);
    renderer.DrawLine(D2D1::Point2F(c.x - s, c.y + s), D2D1::Point2F(c.x + s, c.y - s), theme::kText3, width);
}

Viewer::Action Viewer::OnStripClick(POINT pt, bool doubleClick)
{
    if (!StripContains(pt) || doubleClick)
        return Action::kNone;
    Strip strip = LayoutStrip();
    if (PtInRect(&strip.play, pt))
    {
        m_video.TogglePause();
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        return Action::kNone;
    }
    if (PtInRect(&strip.close, pt))
    {
        StopStrip();
        return Action::kStrip;
    }
    if (PtInRect(&strip.name, pt))
        return Action::kReopen;
    return Action::kNone;
}

void Viewer::StopStrip()
{
    m_background = false;
    KillTimer(m_hWnd, kViewerStripTimer);
    m_video.Stop();
    m_video.SetCovered(false);
    m_paths.clear();
    m_audio = false;
}
