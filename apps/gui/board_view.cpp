#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "board_view.hpp"
#include "board_frames.hpp"
#include "file_kind.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    const int kBarHeight = 36;       // the tabs' row
    const int kTabHeight = 28;
    const int kTabPadding = 12;      // inside a tab, each side
    const int kTabGap = 2;
    const int kTabMaxWidth = 200;
    const int kPlusWidth = 28;
    const int kBarMargin = 16;       // from the edges of the view
    const int kTrayHeight = 124;     // header and one row of tiles
    const int kTrayHeader = 28;
    const int kTrayTile = 76;
    const int kTrayGap = 8;
    const int kTrayInset = 4;        // between a tile's plate and its picture
    const int kBaseSide = 240;       // canvas units: the long side of a file at scale 1
    const int kHandle = 10;          // the resize handle's side
    const int kHandleSlop = 6;
    const int kDragSlop = 4;         // pixels before a press on a tray tile becomes a drag
    const int kThumbSize = 512;      // the canvas asks for bigger thumbnails than the grid does
    const int kNudge = 8;            // canvas units; Shift takes five times as much
    const double kMinZoom = 0.05, kMaxZoom = 8.0;
    const double kMinScale = 0.05, kMaxScale = 40.0;
    const int kPlaceCell = 280;      // between files placed together
    const int kFitMargin = 48;
    const int kEmptyButtonHeight = 32;
    const int kMarkMin = 32;         // the play mark's side, at least and at most
    const int kMarkMax = 48;
    const int kMarkRoom = 56;        // a tile narrower or shorter than this shows no mark
    const int kMarkOverlay = 0;      // the libmpv overlay the mark goes in, over a video playing

    D2D1_RECT_F Inflate(const D2D1_RECT_F &r, float d)
    {
        return D2D1::RectF(r.left - d, r.top - d, r.right + d, r.bottom + d);
    }
}

// ---- Setup ----

void BoardView::StartThumbnails(HWND hWnd, const std::wstring &folder)
{
    m_hWnd = hWnd;
    m_thumbFolder = folder;
}

void BoardView::OnThumbnailsReady()
{
    if (m_thumbsStarted && m_thumbs.TakeFinished())
        Invalidate();
}

int BoardView::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void BoardView::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void BoardView::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
}

void BoardView::SetExporting(bool exporting)
{
    m_exporting = exporting;
    Invalidate();
}

std::wstring BoardView::TakeError()
{
    std::wstring error = m_failed ? strings::kBoardSaveFailed : m_videoFailed ? strings::kBoardVideoFailed : L"";
    m_failed = m_videoFailed = false;
    return error;
}

void BoardView::Fail()
{
    m_failed = true;
    Rebuild();
}

// ---- Boards and files ----

void BoardView::RememberView()
{
    if (!m_board)
        return;
    ViewState &state = m_views[m_board->get_db_id()];
    state.cx = m_cx;
    state.cy = m_cy;
    state.zoom = m_zoom;
    state.set = true;
}

void BoardView::SetBoards(const std::vector<board*> &boards, board *current)
{
    if (current != m_board)
    {
        StopVideo();
        RememberView();
        m_board = current;
        if (!m_selected.empty())
        {
            m_selected.clear();
            m_selectionChanged = true;
        }
        m_trayScroll = 0;
        m_items.clear();
        m_order.clear();
        m_tray.clear();
        auto saved = current ? m_views.find(current->get_db_id()) : m_views.end();
        if (saved != m_views.end() && saved->second.set)
        {
            m_cx = saved->second.cx;
            m_cy = saved->second.cy;
            m_zoom = saved->second.zoom;
            m_fitPending = false;
        }
        else
            m_fitPending = true;
    }
    m_tabs.clear();
    for (const board *b : boards)
        m_tabs.push_back({ b->get_db_id(), Utf8ToWide(b->get_name()) });
    m_hover.Reset(m_hWnd, kBoardHoverTimer);
    m_markFade.Reset(m_hWnd, kBoardMarkTimer);
    Invalidate();
}

void BoardView::SetFiles(const std::vector<file*> &files)
{
    m_matches = files;
    Rebuild();
    if (m_fitPending && !m_items.empty() && CanvasRect().right > CanvasRect().left)
    {
        Fit();
        m_fitPending = false;
    }
    Invalidate();
}

void BoardView::Clear(bool boardToo)
{
    if (m_mode != Mode::kNone)
    {
        m_mode = Mode::kNone;
        ReleaseCapture();
    }
    StopVideo();
    m_markFade.Reset(m_hWnd, kBoardMarkTimer);
    m_matches.clear();
    m_byId.clear();
    m_items.clear();
    m_order.clear();
    m_tray.clear();
    m_before.clear();
    m_opened = nullptr;
    if (!m_selected.empty())
    {
        m_selected.clear();
        m_selectionChanged = true;
    }
    if (boardToo)
    {
        RememberView();
        m_board = nullptr;
    }
    Invalidate();
}

std::int64_t BoardView::IdOf(const file *f) const
{
    return f ? f->get_db_id() : 0;
}

// Places and tray from the board and the files the search finds, keeping the selection of files still on show.
void BoardView::Rebuild()
{
    m_items.clear();
    m_tray.clear();
    m_byId.clear();
    for (file *f : m_matches)
        m_byId[f->get_db_id()] = f;
    std::unordered_set<std::int64_t> placed;
    if (m_board)
    {
        for (const board_item &bi : m_board->get_items())
        {
            auto it = m_byId.find(bi.file_id);
            if (it == m_byId.end())
                continue;
            Item item;
            item.f = it->second;
            item.id = bi.file_id;
            item.x = bi.x;
            item.y = bi.y;
            item.scale = bi.scale;
            item.z = bi.z;
            item.frame = bi.frame;
            m_items.push_back(item);
            placed.insert(bi.file_id);
        }
        for (file *f : m_matches)
        {
            if (!placed.count(f->get_db_id()))
                m_tray.push_back(f);
        }
    }
    size_t before = m_selected.size();
    for (auto it = m_selected.begin(); it != m_selected.end();)
        it = m_byId.count(*it) ? std::next(it) : m_selected.erase(it);
    if (m_selected.size() != before)
        m_selectionChanged = true;
    SortOrder();
    int most = std::max<int>(0, TrayContentWidth() - (m_bounds.right - m_bounds.left - 2 * Scale(kBarMargin)));
    m_trayScroll = std::min(m_trayScroll, most);
    // The marks are by index, which changed; a video no longer placed (taken off, or the search lost it) stops.
    m_markFade.Reset(m_hWnd, kBoardMarkTimer);
    m_onMark = false;
    if (m_playId && !FindItem(m_playId))
        StopVideo();
}

void BoardView::SortOrder()
{
    m_order.resize(m_items.size());
    for (size_t i = 0; i < m_items.size(); i++)
        m_order[i] = i;
    std::sort(m_order.begin(), m_order.end(), [this](size_t a, size_t b)
    {
        if (m_items[a].z != m_items[b].z)
            return m_items[a].z < m_items[b].z;
        return m_items[a].id < m_items[b].id;
    });
}

double BoardView::NextZ() const
{
    int top = 0;
    for (const Item &item : m_items)
        top = std::max(top, item.z);
    return top + 1;
}

