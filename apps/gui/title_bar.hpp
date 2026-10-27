#ifndef TITLE_BAR_HPP
#define TITLE_BAR_HPP

#include <windows.h>
#include <string>
#include "ui_util.hpp"

class Renderer;

// The custom title bar: the three round buttons (close, minimize, maximize) like the macOS ones, the app's name,
// the button that shows or hides the sidebar, dragging the window around, and maximize/restore. The window keeps a
// native sizable frame that it hides, so Windows maximizes, snaps and restores it like any other window. Its bounds
// cover the whole top block, the search row included, so the empty space around the search bar drags the window too.
//
//   o o o  Corpus
//   [▯] [ Search ]                                  the sidebar button sits at the left end of the search row
class TitleBar
{
public:
    // What a click on a button of the search row asks the window to do.
    enum class Action { kNone, kToggleSidebar, kSort, kAddFolder };

    void SetBounds(HWND hWnd, const RECT &bounds);
    void Paint(Renderer &renderer) const;

    // Returns true when the click landed on the title bar (a button or the drag area). A double-click on the drag
    // area maximizes or restores the window. A click on a button of the search row sets 'action'.
    bool OnLButtonDown(POINT pt, bool doubleClick, Action &action);
    // The buttons show their symbols while the pointer is over them. 'pt' is outside the window when it left.
    void OnHover(POINT pt);
    // The buttons turn grey while another window is active, as on macOS.
    void SetActive(bool active);
    void OnTimer(UINT_PTR timer);
    // The label of the icon-only button under 'pt', if there is one, and where that button is.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;
    // The sidebar button is left out while the window is too narrow for a sidebar.
    void ShowSidebarButton(bool shown);
    // A dot on the sidebar button says that a Library view other than All narrows the grid while the sidebar is
    // hidden: 'caution' for Missing, the accent for Untagged. Painting reads it; no redraw is asked for.
    void SetSidebarMark(bool shown, bool caution) { m_mark = shown; m_markCaution = caution; }
    // The sort button shows the key in use ('label', left out when empty) and an arrow for the order. 'width' is
    // the whole button's, from SortButtonWidth.
    void SetSort(const std::wstring &label, bool descending, int width);
    // How wide the sort button is with this label (empty: the arrow alone).
    static int SortButtonWidth(HWND hWnd, Renderer &renderer, const std::wstring &label);
    // The sort and Add folder buttons are left out while the viewer or the settings cover the search row.
    void ShowRowButtons(bool shown);
    // Where the sort button is, for its menu to open under it; empty while it is left out.
    RECT SortButtonRect() const;
    // The room the two buttons take at the right end of the search row, gaps included.
    int RowButtonsWidth() const;

    // The maximize button is under 'pt'. Windows hit-tests it as one, for the snap layouts of Windows 11.
    bool OverMaximizeButton(POINT pt) const;

    // Not named IsMaximized: windowsx.h has a macro with that name.
    bool Maximized() const { return IsZoomed(m_hWnd) != FALSE; }
    // Where a maximized window shows on 'monitor': its work area, less a pixel along each auto-hidden taskbar.
    // 'strips' gets the pixels left free (empty for the other edges).
    static RECT MaximizedArea(HMONITOR monitor, RECT (&strips)[4]);
    // Hides the strips that cover the free pixel along an auto-hidden taskbar, for the viewer's fullscreen.
    void HideEdgeStrips();
    // Shows the strips while the window is maximized and hides them otherwise, after it maximized, restored or the
    // taskbar changed.
    void FitEdgeStrips();

private:
    enum Button { kClose = 0, kMinimize = 1, kMaximize = 2, kButtonCount = 3 };

    int Scale(int value) const;
    RECT ButtonRect(int index) const;
    RECT ButtonsRect() const;
    RECT SidebarButtonRect() const;
    RECT AddButtonRect() const;
    void PaintSidebarButton(Renderer &renderer) const;
    void PaintRowButtons(Renderer &renderer) const;
    void ToggleMaximize();
    void BeginDrag();
    void ShowEdgeStrips(const RECT (&rects)[4]);

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};

    bool m_active = true;
    bool m_hoverButtons = false;
    bool m_sidebarButton = true;
    bool m_mark = false;
    bool m_markCaution = false;
    bool m_rowButtons = true;
    std::wstring m_sortLabel;
    bool m_sortDescending = true;
    int m_sortWidth = 0;
    HoverFade m_hover;  // the button under the pointer: 0 sidebar, 1 sort, 2 Add folder
    HWND m_strips[4] = {};
};

#endif
