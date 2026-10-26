#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "file_grid.hpp"
#include "file_kind.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>

namespace
{
    // A tile: the plate (the area that greys out when selected), with the icon or thumbnail inside it, and the name
    // below.
    // The plate's size is the user's tile size (FileGrid::TileSize); the cell is the plate plus these margins.
    const int kPlateInset = 4;        // from the top and sides of the cell
    const int kCellExtraHeight = 48;  // under the plate: the gap and two lines of name
    const int kIconInset = 4;         // the icon or thumbnail inside the plate
    const int kNameGap = 4;           // between the plate and the name
    const int kNamePadding = 4;       // around the name, inside its highlight
    const int kGridTop = 12;          // above the first row
    const int kTypeIconSize = 36;     // the line drawing in the middle of the icon

    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 4;  // from the right and bottom edges of the grid
    const int kScrollbarMinThumb = 24;
    const int kScrollbarHitSlop = 6;  // the bar is thin, so clicks a bit to its left still count

    const int kEmptyInset = 24;       // the drop area of an empty library, from the edges of the grid
    const int kButtonGap = 16;        // between the text of an empty grid and its button
    const int kButtonPadding = 14;    // on each side of the button's label
    const int kFolderIconSize = 40;   // above the text of an empty library
    const float kFolderTop = 0.77f;   // how far the folder reaches above the title, in icon sizes (0.45 + 0.32)

    const int kBannerHeight = 52;     // the banner over the tiles, its margins included
    const int kBannerMargin = 8;      // above it and on its sides, inside the page margin
    const int kBannerPadding = 12;    // inside it
    const int kBadgeSize = 14;        // the caution badge on a missing file's icon

    enum class Kind { kOther, kImage, kVideo, kAudio, kDocument, kArchive, kCount };

    std::string FileName(const std::string &path)
    {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos)
            return path;
        return path.substr(slash + 1);
    }

    // The icon of a file: books share the document's page.
    Kind IconKind(FileKind kind)
    {
        switch (kind)
        {
        case FileKind::kPicture:  return Kind::kImage;
        case FileKind::kVideo:    return Kind::kVideo;
        case FileKind::kAudio:    return Kind::kAudio;
        case FileKind::kDocument:
        case FileKind::kBook:     return Kind::kDocument;
        case FileKind::kArchive:  return Kind::kArchive;
        case FileKind::kOther:    break;
        }
        return Kind::kOther;
    }

    // Builds the icon of a kind of file, a line drawing in a square of side 's' centered on the origin.
    void BuildTypeIcon(PathBuilder &lines, PathBuilder &fills, Kind kind, float s, float line)
    {
        auto P = [&](float x, float y) { return D2D1::Point2F(x * s, y * s); };

        switch (kind)
        {
        case Kind::kImage:
        {
            lines.RoundedRect(D2D1::RectF(-0.5f * s, -0.38f * s, 0.5f * s, 0.38f * s), line * 1.5f);
            const D2D1_POINT_2F hills[] = { P(-0.38f, 0.26f), P(-0.12f, -0.02f), P(0.06f, 0.16f), P(0.18f, 0.06f), P(0.38f, 0.26f) };
            lines.Polyline(hills, 5);
            lines.Circle(P(0.22f, -0.16f), 0.07f * s);
            break;
        }
        case Kind::kVideo:
        {
            lines.RoundedRect(D2D1::RectF(-0.5f * s, -0.34f * s, 0.5f * s, 0.34f * s), line * 1.5f);
            const D2D1_POINT_2F play[] = { P(-0.12f, -0.16f), P(0.18f, 0.0f), P(-0.12f, 0.16f) };
            fills.Polyline(play, 3, true);
            break;
        }
        case Kind::kAudio:
        {
            // A pair of eighth notes joined by a beam.
            lines.Line(P(-0.18f, 0.24f), P(-0.18f, -0.34f));
            lines.Line(P(0.3f, 0.16f), P(0.3f, -0.42f));
            const D2D1_POINT_2F beam[] = { P(-0.18f, -0.34f), P(0.3f, -0.42f), P(0.3f, -0.3f), P(-0.18f, -0.22f) };
            fills.Polyline(beam, 4, true);
            fills.Circle(P(-0.29f, 0.26f), 0.12f * s);
            fills.Circle(P(0.19f, 0.18f), 0.12f * s);
            break;
        }
        default:
        {
            // A page with a folded corner.
            const float fold = 0.22f;
            const D2D1_POINT_2F page[] = { P(-0.36f, -0.48f), P(0.36f - fold, -0.48f), P(0.36f, -0.48f + fold), P(0.36f, 0.48f), P(-0.36f, 0.48f) };
            lines.Polyline(page, 5, true);
            const D2D1_POINT_2F corner[] = { P(0.36f - fold, -0.48f), P(0.36f - fold, -0.48f + fold), P(0.36f, -0.48f + fold) };
            lines.Polyline(corner, 3);

            if (kind == Kind::kDocument)
            {
                for (float y : { -0.04f, 0.12f, 0.28f })
                    lines.Line(P(-0.2f, y), P(0.2f, y));
            }
            else if (kind == Kind::kArchive)
            {
                // A zipper down the middle.
                for (int i = 0; i < 4; i++)
                {
                    float y = -0.42f + i * 0.14f;
                    float x = (i % 2) ? 0.0f : -0.08f;
                    fills.Rect(D2D1::RectF(x * s, y * s, (x + 0.08f) * s, (y + 0.07f) * s));
                }
                lines.RoundedRect(D2D1::RectF(-0.1f * s, 0.14f * s, 0.1f * s, 0.34f * s), line);
            }
            break;
        }
        }
    }
}

static_assert(static_cast<int>(Kind::kCount) == 6, "FileGrid::kKindCount is the number of kinds");

int FileGrid::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

