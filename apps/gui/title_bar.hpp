#ifndef TITLE_BAR_HPP
#define TITLE_BAR_HPP

#include <windows.h>
#include "ui_util.hpp"

class Renderer;

// The custom title bar: the three round buttons (close, minimize, maximize) like the macOS ones, the app's name,
// the button that shows or hides the sidebar, dragging the window around, and our own maximize/restore (the
// window has no native frame). Its bounds cover the whole top block, the search row included, so the empty space
// around the search bar drags the window too.
//
//   o o o  Corpus        [▯]   the sidebar button lines up with the right edge of the open sidebar
class TitleBar
{
public:
    void SetBounds(HWND hWnd, const RECT &bounds);
    void Paint(Renderer &renderer) const;

    // Returns true when the click landed on the title bar (a button or the drag area). A double-click on the drag
    // area maximizes or restores the window. A click on the sidebar button sets 'toggleSidebar'.
    bool OnLButtonDown(POINT pt, bool doubleClick, bool &toggleSidebar);
    // Returns true while a drag out of the maximized state is pending.
    bool OnMouseMove();
    void OnLButtonUp();
    // The buttons show their symbols while the pointer is over them. 'pt' is outside the window when it left.
    void OnHover(POINT pt);
    // The buttons turn grey while another window is active, as on macOS.
    void SetActive(bool active);
    void OnTimer();
    // The sidebar button is left out while the window is too narrow for a sidebar.
    void ShowSidebarButton(bool shown);

    // Not named IsMaximized: windowsx.h has a macro with that name.
    bool Maximized() const { return m_maximized; }
    // Fits a maximized window to its monitor again, after the taskbar or the screen changed.
    void FitMaximized();

private:
    enum Button { kClose = 0, kMinimize = 1, kMaximize = 2, kButtonCount = 3 };

    int Scale(int value) const;
    RECT ButtonRect(int index) const;
    RECT ButtonsRect() const;
    RECT SidebarButtonRect() const;
    void PaintSidebarButton(Renderer &renderer) const;
    void ToggleMaximize();
    void BeginDrag(POINT pt);
    void ContinueMaximizedDrag();

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};

    bool m_active = true;
    bool m_hoverButtons = false;
    bool m_sidebarButton = true;
    HoverFade m_sidebarHover;  // 0 while the pointer is over the sidebar button
    bool m_maximized = false;
    RECT m_restoreRect = {};

    // Dragging a maximized window first waits for the mouse to move a few pixels,
    // then restores the window under the cursor and hands the drag to Windows.
    bool m_awaitingDragThreshold = false;
    POINT m_dragAnchorScreen = {};
    int m_dragAnchorClientY = 0;
};

#endif
