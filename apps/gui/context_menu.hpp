#ifndef CONTEXT_MENU_HPP
#define CONTEXT_MENU_HPP

#include <windows.h>
#include "ui_util.hpp"

class Renderer;

// The menu that opens under the pointer on a right click on a file:
//
//   +------------------------------+
//   |      Open                    |
//   |      Show in Explorer        |
//   |      Copy             Ctrl+C |
//   |  --------------------------  |
//   |      Add tag          Ctrl+T |
//   |  (i) Info             Ctrl+I |
//   |      Rename               F2 |
//   |  --------------------------  |
//   |      Remove from library Del |
//   |      Delete from disk        |  the only red row
//   +------------------------------+
//
// The same menu, with other rows, serves a sidebar tag (Rename, Delete tag), the grid's empty room (Add folder) and the sort
// button of the search row (the keys to sort by and the two orders, the ones in use ticked).
//
// It is drawn inside the window like the rest of the GUI. While it is open it takes the mouse and the keyboard:
// a click anywhere closes it, and one on an item also picks that item.
class ContextMenu
{
public:
    enum class Item
    {
        kNone, kOpen, kShowInExplorer, kCopy, kAddTag, kInfo, kRename, kRemove, kDelete, kRenameTag, kDeleteTag, kAddFolder,
        kSortAdded, kSortName, kSortSize, kSortType, kSortAscending, kSortDescending
    };
    enum class Kind { kFile, kTag, kGrid, kSort };

    // Opens the menu with its top left corner at 'pt' (its top right one with 'alignRight'), moved so it stays
    // inside the window.
    void Open(HWND hWnd, POINT pt, Kind kind = Kind::kFile, bool alignRight = false);
    // The sort menu ticks the key and the order in use, and names the two orders for that key ("A to Z").
    void SetSortMarks(Item key, Item order, const wchar_t *ascending, const wchar_t *descending);
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
    Kind m_kind = Kind::kFile;
    Item m_sortKey = Item::kNone;
    Item m_sortOrder = Item::kNone;
    const wchar_t *m_ascending = nullptr;
    const wchar_t *m_descending = nullptr;
    int m_keyboardItem = -1;  // lit by the arrow keys; the pointer takes over when it moves
    HoverFade m_hover;
};

#endif