void FileGrid::Invalidate() const
{
    if (m_hWnd)
        InvalidateRect(m_hWnd, &m_bounds, FALSE);
}

void FileGrid::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    // A grid that gets more or fewer columns lays its tiles out again (the info pane opened, the sidebar slid, the
    // window was resized): the tiles the user was looking at stay in view.
    bool atTop = false;
    int anchor = m_hWnd ? ReflowAnchor(atTop) : -1;
    int columns = m_hWnd ? ComputeLayout().columns : 0;
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    KeepInView(ComputeLayout().columns != columns ? anchor : -1, atTop);
}

// The tile to keep in view when the tiles are laid out again: the focused file, or the first selected one, if it is
// on screen; else the first tile on screen ('atTop'), which stays at the top. -1 for none.
int FileGrid::ReflowAnchor(bool &atTop) const
{
    if (m_files.empty())
        return -1;
    Layout layout = ComputeLayout();
    auto onScreen = [&](int index)
    {
        int top = Scale(kGridTop) + (index / layout.columns) * layout.cellHeight;
        return index >= 0 && top + layout.cellHeight > m_scrollY && top < m_scrollY + layout.viewportHeight;
    };
    int selected = IndexOf(m_focusItem);
    if (selected < 0 || !IsSelected(m_focusItem))
    {
        selected = -1;
        for (int i = 0; i < static_cast<int>(m_files.size()); i++)
        {
            if (IsSelected(m_files[i]) && onScreen(i))
            {
                selected = i;
                break;
            }
        }
    }
    if (onScreen(selected))
    {
        atTop = false;
        return selected;
    }
    atTop = true;
    int row = std::max(0, m_scrollY - Scale(kGridTop) + layout.cellHeight / 2) / layout.cellHeight;
    return std::min(row * layout.columns, static_cast<int>(m_files.size()) - 1);
}

void FileGrid::KeepInView(int anchor, bool atTop)
{
    if (anchor < 0)
    {
        ScrollTo(m_scrollY);
        return;
    }
    Layout layout = ComputeLayout();
    if (atTop)
        ScrollTo((anchor / layout.columns) * layout.cellHeight);
    else
        EnsureVisible(layout, anchor);
    UpdateHover();
    Invalidate();
}

void FileGrid::SetFiles(const std::vector<file*> &files)
{
    if (files == m_files)
        return;

    // The tiles fading in or out keep their fade if their file is still shown, under its new index.
    auto fileAt = [this](int index) { return index >= 0 && index < static_cast<int>(m_files.size()) ? m_files[index] : nullptr; };
    const file *hovered = fileAt(m_hover.Current());
    const file *left = fileAt(m_hover.Previous());

    m_files = files;
    m_scrollY = 0;

    // Files filtered out of view leave the selection, so later actions only touch what's on screen.
    std::unordered_set<const file*> visible(m_files.begin(), m_files.end());
    for (auto it = m_selection.begin(); it != m_selection.end(); )
    {
        if (visible.count(*it))
            ++it;
        else
            it = m_selection.erase(it);
    }
    if (m_selectionAnchor && !visible.count(m_selectionAnchor))
        m_selectionAnchor = nullptr;
    if (m_focusItem && !visible.count(m_focusItem))
        m_focusItem = nullptr;
    // Only the names of the files on display are kept, so the cache can't grow past the library.
    for (auto it = m_names.begin(); it != m_names.end(); )
    {
        if (visible.count(it->first))
            ++it;
        else
            it = m_names.erase(it);
    }

    int newHovered = -1;
    int newLeft = -1;
    for (int i = 0; i < static_cast<int>(m_files.size()); ++i)
    {
        if (m_files[i] == hovered)
            newHovered = i;
        else if (m_files[i] == left)
            newLeft = i;
    }
    m_hover.Renumber(m_hWnd, kGridHoverTimer, newHovered, newLeft);
    UpdateHover();
    Invalidate();
}

void FileGrid::Clear()
{
    m_files.clear();
    m_names.clear();
    m_selection.clear();
    m_selectionAnchor = nullptr;
    m_focusItem = nullptr;
    m_scrollY = 0;
    m_hover.Reset(m_hWnd, kGridHoverTimer);
    Invalidate();
}

void FileGrid::SetEmpty(Empty empty)
{
    if (empty == m_empty)
        return;
    m_empty = empty;
    if (m_files.empty())
    {
        UpdateHover();  // the other button has another width
        Invalidate();
    }
}

void FileGrid::SetTileSize(int size)
{
    size = std::max(kMinTile, std::min(size, kMaxTile));
    if (size == m_tileSize)
        return;

    // The tiles the user was looking at stay in view, as when the grid changes width.
    bool atTop = false;
    int anchor = ReflowAnchor(atTop);
    m_tileSize = size;
    // The names wrap and the extension labels center to the tile's width.
    m_names.clear();
    m_extensions.clear();
    KeepInView(anchor, atTop);
}

void FileGrid::SetBanner(const std::wstring &text, const wchar_t *button)
{
    if (text == m_bannerText && button == m_bannerButton)
        return;
    bool atTop = false;
    int anchor = ReflowAnchor(atTop);
    m_bannerText = text;
    m_bannerButton = button;
    KeepInView(anchor, atTop);
}

void FileGrid::SetMissing(const std::unordered_set<std::string> *paths)
{
    m_missing = paths;
    Invalidate();
}

void FileGrid::SetDropTargets(const std::vector<file*> &files)
{
    std::unordered_set<const file*> targets(files.begin(), files.end());
    if (targets == m_dropTargets)
        return;
    m_dropTargets = std::move(targets);
    Invalidate();
}

void FileGrid::SetActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    // The files may have been edited in another program meanwhile; the next paint checks those on screen.
    if (active)
        m_thumbnails.Revalidate();
    if (active || !m_selection.empty())
        Invalidate();
}

