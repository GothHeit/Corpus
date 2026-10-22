#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "file_grid.hpp"
#include "renderer.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include <cctype>
#include <cwctype>
#include <string>

namespace
{
    // A tile: the plate (the area that greys out when selected), with the icon inside it, and the name below.
    const int kCellWidth = 136;
    const int kCellHeight = 176;
    const int kPlateInset = 4;        // from the top and sides of the cell
    const int kPlateSize = 128;
    const int kIconInset = 4;         // the icon inside the plate
    const int kNameGap = 4;           // between the plate and the name
    const int kNamePadding = 4;       // around the name, inside its highlight
    const int kGridTop = 12;          // above the first row
    const int kTypeIconSize = 36;     // the line drawing in the middle of the icon

    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 4;  // from the right and bottom edges of the grid
    const int kScrollbarMinThumb = 24;
    const int kScrollbarHitSlop = 6;  // the bar is thin, so clicks a bit to its left still count

    const int kEmptyInset = 24;       // the drop area of an empty library, from the edges of the grid

    enum class Kind { kOther, kImage, kVideo, kAudio, kDocument, kArchive };

    std::string FileExtension(const std::string &path)
    {
        size_t dot = path.find_last_of('.');
        size_t slash = path.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return std::string();

        std::string ext = path.substr(dot + 1);
        for (char &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext;
    }

    std::string FileName(const std::string &path)
    {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos)
            return path;
        return path.substr(slash + 1);
    }

    // What kind of file an extension (in lower case) is, for its icon.
    Kind KindOf(const std::string &ext)
    {
        struct Entry { const char *ext; Kind kind; };
        static const Entry kinds[] =
        {
            { "jpg", Kind::kImage }, { "jpeg", Kind::kImage }, { "png", Kind::kImage }, { "gif", Kind::kImage },
            { "webp", Kind::kImage }, { "bmp", Kind::kImage }, { "tif", Kind::kImage }, { "tiff", Kind::kImage },
            { "heic", Kind::kImage }, { "avif", Kind::kImage }, { "svg", Kind::kImage }, { "ico", Kind::kImage },
            { "jfif", Kind::kImage }, { "raw", Kind::kImage }, { "cr2", Kind::kImage }, { "nef", Kind::kImage },
            { "dng", Kind::kImage }, { "psd", Kind::kImage },
            { "mp4", Kind::kVideo }, { "mkv", Kind::kVideo }, { "avi", Kind::kVideo }, { "mov", Kind::kVideo },
            { "webm", Kind::kVideo }, { "wmv", Kind::kVideo }, { "flv", Kind::kVideo }, { "m4v", Kind::kVideo },
            { "mpg", Kind::kVideo }, { "mpeg", Kind::kVideo }, { "ts", Kind::kVideo },
            { "mp3", Kind::kAudio }, { "flac", Kind::kAudio }, { "wav", Kind::kAudio }, { "ogg", Kind::kAudio },
            { "m4a", Kind::kAudio }, { "aac", Kind::kAudio }, { "opus", Kind::kAudio }, { "wma", Kind::kAudio },
            { "pdf", Kind::kDocument }, { "txt", Kind::kDocument }, { "md", Kind::kDocument },
            { "doc", Kind::kDocument }, { "docx", Kind::kDocument }, { "odt", Kind::kDocument },
            { "rtf", Kind::kDocument }, { "xls", Kind::kDocument }, { "xlsx", Kind::kDocument },
            { "ods", Kind::kDocument }, { "csv", Kind::kDocument }, { "ppt", Kind::kDocument },
            { "pptx", Kind::kDocument }, { "odp", Kind::kDocument }, { "epub", Kind::kDocument },
            { "html", Kind::kDocument }, { "htm", Kind::kDocument },
            { "zip", Kind::kArchive }, { "rar", Kind::kArchive }, { "7z", Kind::kArchive }, { "tar", Kind::kArchive },
            { "gz", Kind::kArchive }, { "bz2", Kind::kArchive }, { "xz", Kind::kArchive }, { "zst", Kind::kArchive },
        };
        for (const Entry &entry : kinds)
        {
            if (ext == entry.ext)
                return entry.kind;
        }
        return Kind::kOther;
    }