BoardView::Item *BoardView::FindItem(std::int64_t id)
{
    for (Item &item : m_items)
    {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

const BoardView::Item *BoardView::FindItem(std::int64_t id) const
{
    for (const Item &item : m_items)
    {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

// ---- Selection ----

std::vector<file*> BoardView::SelectedFiles() const
{
    std::vector<file*> files;
    for (file *f : m_matches)
    {
        if (m_selected.count(f->get_db_id()))
            files.push_back(f);
    }
    return files;
}

void BoardView::SetSelection(const std::unordered_set<std::int64_t> &ids)
{
    if (ids == m_selected)
        return;
    m_selected = ids;
    m_selectionChanged = true;
    Invalidate();
}

void BoardView::SelectPath(const std::string &path)
{
    std::unordered_set<std::int64_t> ids;
    for (file *f : m_matches)
    {
        if (f->get_path() == path)
            ids.insert(f->get_db_id());
    }
    SetSelection(ids);
}

// ---- Layout ----

RECT BoardView::BarRect() const
{
    return { m_bounds.left, m_bounds.top, m_bounds.right, std::min<LONG>(m_bounds.bottom, m_bounds.top + Scale(kBarHeight)) };
}

RECT BoardView::TrayRect() const
{
    if (!TrayShown())
        return { m_bounds.left, m_bounds.bottom, m_bounds.right, m_bounds.bottom };
    return { m_bounds.left, std::max<LONG>(BarRect().bottom, m_bounds.bottom - Scale(kTrayHeight)), m_bounds.right, m_bounds.bottom };
}

RECT BoardView::CanvasRect() const
{
    return { m_bounds.left, BarRect().bottom, m_bounds.right, TrayRect().top };
}

RECT BoardView::ExportRect() const
{
    RECT bar = BarRect();
    std::wstring label = m_exporting ? strings::kBoardExporting : strings::kBoardExport;
    int width = static_cast<int>(m_renderer->TextWidth(label, Font::kCaption) + 0.99f) + 2 * Scale(kTabPadding);
    int top = bar.top + (bar.bottom - bar.top - Scale(kTabHeight)) / 2;
    return { bar.right - Scale(kBarMargin) - width, top, bar.right - Scale(kBarMargin), top + Scale(kTabHeight) };
}

RECT BoardView::TabsArea() const
{
    RECT bar = BarRect();
    int top = bar.top + (bar.bottom - bar.top - Scale(kTabHeight)) / 2;
    return { bar.left + Scale(kBarMargin), top, std::max<LONG>(bar.left + Scale(kBarMargin), ExportRect().left - Scale(12)),
        top + Scale(kTabHeight) };
}

std::vector<BoardView::TabBox> BoardView::TabBoxes() const
{
    std::vector<TabBox> boxes;
    RECT area = TabsArea();
    int x = area.left - m_tabScroll;
    for (size_t i = 0; i < m_tabs.size(); i++)
    {
        int width = std::min(Scale(kTabMaxWidth),
            static_cast<int>(m_renderer->TextWidth(m_tabs[i].name, Font::kCaptionStrong) + 0.99f) + 2 * Scale(kTabPadding));
        boxes.push_back({ { x, area.top, x + width, area.bottom }, static_cast<int>(i) });
        x += width + Scale(kTabGap);
    }
    boxes.push_back({ { x, area.top, x + Scale(kPlusWidth), area.bottom }, static_cast<int>(m_tabs.size()) });
    return boxes;
}

int BoardView::TabsWidth() const
{
    std::vector<TabBox> boxes = TabBoxes();
    return boxes.empty() ? 0 : boxes.back().rect.right - boxes.front().rect.left;
}

RECT BoardView::PlaceAllRect() const
{
    RECT tray = TrayRect();
    int width = static_cast<int>(m_renderer->TextWidth(strings::kBoardPlaceAll, Font::kCaption) + 0.99f) + 2 * Scale(kTabPadding);
    int height = Scale(kTrayHeader) - Scale(4);
    return { tray.right - Scale(kBarMargin) - width, tray.top + Scale(2), tray.right - Scale(kBarMargin), tray.top + Scale(2) + height };
}

int BoardView::TrayContentWidth() const
{
    return static_cast<int>(m_tray.size()) * (Scale(kTrayTile) + Scale(kTrayGap));
}

RECT BoardView::TrayTileRect(size_t index) const
{
    RECT tray = TrayRect();
    int x = tray.left + Scale(kBarMargin) - m_trayScroll + static_cast<int>(index) * (Scale(kTrayTile) + Scale(kTrayGap));
    int y = tray.top + Scale(kTrayHeader);
    return { x, y, x + Scale(kTrayTile), y + Scale(kTrayTile) };
}

int BoardView::TrayAt(POINT pt) const
{
    RECT tray = TrayRect();
    if (!TrayShown() || !PtInRect(&tray, pt) || pt.y < tray.top + Scale(kTrayHeader))
        return -1;
    int step = Scale(kTrayTile) + Scale(kTrayGap);
    int rel = pt.x - (tray.left + Scale(kBarMargin) - m_trayScroll);
    if (rel < 0)
        return -1;
    int index = rel / step;
    if (index >= static_cast<int>(m_tray.size()) || rel % step >= Scale(kTrayTile))
        return -1;
    return index;
}

RECT BoardView::EmptyButtonRect() const
{
    RECT canvas = CanvasRect();
    int width = static_cast<int>(m_renderer->TextWidth(strings::kBoardEmptyButton, Font::kBody) + 0.99f) + 2 * Scale(20);
    int middle = (canvas.top + canvas.bottom) / 2;
    int left = (canvas.left + canvas.right - width) / 2;
    int top = middle + Scale(40);
    return { left, top, left + width, top + Scale(kEmptyButtonHeight) };
}

// ---- Canvas geometry ----

D2D1_POINT_2F BoardView::ToScreen(double wx, double wy) const
{
    RECT c = CanvasRect();
    float k = Zoom();
    return D2D1::Point2F(static_cast<float>((c.left + c.right) / 2.0 + (wx - m_cx) * k),
        static_cast<float>((c.top + c.bottom) / 2.0 + (wy - m_cy) * k));
}

void BoardView::ToWorld(POINT pt, double &wx, double &wy) const
{
    RECT c = CanvasRect();
    float k = Zoom();
    wx = m_cx + (pt.x - (c.left + c.right) / 2.0) / k;
    wy = m_cy + (pt.y - (c.top + c.bottom) / 2.0) / k;
}

// The size on the canvas, from the picture's proportions once they are known, else a square.
void BoardView::SizeOf(const Item &item, double &w, double &h) const
{
    double aspect = 1.0;
    auto it = m_aspect.find(item.id);
    if (it != m_aspect.end() && it->second > 0.0)
        aspect = it->second;
    double side = kBaseSide * item.scale;
    if (aspect >= 1.0)
    {
        w = side;
        h = side / aspect;
    }
    else
    {
        h = side;
        w = side * aspect;
    }
}

D2D1_RECT_F BoardView::ItemBox(const Item &item) const
{
    double w, h;
    SizeOf(item, w, h);
    D2D1_POINT_2F a = ToScreen(item.x - w / 2, item.y - h / 2);
    D2D1_POINT_2F b = ToScreen(item.x + w / 2, item.y + h / 2);
    return D2D1::RectF(a.x, a.y, b.x, b.y);
}

RECT BoardView::HandleRect(const Item &item) const
{
    D2D1_RECT_F box = ItemBox(item);
    int half = Scale(kHandle) / 2;
    int x = static_cast<int>(box.right), y = static_cast<int>(box.bottom);
    return { x - half, y - half, x + half, y + half };
}

// The topmost placed file under the point, as an index into m_items, or -1.
int BoardView::ItemAt(POINT pt) const
{
    RECT canvas = CanvasRect();
    if (!PtInRect(&canvas, pt))
        return -1;
    for (size_t k = m_order.size(); k-- > 0;)
    {
        D2D1_RECT_F box = ItemBox(m_items[m_order[k]]);
        if (pt.x >= box.left && pt.x < box.right && pt.y >= box.top && pt.y < box.bottom)
            return static_cast<int>(m_order[k]);
    }
    return -1;
}

file *BoardView::FileAt(POINT pt) const
{
    int item = ItemAt(pt);
    if (item >= 0)
        return m_items[static_cast<size_t>(item)].f;
    int tile = TrayAt(pt);
    return tile >= 0 ? m_tray[static_cast<size_t>(tile)] : nullptr;
}

bool BoardView::Bounds(double &l, double &t, double &r, double &b) const
{
    if (m_items.empty())
        return false;
    l = t = 1e300;
    r = b = -1e300;
    for (const Item &item : m_items)
    {
        double w, h;
        SizeOf(item, w, h);
        l = std::min(l, item.x - w / 2);
        t = std::min(t, item.y - h / 2);
        r = std::max(r, item.x + w / 2);
        b = std::max(b, item.y + h / 2);
    }
    return true;
}

// Shows everything placed, as large as fits (and no larger than life).
void BoardView::Fit()
{
    double l, t, r, b;
    if (!Bounds(l, t, r, b))
    {
        m_cx = m_cy = 0.0;
        m_zoom = 1.0;
        Invalidate();
        return;
    }
    RECT c = CanvasRect();
    double unit = Unit();
    double width = std::max(1.0, r - l), height = std::max(1.0, b - t);
    double room = Scale(kFitMargin) * 2.0;
    double zoom = std::min((c.right - c.left - room) / (width * unit), (c.bottom - c.top - room) / (height * unit));
    m_zoom = std::max(kMinZoom, std::min(1.0, zoom));
    m_cx = (l + r) / 2;
    m_cy = (t + b) / 2;
    Invalidate();
}

void BoardView::ZoomAt(POINT pt, double factor)
{
    double wx, wy;
    ToWorld(pt, wx, wy);
    m_zoom = std::max(kMinZoom, std::min(kMaxZoom, m_zoom * factor));
    RECT c = CanvasRect();
    float k = Zoom();
    m_cx = wx - (pt.x - (c.left + c.right) / 2.0) / k;
    m_cy = wy - (pt.y - (c.top + c.bottom) / 2.0) / k;
    Invalidate();
}

// ---- Changes ----

// Writes these placed files (by id) to the catalog as they are on screen. A refusal puts the picture back.
void BoardView::Commit(const std::vector<std::int64_t> &ids)
{
    if (!m_lib || !m_board)
        return;
    std::vector<board_item> items;
    for (std::int64_t id : ids)
    {
        if (const Item *item = FindItem(id))
        {
            board_item bi;
            bi.file_id = item->id;
            bi.x = item->x;
            bi.y = item->y;
            bi.scale = item->scale;
            bi.z = item->z;
            bi.frame = item->frame;
            items.push_back(bi);
        }
    }
    if (!items.empty() && !m_lib->place_on_board(m_board, items))
        Fail();
}

// Puts files on the canvas around a point, side by side, at their natural size. Only on a user's request.
void BoardView::PlaceFiles(const std::vector<file*> &files, double wx, double wy)
{
    if (!m_lib || !m_board || files.empty())
        return;
    size_t count = files.size();
    size_t columns = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    size_t rows = (count + columns - 1) / columns;
    double left = wx - (columns - 1) * kPlaceCell / 2.0, top = wy - (rows - 1) * kPlaceCell / 2.0;
    int z = static_cast<int>(NextZ());
    std::vector<board_item> items;
    std::unordered_set<std::int64_t> ids;
    for (size_t i = 0; i < count; i++)
    {
        board_item bi;
        bi.file_id = files[i]->get_db_id();
        bi.x = left + (i % columns) * kPlaceCell;
        bi.y = top + (i / columns) * kPlaceCell;
        bi.scale = 1.0;
        bi.z = z + static_cast<int>(i);
        items.push_back(bi);
        ids.insert(bi.file_id);
    }
    if (!m_lib->place_on_board(m_board, items))
    {
        Fail();
        return;
    }
    Rebuild();
    SetSelection(ids);
}

// "Place all": the whole tray as a block to the right of what is on the canvas, then the picture fits everything.
void BoardView::PlaceAll()
{
    if (m_tray.empty())
        return;
    size_t count = m_tray.size();
    size_t columns = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    size_t rows = (count + columns - 1) / columns;
    double wx = 0.0, wy = 0.0, l, t, r, b;
    double blockWidth = columns * static_cast<double>(kPlaceCell), blockHeight = rows * static_cast<double>(kPlaceCell);
    if (Bounds(l, t, r, b))
    {
        wx = r + 120 + blockWidth / 2;
        wy = t + blockHeight / 2;
    }
    PlaceFiles(m_tray, wx, wy);
    // Everything just placed would be selected: a calm board shows none selected.
    SetSelection({});
    Fit();
}

void BoardView::PlaceSelectedTray()
{
    std::vector<file*> files;
    for (file *f : m_tray)
    {
        if (m_selected.count(f->get_db_id()))
            files.push_back(f);
    }
    PlaceFiles(files, m_cx, m_cy);
}

void BoardView::TakeOff()
{
    if (!m_lib || !m_board)
        return;
    std::vector<std::int64_t> ids;
    for (const Item &item : m_items)
    {
        if (m_selected.count(item.id))
            ids.push_back(item.id);
    }
    if (ids.empty())
        return;
    if (!m_lib->unplace_from_board(m_board, ids))
    {
        Fail();
        return;
    }
    SweepFrames();
    Rebuild();
    Invalidate();
}

void BoardView::BringToFront(bool front)
{
    std::vector<std::int64_t> ids;
    int low = 0;
    for (const Item &item : m_items)
        low = std::min(low, item.z);
    int top = static_cast<int>(NextZ());
    int count = 0;
    for (const Item &item : m_items)
        count += m_selected.count(item.id) > 0;
    // Keep the selection's own order among itself, whichever way it goes.
    for (size_t index : m_order)
    {
        Item &item = m_items[index];
        if (!m_selected.count(item.id))
            continue;
        item.z = front ? top++ : low - count + static_cast<int>(ids.size());
        ids.push_back(item.id);
    }
    if (ids.empty())
        return;
    Commit(ids);
    SortOrder();
    Invalidate();
}

std::vector<BoardExportItem> BoardView::ExportItems() const
{
    std::vector<BoardExportItem> out;
    for (size_t index : m_order)
    {
        const Item &item = m_items[index];
        BoardExportItem e;
        e.path = item.f->get_path();
        // A video paused on is exported as that frame, a PNG the export decodes like any picture.
        if (item.frame >= 0.0 && IsVideo(item.f))
        {
            std::wstring frame = BoardFramePath(m_thumbFolder, item.id, item.frame);
            if (GetFileAttributesW(frame.c_str()) != INVALID_FILE_ATTRIBUTES)
                e.path = WideToUtf8(frame);
        }
        e.x = item.x;
        e.y = item.y;
        SizeOf(item, e.width, e.height);
        out.push_back(e);
    }
    return out;
}

// ---- Input ----

BoardView::Action BoardView::OnLButtonDown(POINT pt, bool ctrl, bool shift, bool doubleClick)
{
    (void)shift;
    m_opened = nullptr;
    m_mode = Mode::kNone;
    m_moved = m_raised = false;
    m_before.clear();
    m_pressed = 0;
    m_start = m_point = pt;

    // The bar.
    RECT bar = BarRect();
    if (PtInRect(&bar, pt))
    {
        RECT exportRect = ExportRect();
        if (PtInRect(&exportRect, pt))
            return m_exporting || m_items.empty() ? Action::kNone : Action::kExport;
        RECT area = TabsArea();
        if (PtInRect(&area, pt))
        {
            for (const TabBox &box : TabBoxes())
            {
                if (!PtInRect(&box.rect, pt))
                    continue;
                if (box.index == static_cast<int>(m_tabs.size()))
                    return Action::kNewBoard;
                m_actionTab = box.index;
                return Action::kSwitch;
            }
        }
        return Action::kNone;
    }

    // The empty view's button.
    if (m_tabs.empty())
    {
        RECT button = EmptyButtonRect();
        return PtInRect(&button, pt) ? Action::kNewBoard : Action::kNone;
    }

    // The tray.
    RECT tray = TrayRect();
    if (TrayShown() && PtInRect(&tray, pt))
    {
        RECT all = PlaceAllRect();
        if (PtInRect(&all, pt))
        {
            PlaceAll();
            return Action::kNone;
        }
        int tile = TrayAt(pt);
        if (tile < 0)
            return Action::kNone;
        file *f = m_tray[static_cast<size_t>(tile)];
        std::int64_t id = f->get_db_id();
        m_pressed = id;
        m_pressCtrl = ctrl;
        m_pressedSelected = m_selected.count(id) > 0;
        if (doubleClick)
        {
            PlaceFiles({ f }, m_cx, m_cy);
            return Action::kNone;
        }
        std::unordered_set<std::int64_t> ids = m_selected;
        if (ctrl)
        {
            if (!ids.erase(id))
                ids.insert(id);
        }
        else if (!m_pressedSelected)
            ids = { id };
        SetSelection(ids);
        if (ids.count(id))
        {
            m_mode = Mode::kTrayPress;
            SetCapture(m_hWnd);
        }
        return Action::kNone;
    }

    // The canvas: the handle of the one selected file, a file, or empty space.
    if (m_selected.size() == 1)
    {
        if (const Item *item = FindItem(*m_selected.begin()))
        {
            RECT handle = HandleRect(*item);
            InflateRect(&handle, Scale(kHandleSlop), Scale(kHandleSlop));
            if (PtInRect(&handle, pt))
            {
                D2D1_RECT_F box = ItemBox(*item);
                float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
                m_resizeFrom = std::max(1.0, static_cast<double>(std::hypot(pt.x - cx, pt.y - cy)));
                m_before[item->id] = *item;
                m_pressed = item->id;
                m_mode = Mode::kResize;
                SetCapture(m_hWnd);
                return Action::kNone;
            }
        }
    }

    int hit = ItemAt(pt);
    if (hit >= 0 && !ctrl && !shift && OnMark(m_items[static_cast<size_t>(hit)], pt))
    {
        // The second press of a double-click on the mark is part of the same play, not an open.
        if (doubleClick)
            return Action::kNone;
        m_opened = m_items[static_cast<size_t>(hit)].f;
        return IsVideo(m_opened) ? Action::kPlayVideo : Action::kPlaySong;
    }
    if (hit >= 0)
    {
        Item &item = m_items[static_cast<size_t>(hit)];
        m_pressed = item.id;
        m_pressCtrl = ctrl;
        m_pressedSelected = m_selected.count(item.id) > 0;
        if (doubleClick)
        {
            m_opened = item.f;
            return Action::kOpen;
        }
        std::unordered_set<std::int64_t> ids = m_selected;
        if (ctrl)
        {
            if (!ids.erase(item.id))
                ids.insert(item.id);
        }
        else if (!m_pressedSelected)
            ids = { item.id };
        SetSelection(ids);
        if (!ids.count(item.id))
            return Action::kNone;
        // The press brings the file to the front; the move (or the release) writes it down.
        double top = NextZ();
        bool alone = true;
        for (const Item &other : m_items)
            alone = alone && (other.id == item.id || other.z < item.z);
        if (!alone)
        {
            item.z = static_cast<int>(top);
            m_raised = true;
            SortOrder();
        }
        for (const Item &other : m_items)
        {
            if (ids.count(other.id))
                m_before[other.id] = other;
        }
        m_mode = Mode::kMove;
        SetCapture(m_hWnd);
        return Action::kNone;
    }

    if (!ctrl)
        SetSelection({});
    m_startCx = m_cx;
    m_startCy = m_cy;
    m_mode = Mode::kPan;
    SetCapture(m_hWnd);
    return Action::kNone;
}

BoardView::Action BoardView::OnRButtonDown(POINT pt)
{
    m_opened = nullptr;
    RECT bar = BarRect();
    if (PtInRect(&bar, pt))
    {
        RECT area = TabsArea();
        if (PtInRect(&area, pt))
        {
            for (const TabBox &box : TabBoxes())
            {
                if (PtInRect(&box.rect, pt) && box.index < static_cast<int>(m_tabs.size()))
                {
                    m_actionTab = box.index;
                    return Action::kTabMenu;
                }
            }
        }
        return Action::kNone;
    }
    file *f = FileAt(pt);
    if (!f)
        return Action::kNone;
    if (!m_selected.count(f->get_db_id()))
        SetSelection({ f->get_db_id() });
    return Action::kFileMenu;
}

bool BoardView::OnMouseMove(POINT pt)
{
    if (m_mode == Mode::kNone)
        return false;
    m_point = pt;
    switch (m_mode)
    {
    case Mode::kPan:
    {
        float k = Zoom();
        m_cx = m_startCx - (pt.x - m_start.x) / k;
        m_cy = m_startCy - (pt.y - m_start.y) / k;
        break;
    }
    case Mode::kMove:
    {
        float k = Zoom();
        if (!m_moved && std::hypot(pt.x - m_start.x, pt.y - m_start.y) < Scale(kDragSlop))
            return true;
        m_moved = true;
        for (auto &entry : m_before)
        {
            if (Item *item = FindItem(entry.first))
            {
                item->x = entry.second.x + (pt.x - m_start.x) / k;
                item->y = entry.second.y + (pt.y - m_start.y) / k;
            }
        }
        break;
    }
    case Mode::kResize:
    {
        auto it = m_before.find(m_pressed);
        Item *item = FindItem(m_pressed);
        if (it == m_before.end() || !item)
            break;
        // The file grows or shrinks about its centre as the pointer goes nearer or farther from it.
        D2D1_POINT_2F c = ToScreen(it->second.x, it->second.y);
        double distance = std::hypot(pt.x - c.x, pt.y - c.y);
        item->scale = std::max(kMinScale, std::min(kMaxScale, it->second.scale * distance / m_resizeFrom));
        m_moved = true;
        break;
    }
    case Mode::kTrayPress:
        if (std::hypot(pt.x - m_start.x, pt.y - m_start.y) >= Scale(kDragSlop))
            m_mode = Mode::kTrayDrag;
        break;
    case Mode::kTrayDrag:
        break;
    case Mode::kNone:
        break;
    }
    Invalidate();
    return true;
}

void BoardView::OnLButtonUp(POINT pt)
{
    Mode mode = m_mode;
    m_mode = Mode::kNone;
    if (mode != Mode::kNone)
        ReleaseCapture();
    switch (mode)
    {
    case Mode::kMove:
        if (m_moved || m_raised)
        {
            std::vector<std::int64_t> ids;
            for (const auto &entry : m_before)
                ids.push_back(entry.first);
            // The raised file may have been pressed first among several: all of them are written.
            Commit(ids);
        }
        else if (!m_pressCtrl && m_pressedSelected && m_selected.size() > 1)
            SetSelection({ m_pressed });
        break;
    case Mode::kResize:
        if (m_moved)
            Commit({ m_pressed });
        break;
    case Mode::kTrayPress:
        if (!m_pressCtrl && m_pressedSelected && m_selected.size() > 1)
            SetSelection({ m_pressed });
        break;
    case Mode::kTrayDrag:
    {
        RECT canvas = CanvasRect();
        if (PtInRect(&canvas, pt))
        {
            double wx, wy;
            ToWorld(pt, wx, wy);
            std::vector<file*> files;
            for (file *f : m_tray)
            {
                if (m_selected.count(f->get_db_id()))
                    files.push_back(f);
            }
            PlaceFiles(files, wx, wy);
        }
        break;
    }
    case Mode::kPan:
    case Mode::kNone:
        break;
    }
    m_before.clear();
    Invalidate();
}

void BoardView::OnCaptureChanged()
{
    if (m_mode == Mode::kNone)
        return;
    // The drag was cut short (another window took the pointer): the files go back to where the catalog has them.
    m_mode = Mode::kNone;
    m_before.clear();
    Rebuild();
    Invalidate();
}

bool BoardView::OnMouseWheel(POINT pt, int delta)
{
    if (!Contains(pt))
        return false;
    RECT bar = BarRect();
    RECT tray = TrayRect();
    if (PtInRect(&bar, pt))
    {
        RECT area = TabsArea();
        int most = std::max<int>(0, TabsWidth() - (area.right - area.left));
        m_tabScroll = std::max(0, std::min(most, m_tabScroll - delta * Scale(60) / WHEEL_DELTA));
        Invalidate();
        return true;
    }
    if (TrayShown() && PtInRect(&tray, pt))
    {
        int most = std::max<int>(0, TrayContentWidth() - (tray.right - tray.left - 2 * Scale(kBarMargin)));
        m_trayScroll = std::max(0, std::min(most, m_trayScroll - delta * Scale(80) / WHEEL_DELTA));
        Invalidate();
        return true;
    }
    if (m_mode == Mode::kNone)
        ZoomAt(pt, std::pow(1.1, delta / static_cast<double>(WHEEL_DELTA)));
    return true;
}

void BoardView::OnHover(POINT pt)
{
    m_hoverPoint = pt;
    int id = -1, tray = -1;
    RECT bar = BarRect();
    RECT emptyButton = EmptyButtonRect();
    if (m_tabs.empty() && PtInRect(&emptyButton, pt))
        id = 0;  // the only thing to hover while there are no tabs
    else if (PtInRect(&bar, pt))
    {
        RECT exportRect = ExportRect();
        RECT area = TabsArea();
        if (PtInRect(&exportRect, pt) && !m_exporting && !m_items.empty())
            id = static_cast<int>(m_tabs.size()) + 1;
        else if (PtInRect(&area, pt))
        {
            for (const TabBox &box : TabBoxes())
            {
                if (PtInRect(&box.rect, pt))
                    id = box.index;
            }
        }
    }
    else if (TrayShown())
    {
        RECT all = PlaceAllRect();
        if (PtInRect(&all, pt))
            id = static_cast<int>(m_tabs.size()) + 2;
        tray = TrayAt(pt);
    }
    bool changed = m_hover.Set(m_hWnd, kBoardHoverTimer, id);
    if (tray != m_hoverTray)
    {
        m_hoverTray = tray;
        changed = true;
    }
    // The play mark of the video or song under the pointer.
    int item = m_mode == Mode::kNone && !m_tabs.empty() ? ItemAt(pt) : -1;
    D2D1_POINT_2F center;
    float radius;
    if (item >= 0 && !MarkBox(m_items[static_cast<size_t>(item)], center, radius))
        item = -1;
    changed = m_markFade.Set(m_hWnd, kBoardMarkTimer, item) || changed;
    bool onMark = item >= 0 && OnMark(m_items[static_cast<size_t>(item)], pt);
    if (onMark != m_onMark)
    {
        m_onMark = onMark;
        changed = true;
    }
    if (changed)
        Invalidate();
}

void BoardView::OnTimer(UINT_PTR timer)
{
    if (timer == kBoardHoverTimer)
        m_hover.Step(m_hWnd, timer);
    else if (timer == kBoardMarkTimer)
        m_markFade.Step(m_hWnd, timer);
    Invalidate();
}

bool BoardView::OnKeyDown(WPARAM key, bool ctrl, bool shift)
{
    RECT canvas = CanvasRect();
    POINT middle = { (canvas.left + canvas.right) / 2, (canvas.top + canvas.bottom) / 2 };
    bool selectedPlaced = false;
    for (const Item &item : m_items)
        selectedPlaced = selectedPlaced || m_selected.count(item.id) > 0;

    switch (key)
    {
    case VK_DELETE:
        if (shift)
            return false;  // Shift+Del deletes from disk, as everywhere
        TakeOff();
        return true;
    case VK_BACK:
        TakeOff();
        return true;
    case VK_ESCAPE:
        if (m_selected.empty())
            return false;
        SetSelection({});
        return true;
    case 'A':
        if (!ctrl)
            return false;
        {
            std::unordered_set<std::int64_t> all;
            for (file *f : m_matches)
                all.insert(f->get_db_id());
            SetSelection(all);
        }
        return true;
    case VK_HOME:
        Fit();
        return true;
    case 'P':
        if (ctrl)
            return false;
        PlaceSelectedTray();
        return true;
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    {
        double dx = key == VK_LEFT ? -1 : key == VK_RIGHT ? 1 : 0, dy = key == VK_UP ? -1 : key == VK_DOWN ? 1 : 0;
        if (!selectedPlaced)
        {
            float k = Zoom();
            m_cx += dx * Scale(60) / k;
            m_cy += dy * Scale(60) / k;
            Invalidate();
            return true;
        }
        std::vector<std::int64_t> ids;
        double step = shift ? kNudge * 5 : kNudge;
        for (Item &item : m_items)
        {
            if (m_selected.count(item.id))
            {
                item.x += dx * step;
                item.y += dy * step;
                ids.push_back(item.id);
            }
        }
        Commit(ids);
        Invalidate();
        return true;
    }
    case VK_OEM_PLUS: case VK_ADD: case VK_OEM_MINUS: case VK_SUBTRACT:
    {
        bool grow = key == VK_OEM_PLUS || key == VK_ADD;
        if (ctrl || !selectedPlaced)
        {
            ZoomAt(middle, grow ? 1.15 : 1.0 / 1.15);
            return true;
        }
        std::vector<std::int64_t> ids;
        for (Item &item : m_items)
        {
            if (m_selected.count(item.id))
            {
                item.scale = std::max(kMinScale, std::min(kMaxScale, item.scale * (grow ? 1.1 : 1.0 / 1.1)));
                ids.push_back(item.id);
            }
        }
        Commit(ids);
        Invalidate();
        return true;
    }
    }
    return false;
}

bool BoardView::NewBoardAt(POINT pt) const
{
    if (m_tabs.empty())
    {
        RECT button = EmptyButtonRect();
        return PtInRect(&button, pt) != 0;
    }
    RECT area = TabsArea();
    std::vector<TabBox> boxes = TabBoxes();
    return !boxes.empty() && PtInRect(&area, pt) && PtInRect(&boxes.back().rect, pt);
}

bool BoardView::OnChar(wchar_t ch)
{
    if (ch != L']' && ch != L'[')
        return false;
    BringToFront(ch == L']');
    return true;
}

BoardView::Cursor BoardView::CursorAt(POINT pt) const
{
    if (m_mode == Mode::kPan || m_mode == Mode::kMove || m_mode == Mode::kTrayDrag)
        return Cursor::kMove;
    if (m_mode == Mode::kResize)
        return Cursor::kResize;
    RECT bar = BarRect();
    if (PtInRect(&bar, pt))
        return Cursor::kArrow;
    if (m_selected.size() == 1)
    {
        if (const Item *item = FindItem(*m_selected.begin()))
        {
            RECT handle = HandleRect(*item);
            InflateRect(&handle, Scale(kHandleSlop), Scale(kHandleSlop));
            if (PtInRect(&handle, pt))
                return Cursor::kResize;
        }
    }
    return ItemAt(pt) >= 0 || TrayAt(pt) >= 0 ? Cursor::kHand : Cursor::kArrow;
}

bool BoardView::TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const
{
    RECT bar = BarRect();
    if (!PtInRect(&bar, pt))
        return false;
    std::vector<TabBox> boxes = TabBoxes();
    RECT area = TabsArea();
    if (!boxes.empty() && PtInRect(&area, pt) && PtInRect(&boxes.back().rect, pt))
    {
        anchor = boxes.back().rect;
        text = strings::kBoardNewTip;
        return true;
    }
    return false;
}

// ---- Painting ----

void BoardView::Paint(Renderer &renderer) const
{
    RECT canvas = CanvasRect();
    renderer.FillRect(ToRectF(m_bounds), theme::kLayer);

    if (!m_thumbsStarted && m_hWnd)
    {
        m_thumbs.Start(m_hWnd, m_thumbFolder);
        m_thumbsStarted = true;
    }
    m_thumbs.BeginPaint(renderer, kThumbSize);

    bool fading = false;
    float line = Unit();
    if (!m_tabs.empty() && !m_items.empty())
    {
        // What is on the canvas is asked for first, top to bottom; the tray after.
        std::vector<std::string> wanted;
        for (size_t k = m_order.size(); k-- > 0;)
        {
            const Item &item = m_items[m_order[k]];
            D2D1_RECT_F box = ItemBox(item);
            if (box.right < canvas.left || box.left > canvas.right || box.bottom < canvas.top || box.top > canvas.bottom)
                continue;
            wanted.push_back(item.f->get_path());
        }
        if (TrayShown())
        {
            for (size_t i = 0; i < m_tray.size(); i++)
            {
                RECT tile = TrayTileRect(i);
                if (tile.right > m_bounds.left && tile.left < m_bounds.right)
                    wanted.push_back(m_tray[i]->get_path());
            }
        }
        m_thumbs.Want(wanted);
    }
    else if (TrayShown())
    {
        std::vector<std::string> wanted;
        for (size_t i = 0; i < m_tray.size(); i++)
        {
            RECT tile = TrayTileRect(i);
            if (tile.right > m_bounds.left && tile.left < m_bounds.right)
                wanted.push_back(m_tray[i]->get_path());
        }
        m_thumbs.Want(wanted);
    }

    if (!m_tabs.empty())
    {
        renderer.PushClip(canvas);
        for (size_t index : m_order)
        {
            const Item &item = m_items[index];
            D2D1_RECT_F box = ItemBox(item);
            if (box.right < canvas.left || box.left > canvas.right || box.bottom < canvas.top || box.top > canvas.bottom)
                continue;
            PaintItem(renderer, item, m_selected.count(item.id) > 0);
            float level = 1.0f;
            m_thumbs.Get(renderer, item.f->get_path(), level);
            fading = fading || level < 1.0f;
        }
        // The play mark of the file under the pointer, over its picture. Over a video in its tile it is an overlay
        // of the player instead (SyncPlayer): nothing drawn here shows over the video's window.
        for (int index : { m_markFade.Current(), m_markFade.Previous() })
        {
            if (index < 0 || static_cast<size_t>(index) >= m_items.size() || m_mode != Mode::kNone)
                continue;
            const Item &item = m_items[static_cast<size_t>(index)];
            D2D1_POINT_2F center;
            float radius;
            float level = m_markFade.Level(index);
            bool overlay = item.id == m_playId && !m_covered && m_player.GetStatus() == VideoPlayer::Status::kPlaying;
            if (level > 0.0f && !overlay && MarkBox(item, center, radius))
                PaintMark(renderer, center, radius, MarkPauses(item), level, m_onMark && index == m_markFade.Current());
        }
        // The handle of the one selected file.
        if (m_selected.size() == 1)
        {
            if (const Item *item = FindItem(*m_selected.begin()))
            {
                D2D1_RECT_F handle = ToRectF(HandleRect(*item));
                renderer.FillRoundedRect(handle, 2.0f * line, m_active ? theme::kYuri : theme::kText3);
                renderer.DrawRoundedRect(handle, 2.0f * line, theme::kBase, line);
            }
        }
        renderer.PopClip();
    }
    PaintEmpty(renderer, canvas);
    SyncPlayer(renderer);

    // The bar and the tray are chrome: the base layer with an edge towards the canvas.
    PaintBar(renderer);
    if (TrayShown())
        PaintTray(renderer);

    // A file dragged from the tray follows the pointer.
    if (m_mode == Mode::kTrayDrag)
    {
        float side = static_cast<float>(Scale(kTrayTile)) * 1.6f;
        for (file *f : m_tray)
        {
            if (!m_selected.count(f->get_db_id()))
                continue;
            D2D1_RECT_F box = D2D1::RectF(m_point.x - side / 2, m_point.y - side / 2, m_point.x + side / 2, m_point.y + side / 2);
            PaintPicture(renderer, f, box, true, 0.85f);
            break;
        }
    }

    // A frame timer only while a thumbnail fades in, so an idle window uses no CPU.
    if (fading != m_fading)
    {
        if (fading)
            SetTimer(m_hWnd, kThumbnailTimer, theme::kFrameMs, nullptr);
        else
            KillTimer(m_hWnd, kThumbnailTimer);
        m_fading = fading;
    }
}

// One picture drawn into 'box'; 'fit' keeps its proportions inside the box (tray, drag), else the box is its shape.
void BoardView::PaintPicture(Renderer &renderer, file *f, const D2D1_RECT_F &box, bool fit, float extra) const
{
    const float radius = static_cast<float>(Scale(theme::kControlRadius));
    float level = 1.0f;
    ID2D1Bitmap *bitmap = m_thumbs.Get(renderer, f->get_path(), level);
    bool faded = m_missing && m_missing->count(f->get_path());
    float opacity = level * extra * (faded ? 0.45f : 1.0f);
    if (!bitmap)
    {
        renderer.FillRoundedRect(box, radius, Faded(theme::kControl, extra));
        std::string path = f->get_path();
        size_t dot = path.find_last_of('.');
        size_t slash = path.find_last_of("\\/");
        std::wstring ext = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? Utf8ToWide(path.substr(dot + 1)) : L"";
        for (wchar_t &ch : ext)
            ch = static_cast<wchar_t>(towupper(ch));
        RECT text = { static_cast<LONG>(box.left), static_cast<LONG>(box.top), static_cast<LONG>(box.right), static_cast<LONG>(box.bottom) };
        renderer.DrawTextLine(ext, Font::kMono, text, Faded(theme::kText3, extra), DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    D2D1_SIZE_U pixels = bitmap->GetPixelSize();
    if (pixels.width > 0 && pixels.height > 0)
        m_aspect[f->get_db_id()] = static_cast<double>(pixels.width) / pixels.height;
    D2D1_RECT_F target = box;
    if (fit && pixels.width > 0 && pixels.height > 0)
    {
        float boxW = box.right - box.left, boxH = box.bottom - box.top;
        float s = std::min(boxW / pixels.width, boxH / pixels.height);
        float w = pixels.width * s, h = pixels.height * s;
        target = D2D1::RectF(box.left + (boxW - w) / 2, box.top + (boxH - h) / 2, box.left + (boxW + w) / 2, box.top + (boxH + h) / 2);
    }
    renderer.DrawBitmap(bitmap, target, radius, opacity);
}

void BoardView::PaintItem(Renderer &renderer, const Item &item, bool selected) const
{
    D2D1_RECT_F box = ItemBox(item);
    // A video paused on shows that frame, not its thumbnail.
    if (ID2D1Bitmap *frame = FrameOf(renderer, item))
    {
        D2D1_SIZE_U pixels = frame->GetPixelSize();
        m_aspect[item.id] = static_cast<double>(pixels.width) / pixels.height;
        bool faded = m_missing && m_missing->count(item.f->get_path());
        renderer.DrawBitmap(frame, box, static_cast<float>(Scale(theme::kControlRadius)), faded ? 0.45f : 1.0f);
    }
    else
        PaintPicture(renderer, item.f, box, false, 1.0f);
    if (selected)
    {
        float line = Unit();
        renderer.DrawRoundedRect(Inflate(box, line), static_cast<float>(Scale(theme::kControlRadius)) + line,
            m_active ? theme::kYuri : theme::kText3, 2.0f * line);
    }
}

void BoardView::PaintButton(Renderer &renderer, const RECT &rect, const wchar_t *label, float hover, bool primary) const
{
    const float radius = static_cast<float>(Scale(primary ? theme::kControlRadius : theme::kChipRadius));
    const float line = Unit();
    D2D1_RECT_F box = ToRectF(rect);
    if (primary)
        renderer.FillRoundedRect(box, radius, theme::kControl);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));
    if (primary)
        renderer.DrawRoundedRect(D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2),
            radius, theme::kStroke, line);
    renderer.DrawTextLine(label, primary ? Font::kBody : Font::kCaption, rect, primary ? theme::kText1 : Mix(theme::kText2, theme::kText1, hover),
        DWRITE_TEXT_ALIGNMENT_CENTER);
}

void BoardView::PaintBar(Renderer &renderer) const
{
    RECT bar = BarRect();
    float line = Unit();
    renderer.FillRect(ToRectF(bar), theme::kBase);
    renderer.FillRect(D2D1::RectF(static_cast<float>(bar.left), bar.bottom - line, static_cast<float>(bar.right),
        static_cast<float>(bar.bottom)), theme::kStroke);

    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    RECT area = TabsArea();
    renderer.PushClip(area);
    int current = -1;
    for (size_t i = 0; i < m_tabs.size(); i++)
    {
        if (m_board && m_tabs[i].id == m_board->get_db_id())
            current = static_cast<int>(i);
    }
    for (const TabBox &box : TabBoxes())
    {
        float hover = m_hover.Level(box.index);
        D2D1_RECT_F rect = ToRectF(box.rect);
        if (hover > 0.0f)
            renderer.FillRoundedRect(rect, radius, Faded(theme::kHover, hover));
        if (box.index == static_cast<int>(m_tabs.size()))
        {
            // The plus.
            float cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2, arm = 5.0f * line;
            D2D1_COLOR_F color = Mix(theme::kText3, theme::kText1, hover);
            renderer.DrawLine(D2D1::Point2F(cx - arm, cy), D2D1::Point2F(cx + arm, cy), color, 1.5f * line);
            renderer.DrawLine(D2D1::Point2F(cx, cy - arm), D2D1::Point2F(cx, cy + arm), color, 1.5f * line);
            continue;
        }
        bool shown = box.index == current;
        RECT text = { box.rect.left + Scale(kTabPadding), box.rect.top, box.rect.right - Scale(kTabPadding), box.rect.bottom };
        renderer.DrawTextLine(m_tabs[static_cast<size_t>(box.index)].name, shown ? Font::kCaptionStrong : Font::kCaption, text,
            shown ? theme::kText1 : Mix(theme::kText2, theme::kText1, hover));
        if (shown)
        {
            // Violet marks the board in view.
            renderer.FillRoundedRect(D2D1::RectF(rect.left + Scale(kTabPadding), rect.bottom - 2.0f * line,
                rect.right - Scale(kTabPadding), rect.bottom), line, m_active ? theme::kYuri : theme::kText3);
        }
    }
    renderer.PopClip();

    RECT exportRect = ExportRect();
    bool enabled = !m_exporting && !m_items.empty();
    float hover = enabled ? m_hover.Level(static_cast<int>(m_tabs.size()) + 1) : 0.0f;
    D2D1_RECT_F box = ToRectF(exportRect);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));
    renderer.DrawTextLine(m_exporting ? strings::kBoardExporting : strings::kBoardExport, Font::kCaption, exportRect,
        enabled ? Mix(theme::kText2, theme::kText1, hover) : theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void BoardView::PaintTray(Renderer &renderer) const
{
    RECT tray = TrayRect();
    float line = Unit();
    renderer.FillRect(ToRectF(tray), theme::kBase);
    renderer.FillRect(D2D1::RectF(static_cast<float>(tray.left), static_cast<float>(tray.top), static_cast<float>(tray.right),
        tray.top + line), theme::kStroke);

    // The title and the count; "Place all" at the other end.
    RECT title = { tray.left + Scale(kBarMargin), tray.top, tray.right, tray.top + Scale(kTrayHeader) };
    renderer.DrawTextLine(strings::kBoardTray, Font::kCaptionStrong, title, theme::kText3);
    int titleWidth = static_cast<int>(renderer.TextWidth(strings::kBoardTray, Font::kCaptionStrong) + 0.99f);
    RECT count = { title.left + titleWidth + Scale(8), title.top, tray.right, title.bottom };
    renderer.DrawTextLine(FormatCount(m_tray.size()), Font::kMono, count, theme::kText3);
    PaintButton(renderer, PlaceAllRect(), strings::kBoardPlaceAll, m_hover.Level(static_cast<int>(m_tabs.size()) + 2), false);

    RECT area = { tray.left, tray.top + Scale(kTrayHeader), tray.right, tray.bottom };
    renderer.PushClip(area);
    const float radius = static_cast<float>(Scale(theme::kControlRadius));
    for (size_t i = 0; i < m_tray.size(); i++)
    {
        RECT tile = TrayTileRect(i);
        if (tile.right < tray.left || tile.left > tray.right)
            continue;
        D2D1_RECT_F plate = ToRectF(tile);
        bool selected = m_selected.count(m_tray[i]->get_db_id()) > 0;
        renderer.FillRoundedRect(plate, radius, selected ? theme::kPlate : theme::kControl);
        if (static_cast<int>(i) == m_hoverTray && !selected)
            renderer.FillRoundedRect(plate, radius, theme::kHover);
        float inset = static_cast<float>(Scale(kTrayInset));
        PaintPicture(renderer, m_tray[i], D2D1::RectF(plate.left + inset, plate.top + inset, plate.right - inset, plate.bottom - inset), true,
            m_mode == Mode::kTrayDrag && selected ? 0.4f : 1.0f);
        if (selected)
            renderer.DrawRoundedRect(Inflate(plate, -line), radius, m_active ? theme::kYuri : theme::kText3, 2.0f * line);
    }
    renderer.PopClip();
}

void BoardView::PaintEmpty(Renderer &renderer, const RECT &canvas) const
{
    int middle = (canvas.top + canvas.bottom) / 2;
    float lineHeight = renderer.LineHeight(Font::kBodyStrong);
    if (m_tabs.empty())
    {
        RECT title = { canvas.left, middle - static_cast<int>(lineHeight) - Scale(8), canvas.right, middle - Scale(8) };
        renderer.DrawTextLine(strings::kBoardEmptyTitle, Font::kBodyStrong, title, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
        RECT hint = { canvas.left + Scale(24), middle - Scale(4), canvas.right - Scale(24), middle + Scale(20) };
        renderer.DrawTextLine(strings::kBoardEmptyHint, Font::kCaption, hint, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
        RECT button = EmptyButtonRect();
        PaintButton(renderer, button, strings::kBoardEmptyButton, m_hover.Level(0), true);
        return;
    }
    if (m_matches.empty())
    {
        RECT text = { canvas.left, middle - Scale(16), canvas.right, middle + Scale(16) };
        renderer.DrawTextLine(strings::kBoardNoMatches, Font::kBody, text, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    else if (m_items.empty() && !m_tray.empty())
    {
        RECT text = { canvas.left, middle - Scale(16), canvas.right, middle + Scale(16) };
        renderer.DrawTextLine(strings::kBoardDropHint, Font::kBody, text, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// ---- Videos and songs ----

bool BoardView::IsVideo(const file *f)
{
    return f && KindOf(f->get_path()) == FileKind::kVideo && IsPlayableVideo(f->get_path());
}

bool BoardView::IsSong(const file *f)
{
    return f && IsPlayableAudio(f->get_path());
}

bool BoardView::MarkBox(const Item &item, D2D1_POINT_2F &center, float &radius) const
{
    if (!IsVideo(item.f) && !IsSong(item.f))
        return false;
    if (m_missing && m_missing->count(item.f->get_path()))
        return false;
    D2D1_RECT_F box = ItemBox(item);
    float side = std::min(box.right - box.left, box.bottom - box.top);
    if (side < Scale(kMarkRoom))
        return false;
    float side2 = std::max(static_cast<float>(Scale(kMarkMin)), std::min(static_cast<float>(Scale(kMarkMax)), side * 0.3f));
    center = D2D1::Point2F((box.left + box.right) / 2, (box.top + box.bottom) / 2);
    radius = side2 / 2;
    return true;
}

bool BoardView::OnMark(const Item &item, POINT pt) const
{
    D2D1_POINT_2F center;
    float radius;
    return MarkBox(item, center, radius) && std::fabs(pt.x - center.x) <= radius && std::fabs(pt.y - center.y) <= radius;
}

bool BoardView::MarkPauses(const Item &item) const
{
    if (item.id == m_playId)
        return VideoPlaying() && !m_player.Ended();
    return IsSong(item.f) && m_songPlaying && item.f->get_path() == m_songPath;
}

// The viewer's overlay button: a dark square with chip corners and a play triangle or two pause bars, washed lighter
// with the pointer on it. 'radius' is half its side.
void BoardView::PaintMark(Renderer &renderer, D2D1_POINT_2F center, float radius, bool pause, float level, bool hot) const
{
    D2D1_RECT_F square = D2D1::RectF(center.x - radius, center.y - radius, center.x + radius, center.y + radius);
    float corner = static_cast<float>(Scale(theme::kChipRadius));
    renderer.FillRoundedRect(square, corner, Faded(theme::kOverlay, level));
    if (hot)
        renderer.FillRoundedRect(square, corner, Faded(theme::kOverlayHover, level));
    D2D1_COLOR_F glyph = Faded(hot ? theme::kText1 : Mix(theme::kText2, theme::kText1, 0.5f), level);
    if (pause)
    {
        float h = radius * 0.4f, w = radius * 0.16f, gap = radius * 0.14f;
        renderer.FillRect(D2D1::RectF(center.x - gap - w, center.y - h, center.x - gap, center.y + h), glyph);
        renderer.FillRect(D2D1::RectF(center.x + gap, center.y - h, center.x + gap + w, center.y + h), glyph);
    }
    else
    {
        // A little right of the centre, where a triangle looks centred.
        float h = radius * 0.4f, w = h * 1.7f, cx = center.x + radius * 0.08f;
        const D2D1_POINT_2F tri[] = { { cx - w / 2, center.y - h }, { cx + w / 2, center.y }, { cx - w / 2, center.y + h } };
        renderer.FillPolygon(tri, 3, glyph);
    }
}

void BoardView::PlayVideo(file *f, int volume, const std::string &device)
{
    if (!f || !FindItem(f->get_db_id()))
        return;
    std::int64_t id = f->get_db_id();
    if (id == m_playId)
    {
        SetVideoPaused(VideoPlaying());
        Invalidate();
        return;
    }
    if (m_playId)
        StopVideo();
    m_player.SetEventTag(kBoardVideoTag);
    // Shared output only: the board's player never takes the device from the music.
    m_player.SetBitPerfect(false);
    m_player.SetOutputDevices("", device);
    // Setting the volume unmutes; M's choice holds from one video to the next.
    bool muted = m_player.Muted();
    m_player.SetVolume(volume);
    if (m_player.Muted() != muted)
        m_player.ToggleMute();
    m_playId = id;
    m_pauseWhenReady = m_audioReleased = false;
    m_shapeKnown = false;
    m_shownMarkState = -1;
    const Item *item = FindItem(id);
    D2D1_RECT_F b = ItemBox(*item);
    RECT box = { static_cast<LONG>(std::lround(b.left)), static_cast<LONG>(std::lround(b.top)), static_cast<LONG>(std::lround(b.right)),
        static_cast<LONG>(std::lround(b.bottom)) };
    // A video paused on before carries on from there.
    double start = item->frame > 0.0 ? item->frame : 0.0;
    if (!m_player.Open(m_hWnd, box, f->get_path(), false, start) || m_player.GetStatus() == VideoPlayer::Status::kMissing)
    {
        m_videoFailed = true;
        StopVideo();
        return;
    }
    // Hidden until SyncPlayer has cut it to shape.
    m_player.SetCovered(true);
    m_shownHidden = true;
    Invalidate();
}

void BoardView::PauseVideo()
{
    if (VideoPlaying())
        SetVideoPaused(true);
    Invalidate();
}

void BoardView::SetVideoPaused(bool paused)
{
    if (!m_playId)
        return;
    // The player takes no pause before the file is up: it is remembered and applied then (OnVideoEvent).
    if (m_player.GetStatus() != VideoPlayer::Status::kPlaying)
    {
        m_pauseWhenReady = paused;
        return;
    }
    m_pauseWhenReady = false;
    bool was = m_player.Paused() || m_player.Ended();
    if (paused != was)
        m_player.TogglePause();
    if (paused && !was)
        KeepFrame();
    if (paused != m_audioReleased)
    {
        m_player.SetAudioReleased(paused);
        m_audioReleased = paused;
    }
}

void BoardView::StopVideo()
{
    if (!m_playId)
        return;
    m_playId = 0;
    m_pauseWhenReady = m_audioReleased = false;
    m_player.Stop();
    m_player.SetShape(nullptr);
    m_shapeKnown = false;
    m_shownMarkState = -1;
    Invalidate();
}

bool BoardView::ToggleVideoMute(bool &muted)
{
    if (!m_playId || !m_selected.count(m_playId))
        return false;
    m_player.ToggleMute();
    muted = m_player.Muted();
    return true;
}

bool BoardView::VideoPlaying() const
{
    if (!m_playId)
        return false;
    VideoPlayer::Status status = m_player.GetStatus();
    return (status == VideoPlayer::Status::kLoading || status == VideoPlayer::Status::kPlaying) && !m_pauseWhenReady &&
        !m_player.Paused() && !m_player.Ended();
}

void BoardView::OnVideoEvent()
{
    bool changed = m_player.OnEvent();
    if (!m_playId)
        return;
    VideoPlayer::Status status = m_player.GetStatus();
    if (status == VideoPlayer::Status::kFailed || status == VideoPlayer::Status::kMissing)
    {
        m_videoFailed = true;
        StopVideo();
        return;
    }
    if (status == VideoPlayer::Status::kPlaying)
    {
        if (m_pauseWhenReady)
            SetVideoPaused(true);
        // Stopped on its last frame: the sound device is let go as for a pause.
        else if (m_player.Ended() && !m_audioReleased)
        {
            m_player.SetAudioReleased(true);
            m_audioReleased = true;
        }
    }
    if (changed)
    {
        // The player shows its window once the video is up: keep it to the shape it was given.
        m_shapeKnown = false;
        Invalidate();
    }
}

void BoardView::SetPlayerCovered(bool covered)
{
    if (covered == m_covered)
        return;
    m_covered = covered;
    m_shapeKnown = false;
    // At once: the board may not be painted again while it is covered (the viewer paints instead).
    if (covered && m_playId)
    {
        m_player.SetCovered(true);
        m_shownHidden = true;
    }
    Invalidate();
}

void BoardView::SetSong(const std::string &path, bool playing)
{
    if (path == m_songPath && playing == m_songPlaying)
        return;
    m_songPath = path;
    m_songPlaying = playing;
    if (m_markFade.Current() >= 0 || m_markFade.Previous() >= 0)
        Invalidate();
}

file *BoardView::SelectedVideo() const
{
    if (m_selected.size() != 1)
        return nullptr;
    const Item *item = FindItem(*m_selected.begin());
    return item && IsVideo(item->f) ? item->f : nullptr;
}

void BoardView::SyncPlayer(Renderer &renderer) const
{
    if (!m_playId)
        return;
    const Item *item = FindItem(m_playId);
    if (!item)
        return;
    auto round = [](const D2D1_RECT_F &r)
    {
        return RECT{ static_cast<LONG>(std::lround(r.left)), static_cast<LONG>(std::lround(r.top)), static_cast<LONG>(std::lround(r.right)),
            static_cast<LONG>(std::lround(r.bottom)) };
    };
    RECT box = round(ItemBox(*item));
    RECT canvas = CanvasRect();
    RECT visible;
    bool hidden = m_covered || m_tabs.empty() || !IntersectRect(&visible, &box, &canvas) ||
        m_player.GetStatus() != VideoPlayer::Status::kPlaying;

    // What lies over the video: the files above it, the selection bar, a file dragged from the tray.
    std::vector<RECT> cuts;
    bool above = false;
    for (size_t index : m_order)
    {
        if (above)
        {
            RECT other = round(Inflate(ItemBox(m_items[index]), Unit() * 3.0f));  // and its selection ring
            RECT overlap;
            if (IntersectRect(&overlap, &other, &box))
                cuts.push_back(overlap);
        }
        above = above || m_items[index].id == m_playId;
    }
    // The resize handle of the one selected file, this one's own too.
    if (m_selected.size() == 1)
    {
        if (const Item *selected = FindItem(*m_selected.begin()))
            cuts.push_back(HandleRect(*selected));
    }
    if (!IsRectEmpty(&m_obstacle))
        cuts.push_back(m_obstacle);
    if (m_mode == Mode::kTrayDrag)
    {
        LONG half = static_cast<LONG>(Scale(kTrayTile) * 0.8f) + 1;
        cuts.push_back({ m_point.x - half, m_point.y - half, m_point.x + half, m_point.y + half });
    }

    bool same = m_shapeKnown && EqualRect(&box, &m_shownBox) && hidden == m_shownHidden && cuts.size() == m_shownCuts.size();
    for (size_t i = 0; same && i < cuts.size(); i++)
        same = EqualRect(&cuts[i], &m_shownCuts[i]) != 0;
    if (!same)
    {
        if (!hidden)
        {
            int w = box.right - box.left, h = box.bottom - box.top;
            int r = Scale(theme::kControlRadius) * 2;
            HRGN shape = CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r);
            HRGN part = CreateRectRgn(visible.left - box.left, visible.top - box.top, visible.right - box.left, visible.bottom - box.top);
            CombineRgn(shape, shape, part, RGN_AND);
            for (const RECT &cut : cuts)
            {
                SetRectRgn(part, cut.left - box.left, cut.top - box.top, cut.right - box.left, cut.bottom - box.top);
                CombineRgn(shape, shape, part, RGN_DIFF);
            }
            DeleteObject(part);
            m_player.SetBounds(box);
            m_player.SetShape(shape);
        }
        m_player.SetCovered(hidden);
        m_shownBox = box;
        m_shownCuts = cuts;
        m_shownHidden = hidden;
        m_shapeKnown = true;
        m_shownMarkState = -1;
    }

    // The mark over the video, drawn into memory and handed to libmpv.
    int index = static_cast<int>(item - m_items.data());
    float level = m_mode == Mode::kNone ? m_markFade.Level(index) : 0.0f;
    D2D1_POINT_2F center;
    float radius;
    if (hidden || level <= 0.0f || !MarkBox(*item, center, radius))
    {
        if (m_shownMarkState != 0)
            m_player.HideOverlay(kMarkOverlay);
        m_shownMarkState = 0;
        return;
    }
    bool hot = m_onMark && index == m_markFade.Current();
    bool pause = MarkPauses(*item);
    RECT mark = { static_cast<LONG>(std::floor(center.x - radius)) - 1, static_cast<LONG>(std::floor(center.y - radius)) - 1,
        static_cast<LONG>(std::ceil(center.x + radius)) + 1, static_cast<LONG>(std::ceil(center.y + radius)) + 1 };
    int state = 1 + static_cast<int>(level * 100.0f) * 4 + (hot ? 2 : 0) + (pause ? 1 : 0);
    if (state == m_shownMarkState && EqualRect(&mark, &m_shownMark))
        return;
    std::vector<BYTE> pixels;
    if (renderer.BeginLayer(mark))
    {
        PaintMark(renderer, center, radius, pause, level, hot);
        if (renderer.EndLayer(pixels))
            m_player.ShowOverlay(kMarkOverlay, mark.left - box.left, mark.top - box.top, pixels, static_cast<UINT>(mark.right - mark.left),
                static_cast<UINT>(mark.bottom - mark.top));
    }
    m_shownMark = mark;
    m_shownMarkState = state;
}

void BoardView::KeepFrame()
{
    Item *item = FindItem(m_playId);
    if (!item || !m_lib || !m_board)
        return;
    const int kFrameSide = 3840;  // the saved picture's longest side, at most: a 4K frame stays as it is
    std::vector<BYTE> pixels;
    UINT width = 0, height = 0;
    if (!m_player.ReadFrame(kFrameSide, pixels, width, height))
        return;
    // After the picture: the pause was only asked for, and the time read last matches the frame taken.
    m_player.ReadPosition();
    double seconds = m_player.Position();
    std::wstring target = BoardFramePath(m_thumbFolder, item->id, seconds);
    if (!SaveBoardFrame(target, pixels, width, height))
        return;
    item->frame = seconds;
    Commit({ item->id });
    m_frames.erase(item->id);
    // The picture of the frame before goes, unless another board still shows it.
    SweepFrames();
}

void BoardView::SweepFrames()
{
    if (m_sweepFrames && m_lib)
        SweepBoardFrames(m_thumbFolder, m_lib->get_boards());
}

ID2D1Bitmap *BoardView::FrameOf(Renderer &renderer, const Item &item) const
{
    if (item.frame < 0.0 || !IsVideo(item.f))
        return nullptr;
    FrameBitmap &cached = m_frames[item.id];
    if (cached.frame != item.frame || cached.generation != renderer.Generation())
    {
        cached.frame = item.frame;
        cached.generation = renderer.Generation();
        cached.bitmap.Reset();
        std::vector<BYTE> pixels;
        UINT width = 0, height = 0;
        if (LoadBoardFrame(BoardFramePath(m_thumbFolder, item.id, item.frame), pixels, width, height))
            cached.bitmap = renderer.CreateBitmap(width, height, pixels.data());
    }
    return cached.bitmap.Get();
}