void FileGrid::OnThumbnailsReady()
{
    if (m_thumbnails.TakeFinished())
        Invalidate();
}

// ---- Layout ----

// The room of the tiles: the grid under the banner, if one shows.
RECT FileGrid::GridArea() const
{
    RECT area = m_bounds;
    if (BannerShown())
        area.top = std::min(area.bottom, area.top + Scale(kBannerHeight));
    return area;
}

FileGrid::Layout FileGrid::ComputeLayout() const
{
    const int margin = Scale(theme::kPageMargin);
    const RECT area = GridArea();

    Layout layout;
    layout.cellWidth = Scale(m_tileSize + 2 * kPlateInset);
    layout.cellHeight = Scale(m_tileSize + kCellExtraHeight);

    int usableWidth = (area.right - area.left) - 2 * margin;
    layout.columns = usableWidth / layout.cellWidth;
    if (layout.columns < 1)
        layout.columns = 1;

    layout.left = area.left + margin + (usableWidth - layout.columns * layout.cellWidth) / 2;
    layout.top = area.top + Scale(kGridTop);

    int count = static_cast<int>(m_files.size());
    int rows = (count + layout.columns - 1) / layout.columns;

    layout.viewportHeight = area.bottom - area.top;
    if (layout.viewportHeight < 0)
        layout.viewportHeight = 0;
    layout.contentHeight = Scale(kGridTop) + rows * layout.cellHeight;

    return layout;
}

RECT FileGrid::CellRect(const Layout &layout, int index) const
{
    int col = index % layout.columns;
    int row = index / layout.columns;

    int x = layout.left + col * layout.cellWidth;
    int y = layout.top + row * layout.cellHeight - m_scrollY;

    RECT r = { x, y, x + layout.cellWidth, y + layout.cellHeight };
    return r;
}

// The part of a cell that reacts to the pointer: the plate and the name under it.
RECT FileGrid::ItemRect(const Layout &layout, int index) const
{
    RECT r = CellRect(layout, index);
    const int inset = Scale(kPlateInset);
    InflateRect(&r, -inset, -inset);
    return r;
}

int FileGrid::HitTest(const Layout &layout, POINT pt) const
{
    RECT area = GridArea();
    if (pt.y < area.top || pt.y >= area.bottom || pt.x < layout.left)
        return -1;

    int col = (pt.x - layout.left) / layout.cellWidth;
    int y = pt.y - layout.top + m_scrollY;
    if (y < 0 || col >= layout.columns)
        return -1;

    int index = (y / layout.cellHeight) * layout.columns + col;
    if (index >= static_cast<int>(m_files.size()))
        return -1;

    RECT item = ItemRect(layout, index);
    return PtInRect(&item, pt) ? index : -1;
}

file *FileGrid::FileAt(POINT pt) const
{
    RECT area = GridArea();
    if (!PtInRect(&area, pt))
        return nullptr;

    int index = HitTest(ComputeLayout(), pt);
    return index >= 0 ? m_files[index] : nullptr;
}

FileGrid::Button FileGrid::ButtonAt(POINT pt) const
{
    RECT area, button;
    int middle;
    if (!m_files.empty())
    {
        button = BannerButtonRect();
        return PtInRect(&button, pt) ? Button::kBanner : Button::kNone;
    }
    if (m_empty == Empty::kChecking || !EmptyLayout(area, middle, button) || !PtInRect(&button, pt))
        return Button::kNone;
    return m_empty == Empty::kLibrary ? Button::kAddFolder : Button::kClearFilters;
}

void FileGrid::OnHover(POINT pt)
{
    m_hoverPoint = pt;
    UpdateHover();
}

// Finds the tile under the pointer again: the pointer moved, or the tiles moved under it.
void FileGrid::UpdateHover()
{
    int index = -1;
    RECT area = GridArea();
    if (m_hWnd && !m_draggingThumb && PtInRect(&area, m_hoverPoint))
        index = HitTest(ComputeLayout(), m_hoverPoint);

    if (m_hover.Set(m_hWnd, kGridHoverTimer, index))
        Invalidate();

    bool button = m_hWnd && ButtonAt(m_hoverPoint) != Button::kNone;
    if (m_buttonHover.Set(m_hWnd, kGridButtonTimer, button ? 0 : -1))
        Invalidate();
}

void FileGrid::OnTimer(UINT_PTR timer)
{
    if (timer == kGridHoverTimer)
        m_hover.Step(m_hWnd, timer);
    else if (timer == kGridButtonTimer)
        m_buttonHover.Step(m_hWnd, timer);
    // kThumbnailTimer: the paint moves the fades on, and stops the timer once they are done.
    Invalidate();
}

// ---- Scrolling ----

int FileGrid::MaxScroll(const Layout &layout) const
{
    int max = layout.contentHeight - layout.viewportHeight;
    return max > 0 ? max : 0;
}

void FileGrid::ScrollTo(int y)
{
    int maxScroll = MaxScroll(ComputeLayout());

    if (y > maxScroll)
        y = maxScroll;
    if (y < 0)
        y = 0;

    if (y != m_scrollY)
    {
        m_scrollY = y;
        UpdateHover();
        Invalidate();
    }
}

void FileGrid::EnsureVisible(const Layout &layout, int index)
{
    int rowTop = Scale(kGridTop) + (index / layout.columns) * layout.cellHeight;
    int rowBottom = rowTop + layout.cellHeight;

    if (rowTop < m_scrollY)
        ScrollTo(rowTop - Scale(kGridTop));
    else if (rowBottom > m_scrollY + layout.viewportHeight)
        ScrollTo(rowBottom - layout.viewportHeight);
}

void FileGrid::OnMouseWheel(int delta)
{
    // One wheel notch scrolls one row; touchpads send smaller deltas and scroll proportionally.
    ScrollTo(m_scrollY - MulDiv(delta, ComputeLayout().cellHeight, WHEEL_DELTA));
}

