#ifndef BOARD_VIEW_HPP
#define BOARD_VIEW_HPP

#include <windows.h>
#include "renderer.hpp"
#include "thumbnails.hpp"
#include "ui_util.hpp"
#include "../../include/board.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class file;
class library;

// One file of a board as the PNG export needs it: where it sits on the canvas (centre and size in canvas units).
struct BoardExportItem
{
    std::string path;
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    bool picture = false;  // a picture Corpus decodes itself (set by the window); else Windows' thumbnail is used
    bool psd = false;
};

// The board view: a saved search laid out on a canvas. It takes the place of the file grid while the sidebar's Boards
// row is shown, and is a VIEW of the archive, not a place of its own: the files are the results of the one search
// box, the sidebar's tags still rewrite that box, and the board only remembers where each file was put.
//
//   Spring 25   Fall 24   +                                  Export PNG...   the bar: one tab per board
//   +--------------------------------------------------------------------+
//   |                                                                    |
//   |        [ photo ]      [ photo  ]                                   |  the canvas: drag to pan, wheel to
//   |                  [photo]                                           |  zoom, drag a file to move it, the
//   |                                                                    |  corner handle resizes it
//   +--------------------------------------------------------------------+
//   Not placed  12                                              Place all   the tray: matches nobody placed yet
//   [t] [t] [t] [t] [t] [t] [t] ...                                          (never placed by themselves)
//
// A file the search finds that has no position is in the tray. A placed file the search no longer finds is kept in the
// catalog but not shown, and comes back where it was if the search finds it again. Taking a file off the board (Del)
// returns it to the tray; it never leaves the library.
//
// Only the window's thread calls these. Changes go to the library (library::place_on_board), which writes the catalog
// first; a refusal puts the picture back as it was and sets TakeError().
class BoardView
{
public:
    // What a click asks the window to do, besides what the view does itself.
    enum class Action
    {
        kNone,
        kSwitch,    // a tab: SwitchIndex()
        kNewBoard,  // the "+" tab, or the button of the empty view
        kExport,    // Export PNG
        kTabMenu,   // right click on a tab: SwitchIndex()
        kOpen,      // a double-click on a placed file: OpenedFile()
        kFileMenu   // right click on a file
    };
    enum class Cursor { kArrow, kHand, kMove, kResize, kPan };

    // The thumbnails of the canvas are made at a size of their own (512) and kept in the same folder as the grid's.
    void StartThumbnails(HWND hWnd, const std::wstring &folder);
    void StopThumbnails() { m_thumbs.Stop(); }
    void OnThumbnailsReady();
    void ColorsChanged() { m_thumbs.ColorsChanged(); Invalidate(); }

    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }
    // The room above the tray, where the canvas is: the selection bar floats over its bottom.
    RECT CanvasRect() const;

    void SetLibrary(library *lib) { m_lib = lib; }
    // The boards for the bar, and the one shown (nullptr: none). Keeps the picture of the board you leave and
    // fits or restores the one you enter.
    void SetBoards(const std::vector<board*> &boards, board *current);
    board *Current() const { return m_board; }
    // The files the search finds now, in the grid's order. Placed ones show on the canvas, the others in the tray.
    void SetFiles(const std::vector<file*> &files);
    // Lets go of every file (the library is about to delete them), and with 'boardToo' of the board as well (a reload
    // deletes those too): the window gives the view its board again afterwards.
    void Clear(bool boardToo = false);
    // The paths of the files that are not on disk: drawn faded.
    void SetMissing(const std::unordered_set<std::string> *paths) { m_missing = paths; }
    // Export is running: the button says so and does nothing.
    void SetExporting(bool exporting);
    // The painting is "not a search yet" (no query): hint text in the empty states.
    void SetQueryEmpty(bool empty) { m_queryEmpty = empty; }
    void SetActive(bool active) { m_active = active; Invalidate(); }

    // The files on the canvas, with sizes, bottom to top, for the PNG export.
    std::vector<BoardExportItem> ExportItems() const;

    // The selected files (placed or in the tray), in the order the search lists them.
    std::vector<file*> SelectedFiles() const;
    // The file under the point: a placed one (the topmost) or a tray tile.
    file *FileAt(POINT pt) const;
    bool TakeSelectionChanged() { bool changed = m_selectionChanged; m_selectionChanged = false; return changed; }
    // Selects exactly this file (a click elsewhere, say); an empty path clears.
    void SelectPath(const std::string &path);
    std::wstring TakeError();
    int SwitchIndex() const { return m_actionTab; }
    file *OpenedFile() const { return m_opened; }

    void Paint(Renderer &renderer) const;

    Action OnLButtonDown(POINT pt, bool ctrl, bool shift, bool doubleClick);
    Action OnRButtonDown(POINT pt);
    // True while a drag is running (the window keeps the capture and the pointer).
    bool OnMouseMove(POINT pt);
    void OnLButtonUp(POINT pt);
    void OnCaptureChanged();
    // Wheel with the pointer over the board: zooms the canvas, scrolls the tray or the tabs. Returns true if used.
    bool OnMouseWheel(POINT pt, int delta);
    void OnHover(POINT pt);
    void OnTimer(UINT_PTR timer);
    // Keys of the board: nudge, take off, zoom, order... Returns true if used. The rest (Enter, Space, Ctrl+T...) is
    // the grid's, which works on the same selection.
    bool OnKeyDown(WPARAM key, bool ctrl, bool shift);
    // ] and [ are read as typed characters, not key codes: those depend on the keyboard layout. True if used.
    bool OnChar(wchar_t ch);
    Cursor CursorAt(POINT pt) const;
    // Tab hint text for the pointer over the "+" tab or the export button.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;
    // The point is on "+" or the empty view's button: a press there makes a board of the search text, so the text
    // must not first be saved into the board in view.
    bool NewBoardAt(POINT pt) const;
    bool Dragging() const { return m_mode != Mode::kNone; }

    // Takes the selected placed files off the board (back to the tray). Also what Del does.
    void TakeOff();
    void BringToFront(bool front);
    // The board of the next export must hold at least one file.
    bool HasPlaced() const { return !m_items.empty(); }

