#ifndef CONTEXT_MENU_HPP
#define CONTEXT_MENU_HPP

#include <windows.h>
#include "ui_util.hpp"

class Renderer;

// The menu that opens under the pointer on a right click on a file:
//
//   +------------------------+
//   |  Open                  |
//   |  (i) Info       Ctrl+I |
//   |  --------------------  |
//   |  Remove from library   |
//   +------------------------+
//
// It is drawn inside the window like the rest of the GUI. While it is open it takes the mouse and the keyboard:
// a click anywhere closes it, and one on an item also picks that item.
class ContextMenu
{
public:
    enum class Item { kNone, kOpen, kInfo, kRemove };

    // Opens the menu with its top left corner at 'pt', moved so it stays inside the window.
    void Open(HWND hWnd, POINT pt);
    void Close();
    bool IsOpen() const { return m_open; }

    void Paint(Renderer &renderer) const;
    void OnDpiChanged() { m_hover.Reset(m_hWnd, kMenuHoverTimer); }

    // Returns the item picked: kNone when the click closed the menu on its own.
    Item OnLButtonDown(POINT pt);
    // Up, Down, Enter and Esc. Enter returns the item picked.
    Item OnKeyDown(WPARAM key);
    void OnHover(POINT pt);
    void OnTimer();

private:
    RECT Bounds() const;
    RECT ItemRect(int index) const;
    int ItemAt(POINT pt) const;
    int Scale(int value) const;

    HWND m_hWnd = nullptr;
    bool m_open = false;
    POINT m_origin = {};
    POINT m_pointer = { -1, -1 };
    int m_width = 0;
    int m_keyboardItem = -1;  // lit by the arrow keys; the pointer takes over when it moves
    HoverFade m_hover;
};

#endif