// ---- Scrollbar ----

bool FileGrid::GetScrollbarRects(const Layout &layout, RECT &track, RECT &thumb) const
{
    int maxScroll = MaxScroll(layout);
    if (maxScroll <= 0)
        return false;

    RECT area = GridArea();
    int right = area.right - Scale(kScrollbarEdgeGap);
    track = { right - Scale(kScrollbarWidth), area.top, right, area.bottom - Scale(kScrollbarEdgeGap) };
    int trackHeight = track.bottom - track.top;
    if (trackHeight <= 0)
        return false;

    int thumbHeight = MulDiv(trackHeight, layout.viewportHeight, layout.contentHeight);
    if (thumbHeight < Scale(kScrollbarMinThumb))
        thumbHeight = Scale(kScrollbarMinThumb);
    if (thumbHeight > trackHeight)
        thumbHeight = trackHeight;

    int thumbTop = track.top + MulDiv(trackHeight - thumbHeight, m_scrollY, maxScroll);
    thumb = { track.left, thumbTop, track.right, thumbTop + thumbHeight };
    return true;
}

bool FileGrid::HandleScrollbarClick(POINT pt)
{
    Layout layout = ComputeLayout();

    RECT track, thumb;
    if (!GetScrollbarRects(layout, track, thumb))
        return false;

    // Extends right up to the window's resize border.
    RECT hitArea = { track.left - Scale(kScrollbarHitSlop), track.top,
                     m_bounds.right - Scale(theme::kResizeBorder), track.bottom };
    if (!PtInRect(&hitArea, pt))
        return false;

    if (pt.y >= thumb.top && pt.y < thumb.bottom)
    {
        m_draggingThumb = true;
        m_thumbDragOffset = pt.y - thumb.top;
        SetCapture(m_hWnd);
        Invalidate();
    }
    else if (pt.y < thumb.top)
    {
        ScrollTo(m_scrollY - layout.viewportHeight);
    }
    else
    {
        ScrollTo(m_scrollY + layout.viewportHeight);
    }
    return true;
}

bool FileGrid::OnMouseMove(POINT pt)
{
    if (!m_draggingThumb)
        return false;

    Layout layout = ComputeLayout();

    RECT track, thumb;
    if (!GetScrollbarRects(layout, track, thumb))
        return true;

    int travel = (track.bottom - track.top) - (thumb.bottom - thumb.top);
    if (travel <= 0)
        return true;

    int thumbTop = pt.y - m_thumbDragOffset - track.top;
    ScrollTo(MulDiv(thumbTop, MaxScroll(layout), travel));
    return true;
}

void FileGrid::OnLButtonUp()
{
    if (m_draggingThumb)
        ReleaseCapture();  // OnCaptureChanged finishes the drag
}

void FileGrid::OnCaptureChanged()
{
    if (m_draggingThumb)
    {
        m_draggingThumb = false;
        Invalidate();
    }
}

// ---- Selection ----

bool FileGrid::OnLButtonDown(POINT pt, bool ctrl, bool shift)
{
    if (!PtInRect(&m_bounds, pt))
        return false;
    // The banner isn't the grid: a click on it leaves the selection alone.
    RECT area = GridArea();
    if (!PtInRect(&area, pt))
        return true;

    if (HandleScrollbarClick(pt))
        return true;

    HandleItemClick(HitTest(ComputeLayout(), pt), ctrl, shift);
    Invalidate();
    return true;
}

bool FileGrid::OnKeyDown(WPARAM key, bool ctrl, bool shift)
{
    Layout layout = ComputeLayout();

    switch (key)
    {
    case VK_PRIOR:
        ScrollTo(m_scrollY - layout.viewportHeight);
        return true;
    case VK_NEXT:
        ScrollTo(m_scrollY + layout.viewportHeight);
        return true;
    case VK_HOME:
        ScrollTo(0);
        return true;
    case VK_END:
        ScrollTo(MaxScroll(layout));
        return true;
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
        MoveFocus(key, shift);
        return true;
    case VK_ESCAPE:
        ClearSelection();
        Invalidate();
        return true;
    case 'A':
        if (!ctrl)
            return false;
        SelectAll();
        Invalidate();
        return true;
    }
    return false;
}

bool FileGrid::IsSelected(const file *f) const
{
    return m_selection.count(f) != 0;
}

std::vector<file*> FileGrid::SelectedFiles() const
{
    std::vector<file*> selected;
    for (file *f : m_files)
    {
        if (IsSelected(f))
            selected.push_back(f);
    }
    return selected;
}

int FileGrid::IndexOf(const file *f) const
{
    for (size_t i = 0; i < m_files.size(); i++)
    {
        if (m_files[i] == f)
            return static_cast<int>(i);
    }
    return -1;
}

void FileGrid::HandleItemClick(int index, bool ctrl, bool shift)
{
    if (index < 0)
    {
        if (!ctrl && !shift)
            ClearSelection();
        return;
    }

    const file *clicked = m_files[index];

    if (shift)
    {
        int anchorIndex = IndexOf(m_selectionAnchor);
        if (anchorIndex < 0)
        {
            anchorIndex = index;
            m_selectionAnchor = clicked;
        }

        if (!ctrl)
            m_selection.clear();
        SelectRange(anchorIndex, index);
    }
    else if (ctrl)
    {
        if (!m_selection.erase(clicked))
            m_selection.insert(clicked);
        m_selectionAnchor = clicked;
    }
    else
    {
        m_selection.clear();
        m_selection.insert(clicked);
        m_selectionAnchor = clicked;
    }

    m_focusItem = clicked;
}