    // Draws the icon of a kind of file as a line drawing in a square of side 's' centered on 'c'.
    void PaintTypeIcon(Renderer &renderer, Kind kind, D2D1_POINT_2F c, float s, float line, const D2D1_COLOR_F &color)
    {
        auto P = [&](float x, float y) { return D2D1::Point2F(c.x + x * s, c.y + y * s); };

        switch (kind)
        {
        case Kind::kImage:
        {
            renderer.DrawRoundedRect(D2D1::RectF(c.x - 0.5f * s, c.y - 0.38f * s, c.x + 0.5f * s, c.y + 0.38f * s), line * 1.5f, color, line);
            const D2D1_POINT_2F hills[] = { P(-0.38f, 0.26f), P(-0.12f, -0.02f), P(0.06f, 0.16f), P(0.18f, 0.06f), P(0.38f, 0.26f) };
            renderer.DrawPolyline(hills, 5, color, line);
            renderer.DrawCircle(P(0.22f, -0.16f), 0.07f * s, color, line);
            break;
        }
        case Kind::kVideo:
        {
            renderer.DrawRoundedRect(D2D1::RectF(c.x - 0.5f * s, c.y - 0.34f * s, c.x + 0.5f * s, c.y + 0.34f * s), line * 1.5f, color, line);
            const D2D1_POINT_2F play[] = { P(-0.12f, -0.16f), P(0.18f, 0.0f), P(-0.12f, 0.16f) };
            renderer.FillPolygon(play, 3, color);
            break;
        }
        case Kind::kAudio:
        {
            // A pair of eighth notes joined by a beam.
            renderer.DrawLine(P(-0.18f, 0.24f), P(-0.18f, -0.34f), color, line);
            renderer.DrawLine(P(0.3f, 0.16f), P(0.3f, -0.42f), color, line);
            const D2D1_POINT_2F beam[] = { P(-0.18f, -0.34f), P(0.3f, -0.42f), P(0.3f, -0.3f), P(-0.18f, -0.22f) };
            renderer.FillPolygon(beam, 4, color);
            renderer.FillCircle(P(-0.29f, 0.26f), 0.12f * s, color);
            renderer.FillCircle(P(0.19f, 0.18f), 0.12f * s, color);
            break;
        }
        default:
        {
            // A page with a folded corner.
            const float fold = 0.22f;
            const D2D1_POINT_2F page[] = { P(-0.36f, -0.48f), P(0.36f - fold, -0.48f), P(0.36f, -0.48f + fold), P(0.36f, 0.48f), P(-0.36f, 0.48f) };
            renderer.DrawPolyline(page, 5, color, line, true);
            const D2D1_POINT_2F corner[] = { P(0.36f - fold, -0.48f), P(0.36f - fold, -0.48f + fold), P(0.36f, -0.48f + fold) };
            renderer.DrawPolyline(corner, 3, color, line);

            if (kind == Kind::kDocument)
            {
                for (float y : { -0.04f, 0.12f, 0.28f })
                    renderer.DrawLine(P(-0.2f, y), P(0.2f, y), color, line);
            }
            else if (kind == Kind::kArchive)
            {
                // A zipper down the middle.
                for (int i = 0; i < 4; i++)
                {
                    float y = -0.42f + i * 0.14f;
                    float x = (i % 2) ? 0.0f : -0.08f;
                    renderer.FillRect(D2D1::RectF(c.x + x * s, c.y + y * s, c.x + (x + 0.08f) * s, c.y + (y + 0.07f) * s), color);
                }
                renderer.DrawRoundedRect(D2D1::RectF(P(-0.1f, 0.14f).x, P(0, 0.14f).y, P(0.1f, 0).x, P(0, 0.34f).y), line, color, line);
            }
            break;
        }
        }
    }
}

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
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    ScrollTo(m_scrollY);
}

void FileGrid::SetFiles(const std::vector<file*> &files)
{
    if (files == m_files)
        return;

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

    UpdateHover();
    Invalidate();
}

