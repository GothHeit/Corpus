#ifndef FILE_GRID_HPP
#define FILE_GRID_HPP

#include <windows.h>
#include <unordered_set>
#include <vector>

class file;

// The scrollable grid of files: layout, painting, the scrollbar and selection
// (click, Ctrl/Shift+click, arrow keys, Ctrl+A, Esc).
class FileGrid
{
public:
    void SetBounds(HWND hWnd, const RECT &bounds);
    // Replaces the files on display. Scrolls back to the top and drops hidden files from the selection.
    void SetFiles(const std::vector<file*> &files);
    void Paint(HDC dc) const;

    // Each returns true when the grid handled the event.
    bool OnLButtonDown(POINT pt, bool ctrl, bool shift);
    bool OnMouseMove(POINT pt);
    void OnLButtonUp();
    void OnCaptureChanged();
    bool OnKeyDown(WPARAM key, bool ctrl, bool shift);
    void OnMouseWheel(int delta);

private:
    struct Layout
    {
        int left;            // x of the first column
        int columns;
        int cellSize;
        int iconSize;
        int viewportHeight;  // visible height of the grid
        int contentHeight;   // height of all rows; scrolling moves the viewport over it
    };

    int Scale(int value) const;
    void Invalidate() const;

    Layout ComputeLayout() const;
    RECT ItemRect(const Layout &layout, int index) const;
    int HitTest(const Layout &layout, POINT pt) const;

    int MaxScroll(const Layout &layout) const;
    void ScrollTo(int y);
    void EnsureVisible(const Layout &layout, int index);

    bool GetScrollbarRects(const Layout &layout, RECT &track, RECT &thumb) const;
    bool HandleScrollbarClick(POINT pt);
    void PaintScrollbar(HDC dc, const Layout &layout) const;

    void HandleItemClick(int index, bool ctrl, bool shift);
    void MoveFocus(WPARAM key, bool shift);
    void SelectRange(int from, int to);
    void SelectAll();
    void ClearSelection();
    bool IsSelected(const file *f) const;
    int IndexOf(const file *f) const;

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};
    std::vector<file*> m_files;

    int m_scrollY = 0;
    bool m_draggingThumb = false;
    int m_thumbDragOffset = 0;

    std::unordered_set<const file*> m_selection;
    const file *m_selectionAnchor = nullptr;  // where Shift ranges start from
    const file *m_focusItem = nullptr;        // where the arrow keys move from
};

#endif