void FileGrid::MoveFocus(WPARAM key, bool shift)
{
    int count = static_cast<int>(m_files.size());
    if (count == 0)
        return;

    Layout layout = ComputeLayout();

    int current = IndexOf(m_focusItem);
    int target = current;

    if (current < 0)
    {
        // Nothing focused yet: start from the first fully visible row.
        int scrolled = m_scrollY - Scale(kGridTop);
        int firstRow = scrolled > 0 ? (scrolled + layout.cellHeight - 1) / layout.cellHeight : 0;
        target = firstRow * layout.columns;
        if (target >= count)
            target = count - 1;
    }
    else
    {
        switch (key)
        {
        case VK_LEFT:
            if (current > 0)
                target = current - 1;
            break;
        case VK_RIGHT:
            if (current + 1 < count)
                target = current + 1;
            break;
        case VK_UP:
            if (current >= layout.columns)
                target = current - layout.columns;
            break;
        case VK_DOWN:
            if (current + layout.columns < count)
                target = current + layout.columns;
            else if (current / layout.columns < (count - 1) / layout.columns)
                target = count - 1;
            break;
        }
    }

    const file *f = m_files[target];

    if (shift)
    {
        int anchorIndex = IndexOf(m_selectionAnchor);
        if (anchorIndex < 0)
        {
            anchorIndex = current >= 0 ? current : target;
            m_selectionAnchor = m_files[anchorIndex];
        }
        m_selection.clear();
        SelectRange(anchorIndex, target);
    }
    else
    {
        m_selection.clear();
        m_selection.insert(f);
        m_selectionAnchor = f;
    }

    m_focusItem = f;
    EnsureVisible(layout, target);
    Invalidate();
}

file *FileGrid::FocusedFile() const
{
    int index = IndexOf(m_focusItem);
    return index >= 0 ? m_files[index] : nullptr;
}

bool FileGrid::MenuPoint(POINT &pt)
{
    int index = IndexOf(m_focusItem);
    for (int i = 0; index < 0 && i < static_cast<int>(m_files.size()); i++)
    {
        if (m_selection.count(m_files[i]))
            index = i;
    }
    if (index < 0)
        return false;
    Layout layout = ComputeLayout();
    EnsureVisible(layout, index);
    RECT r = ItemRect(layout, index);
    pt = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    return true;
}

void FileGrid::SelectPath(const std::string &path)
{
    for (int i = 0; i < static_cast<int>(m_files.size()); i++)
    {
        if (m_files[i]->get_path() != path)
            continue;
        m_selection.clear();
        m_selection.insert(m_files[i]);
        m_selectionAnchor = m_focusItem = m_files[i];
        EnsureVisible(ComputeLayout(), i);
        Invalidate();
        return;
    }
}

void FileGrid::FocusPath(const std::string &path)
{
    for (int i = 0; i < static_cast<int>(m_files.size()); i++)
    {
        if (m_files[i]->get_path() != path)
            continue;
        m_focusItem = m_files[i];
        EnsureVisible(ComputeLayout(), i);
        Invalidate();
        return;
    }
}

void FileGrid::SelectRange(int from, int to)
{
    if (from > to)
    {
        int tmp = from;
        from = to;
        to = tmp;
    }
    for (int i = from; i <= to; i++)
        m_selection.insert(m_files[i]);
}

void FileGrid::SelectAll()
{
    m_selection.clear();
    m_selection.insert(m_files.begin(), m_files.end());
}

void FileGrid::ClearSelection()
{
    m_selection.clear();
    m_selectionAnchor = nullptr;
    m_focusItem = nullptr;
}

// ---- Painting ----

void FileGrid::Paint(Renderer &renderer) const
{
    renderer.FillRect(ToRectF(m_bounds), theme::kLayer);
    // The edge between the search row and the grid.
    float line = Scale(4) / 4.0f;
    renderer.FillRect(D2D1::RectF(static_cast<float>(m_bounds.left), static_cast<float>(m_bounds.top),
        static_cast<float>(m_bounds.right), m_bounds.top + line), theme::kStroke);

    if (m_files.empty())
    {
        PaintEmpty(renderer);
        if (m_fading)
        {
            KillTimer(m_hWnd, kThumbnailTimer);
            m_fading = false;
        }
        return;
    }

    Layout layout = ComputeLayout();
    if (layout.viewportHeight <= 0)
        return;
    UpdateCacheDpi();

    PaintBanner(renderer);

    // Rows scrolled out of the grid must not paint over the banner, the search bar or the tag strip.
    renderer.PushClip(GridArea());

    // Only the rows inside the viewport are drawn.
    int count = static_cast<int>(m_files.size());
    int scrolled = m_scrollY - Scale(kGridTop);
    int firstRow = scrolled > 0 ? scrolled / layout.cellHeight : 0;
    int lastRow = (m_scrollY + layout.viewportHeight) / layout.cellHeight;
    int first = firstRow * layout.columns;
    int last = (lastRow + 1) * layout.columns;
    if (last > count)
        last = count;

    m_thumbnails.BeginPaint(renderer, static_cast<UINT>(Scale(m_tileSize) - 2 * Scale(kIconInset)));
    WantThumbnails(first, last);

    bool fading = false;
    for (int index = first; index < last; index++)
        fading |= PaintTile(renderer, layout, index);

    renderer.PopClip();

    // A frame timer only while a thumbnail fades in, so an idle window uses no CPU.
    if (fading != m_fading)
    {
        if (fading)
            SetTimer(m_hWnd, kThumbnailTimer, theme::kFrameMs, nullptr);
        else
            KillTimer(m_hWnd, kThumbnailTimer);
        m_fading = fading;
    }

    PaintScrollbar(renderer, layout);
}

