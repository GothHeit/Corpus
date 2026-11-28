#ifndef CONTEXT_MENU_HPP
#define CONTEXT_MENU_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "ui_util.hpp"

class Renderer;

// The menu that opens under the pointer on a right click on a file:
//
//   +------------------------------+
//   |      Open                    |
//   |      Open in another window  |
//   |      Show in Explorer        |
//   |      Copy             Ctrl+C |
//   |  --------------------------  |
//   |      Add tag          Ctrl+T |
//   |  (i) Info             Ctrl+I |
//   |      Rename               F2 |
//   |  --------------------------  |
//   |      Remove from library Del |
//   |      Move to Recycle Bin     |  the only red row
//   +------------------------------+
//
// The same menu, with other rows, serves a sidebar tag, the grid's empty room and the sort button.
// While open it takes the mouse and the keyboard; a click anywhere closes it.
class ContextMenu
{
public:
    enum class Item
    {
        kNone, kOpen, kOpenNewWindow, kOpenDefault, kRotateLeft, kRotateRight, kFlipHorizontal, kFlipVertical, kPlayNow, kRemoveQueued, kPlayNext, kAddToQueue, kShowInExplorer, kCopy, kAddTag, kInfo, kRename, kRemove, kDelete, kRenameTag, kDeleteTag, kTagNotes, kExportTag, kAddFolder,
        kSortAdded, kSortName, kSortSize, kSortType, kSortAscending, kSortDescending,
        kRenameBoard, kExportBoard, kDeleteBoard, kTakeOffBoard, kBringToFront, kSendToBack, kNewBoard, kPlaceAllBoard, kFitBoard,
        kRequireTag, kExcludeTag, kClearTag,
        kViewAll, kViewPictures, kViewVideos, kViewAudio, kViewBooks, kViewUntagged, kViewInbox, kViewBoards, kViewMissing,
        kChipFilter, kChipAddToAll, kChipTakeOff, kChipTakeOffLevel
    };
    // kViewer: the picture or video shown in the viewer; kViewerDefault adds "Open with default program". kQueued: a
    // queued song of the stage's rail.
    enum class Kind { kFile, kTag, kGrid, kSort, kSong, kViewer, kViewerDefault, kQueued, kBoard, kBoardFile, kBoardCanvas, kViews, kViewsMissing,
        kChip };
    // kViews: the Library rows as a menu (the label of the current view in the search row); kViewsMissing adds Missing.
    // kChip: a tag chip of the selection bar or the Info pane; its rows name the tag, so they come from SetChipRows.

    // A row of the chip menu, whose label is made for the chip ("Take off colour, keep oxblood").
    struct Row
    {
        Item item;
        std::wstring label;
        bool ruleAbove = false;
    };
    void SetChipRows(std::vector<Row> rows) { m_rows = std::move(rows); }
    // Measures the labels, so the menu is as wide as its longest row and key hint.
    void SetRenderer(Renderer *renderer) { m_renderer = renderer; }

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
    // One row as drawn: from the fixed tables, or from m_rows for the chip menu.
    struct Line
    {
        Item item;
        const wchar_t *label;
        const wchar_t *hint;    // the shortcut, or nullptr
        bool ruleAbove;
    };
    int RowCount() const;
    Line LineAt(int index) const;
    int MeasureWidth() const;

    RECT Bounds() const;
    RECT ItemRect(int index) const;
    int ItemAt(POINT pt) const;
    int Scale(int value) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    std::vector<Row> m_rows;
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
