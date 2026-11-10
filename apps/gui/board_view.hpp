#ifndef BOARD_VIEW_HPP
#define BOARD_VIEW_HPP

#include <windows.h>
#include "renderer.hpp"
#include "thumbnails.hpp"
#include "ui_util.hpp"
#include "video_player.hpp"
#include "../../include/board.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class file;
class library;

// The kVideoEvent wParam of the board's own player (the viewer's posts 0).
const WPARAM kBoardVideoTag = 1;

// One file of a board as the PNG export needs it: where it sits on the canvas (centre and size in canvas units).
struct BoardExportItem
{
    std::string path;
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    bool picture = false;  // a picture Corpus decodes itself (set by the window); else Windows' thumbnail is used
    bool psd = false;
};

// A change made on a board, for Ctrl+Z: what the items of some files were before and after, by file id (a file that
// was not placed has no 'before'; one taken off has no 'after'), or the board's saved search before and after.
struct BoardEdit
{
    struct Entry
    {
        std::int64_t file = 0;
        bool hadBefore = false, hasAfter = false;
        board_item before, after;
    };
    std::int64_t board = 0;
    std::wstring label;
    std::vector<Entry> items;
    bool query = false;  // the saved search changed instead
    std::string queryBefore, queryAfter;
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
// Videos and songs play where they lie. With the pointer over one, a round play mark shows in its middle (nothing
// covers a tile otherwise); a click on it plays. A video plays inside its tile, in a player of the board's own (a
// libmpv window cut to the parts of the tile nothing lies over), and stays on its current frame when paused or left
// alone. A song plays through the window's player, in the strip, and the board stays.
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
        kFileMenu,  // right click on a file
        kPlayVideo, // a click on a video's play mark: PlayVideo(OpenedFile())
        kPlaySong,  // a click on a song's play mark: OpenedFile()
        kCanvasMenu // right click on the canvas where no file lies
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
    // Called after each change the user made that reached the catalog (a move, a placement, taking files off).
    void SetEditHandler(std::function<void(BoardEdit)> handler) { m_onEdit = std::move(handler); }
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

    // ---- Video in its tile ----
    // Plays 'f' in its tile, or pauses and plays again the one there. The sound goes to 'device' (libmpv's name for
    // it, empty for the system default) at 'volume', never in exclusive mode.
    void PlayVideo(file *f, int volume, const std::string &device);
    void PauseVideo();
    // The video leaves its tile; the tile shows its thumbnail again.
    void StopVideo();
    // A video is in its tile and not paused (it may still be opening).
    bool VideoPlaying() const;
    void OnVideoEvent();
    // Mutes the video in its tile, or lets it sound again ('muted' says which), while it is selected; false when no
    // video is there or it isn't selected. The choice holds for the next videos until Corpus closes.
    bool ToggleVideoMute(bool &muted);
    // Something the window draws is over the board (a sheet, the menu, the viewer): the video's window hides.
    void SetPlayerCovered(bool covered);
    // A part of the canvas the window draws over (the selection bar): cut out of the video's window.
    void SetObstacle(const RECT &rect) { m_obstacle = rect; }
    // The song of the strip, so its mark shows pause while it plays.
    void SetSong(const std::string &path, bool playing);
    // The one selected file, if it is a placed video: Space plays it in its tile.
    file *SelectedVideo() const;
    void ShutdownPlayer() { m_playId = 0; m_player.Shutdown(); }
    // Deletes the saved frames no board points to any more. Off in a second window (SetFrameSweep(false)).
    void SweepFrames();
    void SetFrameSweep(bool on) { m_sweepFrames = on; }

    // "Place all" and "Fit" for the canvas menu (the tray's button and Home do the same).
    void PlaceAllTray() { PlaceAll(); }
    void FitAll() { Fit(); Invalidate(); }
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
        double frame = -1.0;  // a video's paused time on this board (board_item::frame), < 0 for none
    };
    // The picture of a paused frame, read from its PNG once (board_frames.hpp).
    struct FrameBitmap
    {
        double frame = -1.0;
        unsigned generation = 0;
        ComPtr<ID2D1Bitmap> bitmap;  // nullptr: the file is missing or unreadable
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
    RECT PlusRect() const;
    std::vector<TabBox> TabBoxes() const;
    int TabWidth(size_t index) const;
    int TabsWidth() const;
    void ScrollTabIntoView();
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
    // 'label' (null: not undoable, as a pause's frame) names the change for Ctrl+Z.
    void Commit(const std::vector<std::int64_t> &ids, const wchar_t *label = nullptr);
    // What the catalog holds for these files now, to be completed by Publish once the write worked.
    BoardEdit Snapshot(const std::vector<std::int64_t> &ids, const wchar_t *label) const;
    // Hands the change to the window: 'after' are the items written (empty: the files were taken off).
    void Publish(BoardEdit &edit, const std::vector<board_item> &after);
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

    // ---- Play marks and the video ----
    static bool IsVideo(const file *f);
    static bool IsSong(const file *f);
    // The round play mark of a video or song, in the middle of its box; false when the box is too small for one.
    bool MarkBox(const Item &item, D2D1_POINT_2F &center, float &radius) const;
    bool OnMark(const Item &item, POINT pt) const;
    // The mark shows pause: the video in the tile, or the song of the strip, is playing.
    bool MarkPauses(const Item &item) const;
    void PaintMark(Renderer &renderer, D2D1_POINT_2F center, float radius, bool pause, float level, bool hot) const;
    // Pauses or plays the video, letting go of the sound device while it waits.
    void SetVideoPaused(bool paused);
    // Keeps the frame the video was just paused on: its time in the catalog, its picture beside the thumbnails.
    void KeepFrame();
    // The saved picture of a video's paused frame, or nullptr.
    ID2D1Bitmap *FrameOf(Renderer &renderer, const Item &item) const;
    // Puts the video's window over its tile, cut round what lies over it, with its mark as an overlay.
    void SyncPlayer(Renderer &renderer) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    library *m_lib = nullptr;
    std::function<void(BoardEdit)> m_onEdit;
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
    HoverFade m_markFade;   // the play mark of a placed file, by its index in m_items
    bool m_onMark = false;  // the pointer is on that mark

    // The video playing in its tile.
    mutable VideoPlayer m_player;
    std::int64_t m_playId = 0;           // its file, 0 for none
    bool m_covered = false;
    RECT m_obstacle = {};
    bool m_videoFailed = false;
    bool m_pauseWhenReady = false;       // paused while still opening: applied once it is up
    bool m_audioReleased = false;        // the paused video let go of the sound device
    std::string m_songPath;              // the strip's song
    bool m_songPlaying = false;
    // What the video's window was last given, so an unchanged picture costs nothing.
    mutable RECT m_shownBox = {};
    mutable std::vector<RECT> m_shownCuts;
    mutable bool m_shownHidden = true;
    mutable bool m_shapeKnown = false;
    mutable RECT m_shownMark = {};
    mutable int m_shownMarkState = -1;
    mutable std::unordered_map<std::int64_t, FrameBitmap> m_frames;  // by file id
    bool m_sweepFrames = true;
    mutable Thumbnails m_thumbs;
    mutable bool m_thumbsStarted = false;
    mutable bool m_fading = false;
    std::wstring m_thumbFolder;
};

#endif