// Asks for the thumbnails of the visible tiles [first, last), then for those of a screen below them and a screen
// above, so they are often ready before they scroll into view.
void FileGrid::WantThumbnails(int first, int last) const
{
    int count = static_cast<int>(m_files.size());
    int screen = last - first;
    std::vector<std::string> paths;
    paths.reserve(3 * screen);
    for (int i = first; i < last; i++)
        paths.push_back(m_files[i]->get_path());
    for (int i = last; i < last + screen && i < count; i++)
        paths.push_back(m_files[i]->get_path());
    for (int i = first - 1; i >= first - screen && i >= 0; i--)
        paths.push_back(m_files[i]->get_path());
    m_thumbnails.Want(paths);
}

bool FileGrid::PaintTile(Renderer &renderer, const Layout &layout, int index) const
{
    const file *f = m_files[index];
    bool selected = IsSelected(f);
    RECT cell = CellRect(layout, index);
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const float line = Scale(4) / 4.0f;

    float hover = m_hover.Level(index);
    if (hover > 0.0f && !selected)
        renderer.FillRoundedRect(ToRectF(ItemRect(layout, index)), radius, Faded(theme::kTileHover, hover));

    // A tag being dragged would land on this file: the oxblood outline of a drop target.
    if (!m_dropTargets.empty() && m_dropTargets.count(f))
    {
        D2D1_RECT_F item = ToRectF(ItemRect(layout, index));
        renderer.FillRoundedRect(item, radius, Faded(theme::kOxbloodFill, 0.35f));
        renderer.DrawRoundedRect(D2D1::RectF(item.left + line / 2, item.top + line / 2, item.right - line / 2,
            item.bottom - line / 2), radius, theme::kOxblood, 1.5f * line);
    }

    RECT plate = { cell.left + Scale(kPlateInset), cell.top + Scale(kPlateInset), 0, 0 };
    plate.right = plate.left + Scale(m_tileSize);
    plate.bottom = plate.top + Scale(m_tileSize);
    if (selected)
        renderer.FillRoundedRect(ToRectF(plate), radius, theme::kPlate);

    RECT icon = plate;
    InflateRect(&icon, -Scale(kIconInset), -Scale(kIconInset));
    std::string path = f->get_path();
    float shown = 0.0f;
    ID2D1Bitmap *thumbnail = m_thumbnails.Get(renderer, path, shown);

    // The icon: a tile of the control colour with a drawing of the file's type and its extension. A thumbnail
    // fades in over it as it fades out.
    float iconLevel = 1.0f - shown;
    if (iconLevel > 0.0f)
    {
        renderer.FillRoundedRect(ToRectF(icon), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kControl, iconLevel));

        std::string ext = FileExtension(path);
        const TypeIcon &type = TypeIconOf(renderer, static_cast<int>(IconKind(KindOf(path))));
        D2D1_POINT_2F center = D2D1::Point2F((icon.left + icon.right) / 2.0f, icon.top + (icon.bottom - icon.top) * 0.42f);
        renderer.DrawPath(type.lines.Get(), center, Faded(theme::kText3, iconLevel), 1.5f * line);
        renderer.FillPath(type.fills.Get(), center, Faded(theme::kText3, iconLevel));

        if (!ext.empty())
        {
            float labelLeft = static_cast<float>(icon.left + Scale(kNamePadding));
            float labelWidth = static_cast<float>(icon.right - Scale(kNamePadding)) - labelLeft;
            float labelTop = icon.bottom - Scale(28) + (Scale(28) - Scale(8) - renderer.LineHeight(Font::kMono)) / 2.0f;
            renderer.DrawLayout(ExtensionLabel(renderer, ext, labelWidth), labelLeft, labelTop, Faded(theme::kText3, iconLevel));
        }
    }

    // The thumbnail, centered in the icon's square at its own size: Windows made it to fit, and a small picture
    // isn't blown up.
    if (thumbnail)
    {
        D2D1_SIZE_U pixels = thumbnail->GetPixelSize();
        int width = static_cast<int>(pixels.width);
        int height = static_cast<int>(pixels.height);
        int room = icon.right - icon.left;
        if (width > room || height > room)
        {
            int longest = width > height ? width : height;
            width = MulDiv(width, room, longest);
            height = MulDiv(height, room, longest);
        }
        int left = icon.left + (room - width) / 2;
        int top = icon.top + (room - height) / 2;
        renderer.DrawBitmap(thumbnail, D2D1::RectF(static_cast<float>(left), static_cast<float>(top),
            static_cast<float>(left + width), static_cast<float>(top + height)),
            static_cast<float>(Scale(theme::kControlRadius)), shown);
    }

    // A file no longer at its path fades back, with a caution badge on its corner.
    if (m_missing && m_missing->count(path))
    {
        renderer.FillRoundedRect(ToRectF(icon), static_cast<float>(Scale(theme::kControlRadius)), Faded(theme::kLayer, 0.55f));
        float size = static_cast<float>(Scale(kBadgeSize));
        float right = static_cast<float>(icon.right) - line, top = static_cast<float>(icon.top) + line;
        const D2D1_POINT_2F badge[] = { { right - size / 2, top }, { right, top + size * 0.88f }, { right - size, top + size * 0.88f } };
        renderer.FillPolygon(badge, 3, theme::kCaution);
        float cx = right - size / 2;
        renderer.DrawLine({ cx, top + size * 0.32f }, { cx, top + size * 0.56f }, theme::kOnAccent, 1.5f * line);
        renderer.FillCircle({ cx, top + size * 0.72f }, 0.9f * line, theme::kOnAccent);
    }

    // The name, centered under the plate on up to two lines.
    float padding = static_cast<float>(Scale(kNamePadding));
    IDWriteTextLayout *name = NameLayoutOf(renderer, f, path);
    if (!name)
        return shown < 1.0f && thumbnail;

    float x = plate.left + padding;
    float y = static_cast<float>(plate.bottom + Scale(kNameGap));
    D2D1_COLOR_F textColor = theme::kText1;
    if (selected)
    {
        DWRITE_TEXT_METRICS metrics;
        if (SUCCEEDED(name->GetMetrics(&metrics)))
        {
            D2D1_RECT_F highlight = D2D1::RectF(x + metrics.left - padding, y + metrics.top - line,
                x + metrics.left + metrics.width + padding, y + metrics.top + metrics.height + line);
            renderer.FillRoundedRect(highlight, radius, m_active ? theme::kYuri : theme::kText3);
        }
        textColor = theme::kOnAccent;
    }
    renderer.DrawLayout(name, x, y, textColor);
    return shown < 1.0f && thumbnail;
}

