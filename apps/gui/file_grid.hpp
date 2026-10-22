#ifndef FILE_GRID_HPP
#define FILE_GRID_HPP

#include <windows.h>
#include <unordered_set>
#include <vector>

class file;
class Renderer;

// The scrollable grid of files: layout, painting, the scrollbar and selection
// (click, Ctrl/Shift+click, arrow keys, Ctrl+A, Esc).
//
// Each file is a tile in the style of the Finder: an icon for its type, and its name on up to two lines below.
// A selected tile gets a grey plate behind the icon and an accent highlight behind the name.
class FileGrid
{
public:
    // What the grid says when it has no files.
    enum class Empty { kLibrary, kNoMatches };

    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    // Replaces the files on display. Scrolls back to the top and drops hidden files from the selection.
    void SetFiles(const std::vector<file*> &files);
    // Forgets every file, including the selection. Call it before the library deletes its files, so no
    // pointer to a deleted file is left behind (a new file can be allocated at the same address).
    void Clear();
    void SetEmpty(Empty empty);
    // Selection turns grey while another window is active, as on macOS.
    void SetActive(bool active);
    void Paint(Renderer &renderer) const;

    // The file under the point, or nullptr.
    file *FileAt(POINT pt) const;
    // The selected files, in the order they appear on the grid.
    std::vector<file*> SelectedFiles() const;

    // Each returns true when the grid handled the event.
    bool OnLButtonDown(POINT pt, bool ctrl, bool shift);
    bool OnMouseMove(POINT pt);
    // Highlights the tile under the pointer. 'pt' is outside the window when the pointer left it.
    void OnHover(POINT pt);
    void OnLButtonUp();
    void OnCaptureChanged();
    bool OnKeyDown(WPARAM key, bool ctrl, bool shift);
    void OnMouseWheel(int delta);

private:
    struct Layout
    {
        int left;            // x of the first column
        int top;             // y of the first row, before scrolling
        int columns;
        int cellWidth;
        int cellHeight;
        int viewportHeight;  // visible height of the grid
        int contentHeight;   // height of all rows; scrolling moves the viewport over it
    };

    int Scale(int value) const;
    void Invalidate() const;

    Layout ComputeLayout() const;
    RECT CellRect(const Layout &layout, int index) const;
    RECT ItemRect(const Layout &layout, int index) const;
    int HitTest(const Layout &layout, POINT pt) const;
    void UpdateHover();

    int MaxScroll(const Layout &layout) const;
    void ScrollTo(int y);
    void EnsureVisible(const Layout &layout, int index);

    bool GetScrollbarRects(const Layout &layout, RECT &track, RECT &thumb) const;
    bool HandleScrollbarClick(POINT pt);
    void PaintTile(Renderer &renderer, const Layout &layout, int index) const;
    void PaintEmpty(Renderer &renderer) const;
    void PaintScrollbar(Renderer &renderer, const Layout &layout) const;

    void HandleItemClick(int index, bool ctrl, bool shift);
    void MoveFocus(WPARAM key, bool shift);
    void SelectRange(int from, int to);
    void SelectAll();
    void ClearSelection();
    bool IsSelected(const file *f) const;
    int IndexOf(const file *f) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    std::vector<file*> m_files;
    Empty m_empty = Empty::kLibrary;
    bool m_active = true;

    POINT m_hoverPoint = { -1, -1 };
    int m_hoverIndex = -1;

    int m_scrollY = 0;
    bool m_draggingThumb = false;
    int m_thumbDragOffset = 0;

    std::unordered_set<const file*> m_selection;
    const file *m_selectionAnchor = nullptr;  // where Shift ranges start from
    const file *m_focusItem = nullptr;        // where the arrow keys move from
};

#endif
