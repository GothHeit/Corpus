#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "file_grid.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/file.hpp"
#include <cctype>
#include <string>

namespace
{
    const int kCellSize = 96;
    const int kIconSize = 56;
    const int kItemInset = 4;         // gap between an item's highlight and its cell edge
    const int kIconTopPadding = 4;
    const int kNameGap = 4;           // between the icon and the file name
    const int kNameSidePadding = 2;
    const int kSelectionRadius = 6;
    const int kExtFontSize = 16;
    const int kNameFontSize = 12;

    const int kScrollbarWidth = 6;
    const int kScrollbarEdgeGap = 8;  // from the right and bottom edges of the grid
    const int kScrollbarMinThumb = 24;
    const int kScrollbarHitSlop = 6;  // the bar is thin, so clicks a bit to its left still count

    std::string FileExtension(const std::string &path)
    {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return std::string();

        std::string ext = path.substr(dot + 1);
        for (char &c : ext)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return ext;
    }

    std::string FileName(const std::string &path)
    {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos)
            return path;
        return path.substr(slash + 1);
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

void FileGrid::SetBounds(HWND hWnd, const RECT &bounds)
{
    m_hWnd = hWnd;
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

    Invalidate();
}

// ---- Layout ----

FileGrid::Layout FileGrid::ComputeLayout() const
{
    const int margin = Scale(theme::kPageMargin);

    Layout layout;
    layout.cellSize = Scale(kCellSize);
    layout.iconSize = Scale(kIconSize);

    int usableWidth = (m_bounds.right - m_bounds.left) - 2 * margin;
    layout.columns = usableWidth / layout.cellSize;
    if (layout.columns < 1)
        layout.columns = 1;

    layout.left = m_bounds.left + margin + (usableWidth - layout.columns * layout.cellSize) / 2;

    int count = static_cast<int>(m_files.size());
    int rows = (count + layout.columns - 1) / layout.columns;

    layout.viewportHeight = m_bounds.bottom - m_bounds.top;
    if (layout.viewportHeight < 0)
        layout.viewportHeight = 0;
    layout.contentHeight = rows * layout.cellSize + margin;

    return layout;
}

RECT FileGrid::ItemRect(const Layout &layout, int index) const
{
    const int inset = Scale(kItemInset);
    int col = index % layout.columns;
    int row = index / layout.columns;

    int cellX = layout.left + col * layout.cellSize;
    int cellY = m_bounds.top + row * layout.cellSize - m_scrollY;

    RECT r = { cellX + inset, cellY + inset, cellX + layout.cellSize - inset, cellY + layout.cellSize - inset };
    return r;
}

int FileGrid::HitTest(const Layout &layout, POINT pt) const
{
    if (pt.y < m_bounds.top || pt.x < layout.left)
        return -1;

    int col = (pt.x - layout.left) / layout.cellSize;
    int row = (pt.y - m_bounds.top + m_scrollY) / layout.cellSize;
    if (col >= layout.columns)
        return -1;

    int index = row * layout.columns + col;
    if (index >= static_cast<int>(m_files.size()))
        return -1;

    RECT item = ItemRect(layout, index);
    return PtInRect(&item, pt) ? index : -1;
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
        Invalidate();
    }
}

void FileGrid::EnsureVisible(const Layout &layout, int index)
{
    int rowTop = (index / layout.columns) * layout.cellSize;
    int rowBottom = rowTop + layout.cellSize;

    if (rowTop < m_scrollY)
        ScrollTo(rowTop);
    else if (rowBottom > m_scrollY + layout.viewportHeight)
        ScrollTo(rowBottom - layout.viewportHeight);
}

void FileGrid::OnMouseWheel(int delta)
{
    // One wheel notch scrolls one row; touchpads send smaller deltas and scroll proportionally.
    ScrollTo(m_scrollY - MulDiv(delta, ComputeLayout().cellSize, WHEEL_DELTA));
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
        int firstRow = (m_scrollY + layout.cellSize - 1) / layout.cellSize;
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

void FileGrid::Paint(HDC dc) const
{
    Layout layout = ComputeLayout();
    if (layout.viewportHeight <= 0)
        return;

    // Rows scrolled above the grid must not paint over the search bar.
    int savedDC = SaveDC(dc);
    IntersectClipRect(dc, m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.bottom);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme::kText);

    HFONT extFont = CreateUIFont(Scale(kExtFontSize), FW_BOLD);
    HFONT nameFont = CreateUIFont(Scale(kNameFontSize));

    Gdiplus::Graphics graphics(dc);
    graphics.SetClip(Gdiplus::Rect(m_bounds.left, m_bounds.top, m_bounds.right - m_bounds.left, layout.viewportHeight));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush iconBrush(ToGdiplusColor(theme::kFileIcon));
    Gdiplus::SolidBrush selectedIconBrush(ToGdiplusColor(theme::kFileIconSelected));
    Gdiplus::SolidBrush selectionBrush(ToGdiplusColor(theme::kSelectionFill));
    Gdiplus::Pen selectionPen(ToGdiplusColor(theme::kSelectionBorder), static_cast<float>(Scale(1)));

    // Only the rows inside the viewport are drawn.
    int count = static_cast<int>(m_files.size());
    int firstRow = m_scrollY / layout.cellSize;
    int lastRow = (m_scrollY + layout.viewportHeight) / layout.cellSize;
    int first = firstRow * layout.columns;
    int last = (lastRow + 1) * layout.columns;
    if (last > count)
        last = count;

    for (int index = first; index < last; index++)
    {
        const file *f = m_files[index];
        RECT item = ItemRect(layout, index);
        bool selected = IsSelected(f);

        if (selected)
        {
            Gdiplus::GraphicsPath path;
            AddRoundedRect(path, item, Scale(kSelectionRadius));
            graphics.FillPath(&selectionBrush, &path);
            graphics.DrawPath(&selectionPen, &path);
        }

        int iconX = item.left + (item.right - item.left - layout.iconSize) / 2;
        int iconY = item.top + Scale(kIconTopPadding);

        graphics.FillRectangle(selected ? &selectedIconBrush : &iconBrush, iconX, iconY, layout.iconSize, layout.iconSize);

        SelectObject(dc, extFont);
        std::wstring ext = Utf8ToWide(FileExtension(f->get_path()));
        RECT extRect = { iconX, iconY, iconX + layout.iconSize, iconY + layout.iconSize };
        DrawTextW(dc, ext.c_str(), -1, &extRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(dc, nameFont);
        std::wstring name = Utf8ToWide(FileName(f->get_path()));
        RECT nameRect = { item.left + Scale(kNameSidePadding), iconY + layout.iconSize + Scale(kNameGap),
                          item.right - Scale(kNameSidePadding), item.bottom };
        DrawTextW(dc, name.c_str(), -1, &nameRect, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    RestoreDC(dc, savedDC);
    DeleteObject(extFont);
    DeleteObject(nameFont);

    PaintScrollbar(dc, layout);
}

void FileGrid::PaintScrollbar(HDC dc, const Layout &layout) const
{
    RECT track, thumb;
    if (!GetScrollbarRects(layout, track, thumb))
        return;

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::SolidBrush thumbBrush(ToGdiplusColor(m_draggingThumb ? theme::kScrollThumbActive : theme::kScrollThumb));

    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, thumb, (thumb.right - thumb.left) / 2);
    graphics.FillPath(&thumbBrush, &path);
}