// ---- Banner ----

bool FileGrid::BannerShown() const
{
    return !m_bannerText.empty() && !m_files.empty();
}

RECT FileGrid::BannerRect() const
{
    if (!BannerShown())
        return RECT{};
    int side = Scale(theme::kPageMargin) + Scale(kBannerMargin);
    RECT r = { m_bounds.left + side, m_bounds.top + Scale(kBannerMargin), m_bounds.right - side,
               m_bounds.top + Scale(kBannerHeight) - Scale(kBannerMargin) / 2 };
    if (r.right < r.left || r.bottom > m_bounds.bottom)
        return RECT{};
    return r;
}

// The banner's button, at its right end. Empty while the banner has none or doesn't show.
RECT FileGrid::BannerButtonRect() const
{
    RECT banner = BannerRect();
    if (IsRectEmpty(&banner) || !m_bannerButton || !m_renderer)
        return RECT{};
    int width = static_cast<int>(std::ceil(m_renderer->TextWidth(m_bannerButton, Font::kBody))) + 2 * Scale(kButtonPadding);
    int height = Scale(theme::kControlHeight);
    int right = banner.right - (banner.bottom - banner.top - height) / 2;
    int top = (banner.top + banner.bottom - height) / 2;
    RECT r = { right - width, top, right, top + height };
    if (r.left < banner.left)
        return RECT{};
    return r;
}

// A strip over the tiles, for a word about the files shown (in Missing, what it means) and a button to act on it.
void FileGrid::PaintBanner(Renderer &renderer) const
{
    RECT banner = BannerRect();
    if (IsRectEmpty(&banner))
        return;
    float line = Scale(4) / 4.0f;
    float radius = static_cast<float>(Scale(theme::kChipRadius));
    D2D1_RECT_F box = ToRectF(banner);
    renderer.FillRoundedRect(box, radius, theme::kBase);
    renderer.DrawRoundedRect(D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2),
        radius, theme::kStroke, line);

    // The caution triangle of the Missing view.
    float u = line;
    float x = static_cast<float>(banner.left + Scale(kBannerPadding));
    float cy = (banner.top + banner.bottom) / 2.0f;
    const D2D1_POINT_2F triangle[] = { { x + 7.0f * u, cy - 6.0f * u }, { x + 13.5f * u, cy + 5.5f * u }, { x + 0.5f * u, cy + 5.5f * u } };
    renderer.DrawPolyline(triangle, 3, theme::kCaution, 1.5f * u, true);
    renderer.DrawLine({ x + 7.0f * u, cy - 2.0f * u }, { x + 7.0f * u, cy + 1.25f * u }, theme::kCaution, 1.5f * u);
    renderer.FillCircle({ x + 7.0f * u, cy + 3.25f * u }, 0.85f * u, theme::kCaution);

    RECT button = BannerButtonRect();
    RECT text = { banner.left + Scale(kBannerPadding) + Scale(14) + Scale(10), banner.top,
                  IsRectEmpty(&button) ? banner.right - Scale(kBannerPadding) : button.left - Scale(kBannerPadding), banner.bottom };
    if (text.right > text.left)
        renderer.DrawTextLine(m_bannerText, Font::kCaption, text, theme::kText2);
    if (IsRectEmpty(&button))
        return;
    PaintButton(renderer, button, m_bannerButton);
}