void FileGrid::Clear()
{
    m_files.clear();
    m_selection.clear();
    m_selectionAnchor = nullptr;
    m_focusItem = nullptr;
    m_scrollY = 0;
    m_hoverIndex = -1;
    Invalidate();
}

void FileGrid::SetEmpty(Empty empty)
{
    if (empty == m_empty)
        return;
    m_empty = empty;
    if (m_files.empty())
        Invalidate();
}

void FileGrid::SetActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (!m_selection.empty())
        Invalidate();
}

// ---- Layout ----

FileGrid::Layout FileGrid::ComputeLayout() const
{
    const int margin = Scale(theme::kPageMargin);

    Layout layout;
    layout.cellWidth = Scale(kCellWidth);
    layout.cellHeight = Scale(kCellHeight);

    int usableWidth = (m_bounds.right - m_bounds.left) - 2 * margin;
    layout.columns = usableWidth / layout.cellWidth;
    if (layout.columns < 1)
        layout.columns = 1;

    layout.left = m_bounds.left + margin + (usableWidth - layout.columns * layout.cellWidth) / 2;
    layout.top = m_bounds.top + Scale(kGridTop);

    int count = static_cast<int>(m_files.size());
    int rows = (count + layout.columns - 1) / layout.columns;

    layout.viewportHeight = m_bounds.bottom - m_bounds.top;
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
    if (pt.y < m_bounds.top || pt.y >= m_bounds.bottom || pt.x < layout.left)
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
    if (!PtInRect(&m_bounds, pt))
        return nullptr;

    int index = HitTest(ComputeLayout(), pt);
    return index >= 0 ? m_files[index] : nullptr;
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
    if (m_hWnd && !m_draggingThumb && PtInRect(&m_bounds, m_hoverPoint))
        index = HitTest(ComputeLayout(), m_hoverPoint);

    if (index != m_hoverIndex)
    {
        m_hoverIndex = index;
        Invalidate();
    }
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

    int right = m_bounds.right - Scale(kScrollbarEdgeGap);
    track = { right - Scale(kScrollbarWidth), m_bounds.top, right, m_bounds.bottom - Scale(kScrollbarEdgeGap) };
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
        return;
    }

    Layout layout = ComputeLayout();
    if (layout.viewportHeight <= 0)
        return;

    // Rows scrolled out of the grid must not paint over the search bar or the tag strip.
    renderer.PushClip(m_bounds);

    // Only the rows inside the viewport are drawn.
    int count = static_cast<int>(m_files.size());
    int scrolled = m_scrollY - Scale(kGridTop);
    int firstRow = scrolled > 0 ? scrolled / layout.cellHeight : 0;
    int lastRow = (m_scrollY + layout.viewportHeight) / layout.cellHeight;
    int first = firstRow * layout.columns;
    int last = (lastRow + 1) * layout.columns;
    if (last > count)
        last = count;

    for (int index = first; index < last; index++)
        PaintTile(renderer, layout, index);

    renderer.PopClip();

    PaintScrollbar(renderer, layout);
}

