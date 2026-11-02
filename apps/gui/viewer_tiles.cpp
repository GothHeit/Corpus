#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include "theme.hpp"
#include <algorithm>
#include <cmath>

// A picture bigger than the graphics card's bitmaps (or than memory allows) is decoded scaled down: the overview.
// Zoomed in past the overview's own sharpness, the part on screen is cut into tiles from the picture at the
// resolution the zoom needs and drawn over it, so the detail is there at full size. A tile that isn't ready leaves
// the overview showing through.

namespace
{
    // Tiles kept beyond the ones on screen and the ring around them that is read ahead.
    const size_t kMinCachedTiles = 128;
    const size_t kSpareTiles = 32;
    // Tiles turned into bitmaps in one paint, so a screenful arriving at once doesn't stall it.
    const int kUploadsPerPaint = 8;

    unsigned long long TileId(const TileKey &key)
    {
        return (static_cast<unsigned long long>(key.level) << 56) | (static_cast<unsigned long long>(key.row) << 28) |
            static_cast<unsigned long long>(key.col);
    }
}

// Whether the picture is shown in tiles at this zoom, and from which level: the coarsest still at least as sharp as
// the screen. Never one the overview already beats.
bool Viewer::TileLevel(int &level) const
{
    if (!m_tilesOpen || m_frame.sourceWidth == 0 || m_frame.width == 0)
        return false;
    double zoom = Zoom();
    double overview = static_cast<double>(m_frame.width) / m_frame.sourceWidth;
    if (zoom <= overview * 1.0001)
        return false;
    level = 0;
    while (level < 30 && std::ldexp(1.0, -(level + 1)) >= zoom)
        level++;
    return std::ldexp(1.0, -level) > overview * 1.0001;
}

void Viewer::ResetTiles()
{
    if (m_tilesOpen)
        m_tiles.Close();
    m_tilesOpen = false;
    m_tileCache.clear();
    m_tilePending.clear();
    m_tileWant.clear();
}

void Viewer::TakeTiles()
{
    if (!m_tilesOpen)
        return;
    size_t before = m_tilePending.size();
    m_tiles.Take(m_tilePending);
    if (m_tilePending.size() != before)
        Invalidate();
}

void Viewer::UploadTiles(Renderer &renderer)
{
    int made = 0;
    while (!m_tilePending.empty() && made < kUploadsPerPaint)
    {
        DecodedTile tile = std::move(m_tilePending.front());
        m_tilePending.erase(m_tilePending.begin());
        Tile &entry = m_tileCache[TileId(tile.key)];
        entry.used = ++m_tileClock;
        if (tile.width != 0 && tile.height != 0)
            entry.bitmap = renderer.CreateBitmap(tile.width, tile.height, tile.pixels.data());
        made++;
    }
    if (!m_tilePending.empty())
        Invalidate();
}

