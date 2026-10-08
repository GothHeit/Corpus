#ifndef TITLE_BAR_HPP
#define TITLE_BAR_HPP

#include <windows.h>

// The custom title bar: the three round buttons (close, minimize, maximize),
// dragging the window around, and our own maximize/restore (the window has no native frame).
class TitleBar
{
public:
    void SetBounds(HWND hWnd, const RECT &bounds);
    void Paint(HDC dc) const;

    // Returns true when the click landed on the title bar (a button or the drag area).
    bool OnLButtonDown(POINT pt);
    // Returns true while a drag out of the maximized state is pending.
    bool OnMouseMove();
    void OnLButtonUp();

    // Not named IsMaximized: windowsx.h has a macro with that name.
    bool Maximized() const { return m_maximized; }

private:
    enum Button { kClose = 0, kMinimize = 1, kMaximize = 2, kButtonCount = 3 };

    int Scale(int value) const;
    RECT ButtonRect(int index) const;
    void ToggleMaximize();
    void BeginDrag(POINT pt);
    void ContinueMaximizedDrag();

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};

    bool m_maximized = false;
    RECT m_restoreRect = {};

    // Dragging a maximized window first waits for the mouse to move a few pixels,
    // then restores the window under the cursor and hands the drag to Windows.
    bool m_awaitingDragThreshold = false;
    POINT m_dragAnchorScreen = {};
    int m_dragAnchorClientY = 0;
};

#endif