// A plain button: the control colour with a thin edge, lighter under the pointer.
void FileGrid::PaintButton(Renderer &renderer, const RECT &button, const wchar_t *label) const
{
    const float radius = static_cast<float>(Scale(theme::kControlRadius));
    const float line = Scale(4) / 4.0f;
    D2D1_RECT_F box = ToRectF(button);
    renderer.FillRoundedRect(box, radius, theme::kControl);
    float hover = m_buttonHover.Level(0);
    if (hover > 0.0f)
        renderer.FillRoundedRect(box, radius, Faded(theme::kHover, hover));
    renderer.DrawRoundedRect(D2D1::RectF(box.left + line / 2, box.top + line / 2, box.right - line / 2, box.bottom - line / 2),
        radius, theme::kStroke, line);
    renderer.DrawTextLine(label, Font::kBody, button, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ---- Cached drawings ----

void FileGrid::UpdateCacheDpi() const
{
    UINT dpi = m_hWnd ? GetDpiForWindow(m_hWnd) : 0;
    if (dpi == m_cacheDpi)
        return;
    m_cacheDpi = dpi;
    m_names.clear();
    m_extensions.clear();
    for (TypeIcon &icon : m_icons)
        icon = TypeIcon();
}

IDWriteTextLayout *FileGrid::NameLayoutOf(Renderer &renderer, const file *f, const std::string &path) const
{
    NameLayout &name = m_names[f];
    if (!name.layout || name.path != path)
    {
        name.path = path;
        float width = static_cast<float>(Scale(m_tileSize) - 2 * Scale(kNamePadding));
        name.layout = renderer.Layout(Utf8ToWide(FileName(name.path)), Font::kCaption, width, 2, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    return name.layout.Get();
}

IDWriteTextLayout *FileGrid::ExtensionLabel(Renderer &renderer, const std::string &ext, float width) const
{
    ComPtr<IDWriteTextLayout> &label = m_extensions[ext];
    if (!label)
    {
        std::wstring text = Utf8ToWide(ext);
        for (wchar_t &c : text)
            c = towupper(c);
        label = renderer.Layout(text, Font::kMono, width, 1, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    return label.Get();
}

const FileGrid::TypeIcon &FileGrid::TypeIconOf(Renderer &renderer, int kind) const
{
    TypeIcon &icon = m_icons[kind];
    if (!icon.lines)
    {
        PathBuilder lines = renderer.BeginPath(false);
        PathBuilder fills = renderer.BeginPath(true);
        BuildTypeIcon(lines, fills, static_cast<Kind>(kind), static_cast<float>(Scale(kTypeIconSize)), 1.5f * Scale(4) / 4.0f);
        icon.lines = lines.Finish();
        icon.fills = fills.Finish();
    }
    return icon;
}

const wchar_t *FileGrid::ButtonLabel() const
{
    return m_empty == Empty::kLibrary ? strings::kAddFolder : strings::kClearFilters;
}

// Where the text of an empty grid goes ('middle' is the line it is placed from) and its button under it, the
// whole block centered in 'area'. Returns false if the grid is too small to show anything. In a short grid the
// folder and then the hint are left out ('folder', 'hint' say which are shown), so the block stays in the box.
bool FileGrid::EmptyLayout(RECT &area, int &middle, RECT &button, bool *folder, bool *hint) const
{
    area = m_bounds;
    InflateRect(&area, -Scale(kEmptyInset), -Scale(kEmptyInset));
    if (!m_renderer || area.right <= area.left || area.bottom <= area.top)
        return false;

    const int gap = Scale(kButtonGap);
    const int height = Scale(theme::kControlHeight);
    const int lineHeight = static_cast<int>(m_renderer->LineHeight(Font::kBodyStrong));
    const int folderHeight = static_cast<int>(kFolderTop * Scale(kFolderIconSize));
    const int hintHeight = Scale(2) + lineHeight;
    const int room = area.bottom - area.top - Scale(8);  // a little air inside the dashed box
    bool showFolder = false, showHint = false;
    int center = (area.top + area.bottom) / 2;
    int textBottom;
    if (m_empty != Empty::kLibrary)
    {
        // One line of text from middle - 16 to middle + 16, then the button.
        middle = center - (gap + height) / 2;
        textBottom = middle + Scale(16);
    }
    else
    {
        // The folder and the title above the middle, the hint and the button below it.
        showFolder = room >= lineHeight + folderHeight + hintHeight + gap + height;
        showHint = showFolder || room >= lineHeight + hintHeight + gap + height;
        int above = lineHeight + (showFolder ? folderHeight : 0);
        int below = (showHint ? hintHeight : 0) + gap + height;
        middle = center + (above - below) / 2;
        textBottom = middle + (showHint ? hintHeight : 0);
    }
    if (folder)
        *folder = showFolder;
    if (hint)
        *hint = showHint;
    int width = static_cast<int>(m_renderer->TextWidth(ButtonLabel(), Font::kBody) + 0.99f) + 2 * Scale(kButtonPadding);
    int left = (area.left + area.right - width) / 2;
    button = { left, textBottom + gap, left + width, textBottom + gap + height };
    return true;
}

// An empty library shows where to drop files; a search that finds nothing says so.
void FileGrid::PaintEmpty(Renderer &renderer) const
{
    RECT area, button;
    int middle;
    bool showFolder, showHint;
    if (!EmptyLayout(area, middle, button, &showFolder, &showHint))
        return;

    float lineHeight = renderer.LineHeight(Font::kBodyStrong);
    float line = Scale(4) / 4.0f;

    if (m_empty != Empty::kLibrary)
    {
        RECT text = { area.left, middle - Scale(16), area.right, middle + Scale(16) };
        renderer.DrawTextLine(m_empty == Empty::kChecking ? strings::kChecking : strings::kNoMatches, Font::kBody,
            text, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
        // Nothing to click while the disk is being checked.
        if (m_empty == Empty::kChecking)
            return;
    }
    else
    {
        D2D1_RECT_F outline = ToRectF(area);
        renderer.DrawRoundedRect(D2D1::RectF(outline.left + line, outline.top + line, outline.right - line, outline.bottom - line),
            static_cast<float>(Scale(theme::kControlRadius)), theme::kYuri, 1.5f * line, true);

        // A folder above the two lines of text.
        if (showFolder)
        {
            float s = static_cast<float>(Scale(kFolderIconSize));
            D2D1_POINT_2F c = D2D1::Point2F((area.left + area.right) / 2.0f, middle - lineHeight - s * 0.45f);
            auto P = [&](float x, float y) { return D2D1::Point2F(c.x + x * s, c.y + y * s); };
            const D2D1_POINT_2F folder[] = { P(-0.5f, -0.32f), P(-0.14f, -0.32f), P(-0.04f, -0.2f), P(0.5f, -0.2f), P(0.5f, 0.32f), P(-0.5f, 0.32f) };
            renderer.DrawPolyline(folder, 6, theme::kText3, 1.5f * line, true);
        }

        RECT title = { area.left, middle - static_cast<int>(lineHeight), area.right, middle };
        renderer.DrawTextLine(strings::kEmptyLibrary, Font::kBodyStrong, title, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (showHint)
        {
            RECT hint = { area.left, middle + Scale(2), area.right, middle + Scale(2) + static_cast<int>(lineHeight) };
            renderer.DrawTextLine(strings::kEmptyLibraryHint, Font::kCaption, hint, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    PaintButton(renderer, button, ButtonLabel());
}

void FileGrid::PaintScrollbar(Renderer &renderer, const Layout &layout) const
{
    RECT track, thumb;
    if (!GetScrollbarRects(layout, track, thumb))
        return;

    renderer.FillRoundedRect(ToRectF(thumb), static_cast<float>(Scale(theme::kControlRadius)),
        m_draggingThumb ? theme::kScrollThumbActive : theme::kScrollThumb);
}