void Viewer::PaintTiles(Renderer &renderer, const D2D1_RECT_F &picture, bool smooth, bool blocks)
{
    int level = 0;
    if (!TileLevel(level))
    {
        // Zoomed back out: the tiles aren't needed, and their bitmaps are given back.
        if (!m_tileWant.empty())
        {
            m_tileWant.clear();
            m_tiles.Want(m_tileWant);
        }
        m_tileCache.clear();
        m_tilePending.clear();
        return;
    }

    const double zoom = Zoom();
    const UINT levelWidth = LevelSize(m_frame.sourceWidth, level);
    const UINT levelHeight = LevelSize(m_frame.sourceHeight, level);
    // Screen pixels per pixel of the level.
    const double scaleX = static_cast<double>(m_frame.sourceWidth) / levelWidth * zoom;
    const double scaleY = static_cast<double>(m_frame.sourceHeight) / levelHeight * zoom;
    const int columns = static_cast<int>((levelWidth + kTileSize - 1) / kTileSize);
    const int rows = static_cast<int>((levelHeight + kTileSize - 1) / kTileSize);

    // The tiles on screen, as columns and rows of the level.
    auto span = [](double from, double to, double origin, double scale, int count, int &first, int &last)
    {
        first = static_cast<int>(std::floor((from - origin) / scale / kTileSize));
        last = static_cast<int>(std::floor((to - origin) / scale / kTileSize - 1e-9));
        first = std::max(0, std::min(first, count - 1));
        last = std::max(first, std::min(last, count - 1));
    };
    int c0, c1, r0, r1;
    span(m_bounds.left, m_bounds.right, picture.left, scaleX, columns, c0, c1);
    span(m_bounds.top, m_bounds.bottom, picture.top, scaleY, rows, r0, r1);

    auto edge = [](double origin, double at, double scale) { return static_cast<float>(std::floor(origin + at * scale + 0.5)); };
    const double centerX = (m_bounds.left + m_bounds.right) / 2.0;
    const double centerY = (m_bounds.top + m_bounds.bottom) / 2.0;

    struct Want
    {
        TileKey key;
        double distance;
        bool visible;
    };
    std::vector<Want> missing;
    size_t onScreen = 0;
    // One ring of tiles beyond the screen is read ahead, for panning.
    for (int row = std::max(0, r0 - 1); row <= std::min(rows - 1, r1 + 1); row++)
    {
        for (int col = std::max(0, c0 - 1); col <= std::min(columns - 1, c1 + 1); col++)
        {
            bool visible = col >= c0 && col <= c1 && row >= r0 && row <= r1;
            TileKey key;
            key.level = level;
            key.col = col;
            key.row = row;
            if (visible)
                onScreen++;
            auto found = m_tileCache.find(TileId(key));
            if (found != m_tileCache.end())
            {
                found->second.used = ++m_tileClock;
                if (visible && found->second.bitmap)
                {
                    D2D1_RECT_F rect = D2D1::RectF(
                        edge(picture.left, col * static_cast<double>(kTileSize), scaleX),
                        edge(picture.top, row * static_cast<double>(kTileSize), scaleY),
                        edge(picture.left, std::min<double>(levelWidth, (col + 1.0) * kTileSize), scaleX),
                        edge(picture.top, std::min<double>(levelHeight, (row + 1.0) * kTileSize), scaleY));
                    // The overview under it is wiped first: a see-through picture would be blended twice.
                    renderer.FillRect(rect, theme::kViewerBackdrop);
                    renderer.DrawImage(found->second.bitmap.Get(), rect, smooth, blocks);
                }
                continue;
            }
            // Waiting to be turned into a bitmap.
            if (std::any_of(m_tilePending.begin(), m_tilePending.end(), [&](const DecodedTile &tile) { return tile.key == key; }))
                continue;
            double dx = picture.left + (col + 0.5) * kTileSize * scaleX - centerX;
            double dy = picture.top + (row + 0.5) * kTileSize * scaleY - centerY;
            missing.push_back({ key, dx * dx + dy * dy, visible });
        }
    }

    // The screen first, nearest the middle first.
    std::sort(missing.begin(), missing.end(), [](const Want &a, const Want &b)
    {
        return a.visible != b.visible ? a.visible : a.distance < b.distance;
    });
    std::vector<TileKey> want;
    for (const Want &item : missing)
        want.push_back(item.key);
    if (!(want == m_tileWant))
    {
        m_tileWant = want;
        m_tiles.Want(want);
    }

    // Tiles of other levels go first, then the least recently used.
    size_t limit = std::max(kMinCachedTiles, onScreen * 2 + kSpareTiles);
    while (m_tileCache.size() > limit)
    {
        auto oldest = m_tileCache.end();
        auto score = [level](unsigned long long id, const Tile &tile)
        {
            return (static_cast<int>(id >> 56) == level ? (1ull << 62) : 0ull) + tile.used;
        };
        for (auto it = m_tileCache.begin(); it != m_tileCache.end(); ++it)
        {
            if (oldest == m_tileCache.end() || score(it->first, it->second) < score(oldest->first, oldest->second))
                oldest = it;
        }
        m_tileCache.erase(oldest);
    }
}