private:
    struct Item
    {
        file *f = nullptr;
        std::int64_t id = 0;
        double x = 0.0, y = 0.0, scale = 1.0;
        int z = 0;
    };
    struct Tab
    {
        std::int64_t id = 0;
        std::wstring name;
    };
    struct ViewState
    {
        double cx = 0.0, cy = 0.0, zoom = 1.0;
        bool set = false;
    };
    enum class Mode { kNone, kPan, kMove, kResize, kTrayPress, kTrayDrag };
    struct TabBox
    {
        RECT rect;
        int index;  // into m_tabs; m_tabs.size() for "+"
    };

    int Scale(int value) const;
    float Unit() const { return Scale(4) / 4.0f; }
    void Invalidate() const;

    // ---- Layout ----
    RECT BarRect() const;
    RECT TrayRect() const;
    bool TrayShown() const { return !m_tray.empty(); }
    RECT ExportRect() const;
    RECT TabsArea() const;
    std::vector<TabBox> TabBoxes() const;
    int TabsWidth() const;
    RECT PlaceAllRect() const;
    RECT TrayTileRect(size_t index) const;
    int TrayContentWidth() const;
    int TrayAt(POINT pt) const;
    RECT EmptyButtonRect() const;

    // ---- Canvas ----
    float Zoom() const { return static_cast<float>(m_zoom) * Unit(); }
    D2D1_POINT_2F ToScreen(double wx, double wy) const;
    void ToWorld(POINT pt, double &wx, double &wy) const;
    void SizeOf(const Item &item, double &w, double &h) const;
    D2D1_RECT_F ItemBox(const Item &item) const;
    RECT HandleRect(const Item &item) const;
    int ItemAt(POINT pt) const;
    Item *FindItem(std::int64_t id);
    const Item *FindItem(std::int64_t id) const;
    void Rebuild();
    void SortOrder();
    void Fit();
    void ZoomAt(POINT pt, double factor);
    bool Bounds(double &l, double &t, double &r, double &b) const;
    void RememberView();

    // ---- Changes ----
    void Commit(const std::vector<std::int64_t> &ids);
    void PlaceFiles(const std::vector<file*> &files, double wx, double wy);
    void PlaceAll();
    void PlaceSelectedTray();
    void SetSelection(const std::unordered_set<std::int64_t> &ids);
    void SelectionTouched();
    void Fail();
    std::int64_t IdOf(const file *f) const;
    double NextZ() const;

    // ---- Painting ----
    void PaintBar(Renderer &renderer) const;
    void PaintEmpty(Renderer &renderer, const RECT &canvas) const;
    void PaintTray(Renderer &renderer) const;
    void PaintItem(Renderer &renderer, const Item &item, bool selected) const;
    void PaintPicture(Renderer &renderer, file *f, const D2D1_RECT_F &box, bool fit, float extra) const;
    void PaintButton(Renderer &renderer, const RECT &rect, const wchar_t *label, float hover, bool primary) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    library *m_lib = nullptr;
    board *m_board = nullptr;
    bool m_active = true;
    bool m_queryEmpty = true;
    bool m_exporting = false;

    std::vector<Tab> m_tabs;
    int m_tabScroll = 0;

    std::vector<file*> m_matches;                       // what the search finds
    std::unordered_map<std::int64_t, file*> m_byId;     // the same, by database id
    std::vector<Item> m_items;                          // matches that have a position
    std::vector<size_t> m_order;                        // indexes of m_items, bottom to top
    std::vector<file*> m_tray;                          // matches with none
    mutable std::unordered_map<std::int64_t, double> m_aspect;  // width / height of a file's picture once seen
    std::unordered_set<std::int64_t> m_selected;
    bool m_selectionChanged = false;
    const std::unordered_set<std::string> *m_missing = nullptr;

    double m_cx = 0.0, m_cy = 0.0, m_zoom = 1.0;       // the canvas point in the middle of the canvas, and the zoom
    std::unordered_map<std::int64_t, ViewState> m_views;  // by board id, for the session
    int m_trayScroll = 0;
    bool m_fitPending = false;                          // fit the picture once the files are known

    // The drag in progress.
    Mode m_mode = Mode::kNone;
    POINT m_start = {}, m_point = {};
    double m_startCx = 0.0, m_startCy = 0.0;
    std::unordered_map<std::int64_t, Item> m_before;    // the moved items as they were
    std::int64_t m_pressed = 0;                         // the file the press landed on
    bool m_moved = false;
    bool m_raised = false;                              // the press brought a file to the front
    double m_resizeFrom = 0.0;                          // distance from the centre at the press
    bool m_trayPressed = false;
    bool m_pressedSelected = false;                     // the press was on a file that was already selected
    bool m_pressCtrl = false;

    int m_actionTab = -1;
    file *m_opened = nullptr;
    bool m_failed = false;
    int m_hoverTab = -1;
    int m_hoverTray = -1;
    POINT m_hoverPoint = { -1, -1 };

    HoverFade m_hover;      // the bar's tabs (0..n-1), "+" (n), Export (n+1), and Place all (n+2)
    mutable Thumbnails m_thumbs;
    mutable bool m_thumbsStarted = false;
    mutable bool m_fading = false;
    std::wstring m_thumbFolder;
};

#endif