void FileGrid::PaintTile(Renderer &renderer, const Layout &layout, int index) const
{
    const file *f = m_files[index];
    bool selected = IsSelected(f);
    RECT cell = CellRect(layout, index);
    const float radius = static_cast<float>(Scale(theme::kChipRadius));
    const float line = Scale(4) / 4.0f;

    if (index == m_hoverIndex && !selected)
        renderer.FillRoundedRect(ToRectF(ItemRect(layout, index)), radius, theme::kHover);

    RECT plate = { cell.left + Scale(kPlateInset), cell.top + Scale(kPlateInset), 0, 0 };
    plate.right = plate.left + Scale(kPlateSize);
    plate.bottom = plate.top + Scale(kPlateSize);
    if (selected)
        renderer.FillRoundedRect(ToRectF(plate), radius, theme::kPlate);

    // The icon: a tile of the control colour with a drawing of the file's type and its extension.
    RECT icon = plate;
    InflateRect(&icon, -Scale(kIconInset), -Scale(kIconInset));
    renderer.FillRoundedRect(ToRectF(icon), static_cast<float>(Scale(theme::kControlRadius)), theme::kControl);

    std::string path = f->get_path();
    std::string ext = FileExtension(path);
    float iconSize = static_cast<float>(Scale(kTypeIconSize));
    D2D1_POINT_2F center = D2D1::Point2F((icon.left + icon.right) / 2.0f, icon.top + (icon.bottom - icon.top) * 0.42f);
    PaintTypeIcon(renderer, KindOf(ext), center, iconSize, 1.5f * line, theme::kText3);

    if (!ext.empty())
    {
        std::wstring label = Utf8ToWide(ext);
        for (wchar_t &c : label)
            c = towupper(c);
        RECT extRect = { icon.left + Scale(kNamePadding), icon.bottom - Scale(28), icon.right - Scale(kNamePadding), icon.bottom - Scale(8) };
        renderer.DrawTextLine(label, Font::kMono, extRect, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // The name, centered under the plate on up to two lines.
    float padding = static_cast<float>(Scale(kNamePadding));
    float nameWidth = static_cast<float>(plate.right - plate.left) - 2 * padding;
    ComPtr<IDWriteTextLayout> name = renderer.Layout(Utf8ToWide(FileName(path)), Font::kCaption, nameWidth, 2,
        DWRITE_TEXT_ALIGNMENT_CENTER);
    if (!name)
        return;

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
    renderer.DrawLayout(name.Get(), x, y, textColor);
}

// An empty library shows where to drop files; a search that finds nothing says so.
void FileGrid::PaintEmpty(Renderer &renderer) const
{
    RECT area = m_bounds;
    InflateRect(&area, -Scale(kEmptyInset), -Scale(kEmptyInset));
    if (area.right <= area.left || area.bottom <= area.top)
        return;

    float lineHeight = renderer.LineHeight(Font::kBodyStrong);
    int middle = (area.top + area.bottom) / 2;

    if (m_empty == Empty::kNoMatches)
    {
        RECT text = { area.left, middle - Scale(16), area.right, middle + Scale(16) };
        renderer.DrawTextLine(strings::kNoMatches, Font::kBody, text, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }

    float line = Scale(4) / 4.0f;
    D2D1_RECT_F outline = ToRectF(area);
    renderer.DrawRoundedRect(D2D1::RectF(outline.left + line, outline.top + line, outline.right - line, outline.bottom - line),
        static_cast<float>(Scale(theme::kControlRadius)), theme::kYuri, 1.5f * line, true);

    // A folder above the two lines of text.
    float s = static_cast<float>(Scale(40));
    D2D1_POINT_2F c = D2D1::Point2F((area.left + area.right) / 2.0f, middle - lineHeight - s * 0.45f);
    auto P = [&](float x, float y) { return D2D1::Point2F(c.x + x * s, c.y + y * s); };
    const D2D1_POINT_2F folder[] = { P(-0.5f, -0.32f), P(-0.14f, -0.32f), P(-0.04f, -0.2f), P(0.5f, -0.2f), P(0.5f, 0.32f), P(-0.5f, 0.32f) };
    renderer.DrawPolyline(folder, 6, theme::kText3, 1.5f * line, true);

    RECT title = { area.left, middle - static_cast<int>(lineHeight), area.right, middle };
    renderer.DrawTextLine(strings::kEmptyLibrary, Font::kBodyStrong, title, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
    RECT hint = { area.left, middle + Scale(2), area.right, middle + Scale(2) + static_cast<int>(lineHeight) };
    renderer.DrawTextLine(strings::kEmptyLibraryHint, Font::kCaption, hint, theme::kText3, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void FileGrid::PaintScrollbar(Renderer &renderer, const Layout &layout) const
{
    RECT track, thumb;
    if (!GetScrollbarRects(layout, track, thumb))
        return;

    renderer.FillRoundedRect(ToRectF(thumb), static_cast<float>(Scale(theme::kControlRadius)),
        m_draggingThumb ? theme::kScrollThumbActive : theme::kScrollThumb);
}
