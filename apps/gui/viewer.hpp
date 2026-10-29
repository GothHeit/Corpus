#ifndef VIEWER_HPP
#define VIEWER_HPP

#include <windows.h>
#include "renderer.hpp"
#include "image_decoder.hpp"
#include "video_player.hpp"
#include "book_pages.hpp"
#include "book_reader.hpp"
#include "ui_util.hpp"
#include <functional>
#include <string>
#include <vector>

// Posted by the viewer to itself when the overlays over a video have to be drawn again; the window hands it back.
const UINT kViewerOverlays = WM_APP + 5;

// The in-window viewer, like Quick Look: pictures, videos (through VideoPlayer) and books (through BookReader).
// The name and buttons fade out after two seconds of stillness, and so does the pointer. The window does the
// tagging (kAddTag, kRemoveTag) and keeps the reading position (SetReading).
//
// Nothing the window draws can cover the video's window, so over a video the overlays are drawn into memory and
// handed to libmpv instead (OnOverlays).
class Viewer
{
public:
    // What the window has to do after an event.
    enum class Action
    {
        kNone,
        kClose,           // close the viewer
        kFallBack,        // close the viewer and open the file in its default program: Corpus can't show it
        kLayout,          // the viewer covers the whole window now, or no longer: lay the window out again
        kAddTag,          // ask for tags to add to the file shown
        kRemoveTag,       // take TagToRemove() off the file shown
    };

    // Whether the viewer shows the file: a picture Windows can decode, a video libmpv can play, or a book.
    static bool CanShow(const std::string &path)
    {
        return IsDecodableImage(path) || IsPsd(path) || IsPlayableVideo(path) || IsBook(path);
    }

    // Where reading a book stopped: the page, and for an EPUB the place in the text (BookPages::Location), which
    // finds the page again once the pages are laid out at another size.
    struct Bookmark
    {
        size_t page = 0;
        std::string location;
    };

    // 'lastPage' tells where to open a book (the first page for one never read), and 'savePage' keeps the page
    // reached, the page count and the place in the text, once the reader stays on a page for a moment and when
    // the book is left on another page than the one saved.
    using LastPage = std::function<Bookmark(const std::string &path)>;
    using SavePage = std::function<void(const std::string &path, size_t page, size_t count, const std::string &location)>;
    void SetReading(LastPage lastPage, SavePage savePage);

    // 'tagsOf' fills in the tags of a file and returns true, or returns false for a file not in the library, which
    // has no chips. RefreshTags asks again for the file shown, after the library changed.
    using TagsOf = std::function<bool(const std::string &path, std::vector<std::string> &tags)>;
    void SetTags(TagsOf tagsOf);
    void RefreshTags();
    const std::string &TagToRemove() const { return m_tagToRemove; }
    // A sheet is up over the viewer: a video pauses and hides its window, which would cover the sheet, until then.
    void SetCovered(bool covered);

    void Start(HWND hWnd);
    void Stop();
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);

    // Shows paths[index]; the arrows step through 'paths'. Each must be one CanShow takes.
    Action Open(std::vector<std::string> paths, size_t index);
    void Close();
    bool IsOpen() const { return m_open; }
    bool Contains(POINT pt) const { return m_open && PtInRect(&m_bounds, pt); }
    const std::string &CurrentPath() const { return m_paths[m_index]; }
    // The viewer covers the whole window, title bar included, rather than everything under the title row.
    bool Fullscreen() const { return m_open && m_fullscreen; }
    // The pointer hides with the overlays.
    bool CursorHidden() const;

    void Paint(Renderer &renderer);

    // 'repeat': the key is held down.
    Action OnKeyDown(WPARAM key, bool repeat);
    Action OnLButtonDown(POINT pt, bool doubleClick);
    // 'pt' is outside the window when the pointer left it.
    void OnMouseMove(POINT pt);
    void OnLButtonUp();
    void OnCaptureChanged();
    void OnMouseWheel(POINT pt, int delta);
    void OnTimer(UINT_PTR timer);
    // The decoder has something: kImageDecoded.
    Action OnDecoded();
    // The book reader has a page: kPageReady.
    Action OnPageReady();
    // libmpv has news: kVideoEvent.
    Action OnVideoEvent();
    // Draws the overlays over the video again: kViewerOverlays.
    void OnOverlays();
    // The label of the icon-only button under 'pt', if there is one, and where that button is.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;

private:
    // The buttons, which are also the ids of their hover fades. Menu items fade as kMenuItem + their index.
    enum Button
    {
        kCloseButton,
        kPreviousButton,
        kNextButton,
        kPlayButton,
        kMuteButton,
        kSubtitleButton,
        kAudioButton,
        kSpeedButton,
        kFullscreenButton,
        kFitButton,        // the page bar: the whole page, or its width
        kDirectionButton,  // the page bar: which way the pages turn
        kButtonCount,
        kMenuItem = 100,
        kAddTagButton = 200,  // the info panel: adds a tag
        kTagChip = 201,       // the info panel: the x of tag chip 0; kTagChip + i for chip i
    };

    // The libmpv overlays the viewer draws over a video, from the back.
    enum Overlay { kCloseOverlay, kPreviousOverlay, kNextOverlay, kInfoOverlay, kControlsOverlay, kMenuOverlay, kToastOverlay, kOverlayCount };

    enum class Menu { kNone, kSubtitles, kAudio };

    // How a page of a book fits the viewer, before it is zoomed.
    enum class PageFit { kPage, kWidth };

    // Where the parts of the page bar are.
    struct PageBar
    {
        RECT bar = {};
        RECT pages = {};      // "12 / 200"
        RECT slider = {};     // where a click goes to a page; the bar is drawn in its middle
        RECT fit = {};
        RECT direction = {};
    };

    // Where the parts of the control bar are. A narrow window leaves out the volume slider, then the times.
    struct Controls
    {
        RECT bar = {};
        RECT play = {};
        RECT elapsed = {};    // the time played
        RECT seek = {};       // where a click seeks; the bar is drawn in its middle
        RECT duration = {};
        RECT mute = {};
        RECT volume = {};     // the slider; empty when left out
        RECT subtitles = {};  // empty when the video has no subtitles
        RECT audio = {};      // empty when the video has only one audio track
        RECT speed = {};      // the button that cycles the speed
        RECT fullscreen = {};
    };

    // A tag chip of the info panel.
    struct Chip
    {
        RECT rect = {};
        int textWidth = 0;
    };

    // The name and details panel at the bottom, with the tags on a row under the name for a file in the library.
    struct Info
    {
        D2D1_RECT_F panel = {};
        std::wstring name;
        std::wstring detail;  // empty when there's no room
        float nameWidth = 0.0f;
        bool shown = false;
        bool tagRow = false;
        std::vector<Chip> chips;  // the first m_tags, as many as fit
        RECT add = {};            // the add button, last on the row
        std::wstring more;        // "+3" for the tags left out
        RECT moreRect = {};
    };

    int Scale(int value) const;
    void Invalidate() const;
    Action Result(Action action);

    Action Show(size_t index);
    void Step(int delta);
    bool m_stepPaused = false;
    void StartFrame();
    void NextFrame();
    void SetFullscreen(bool fullscreen);
    // Shows the frame decoded or made: a new picture or page, or the one on screen again.
    void TakeFrame(DecodedFrame &frame);

    // ---- Books ----
    bool PageBarShown() const { return m_book && m_pageCount > 0; }
    PageBar LayoutPageBar() const;
    size_t PageAt(int x) const;
    // Asks for page 'page', which comes up showing its top, or its bottom when 'atBottom' (scrolled back to).
    void GoToPage(size_t page, bool atBottom = false);
    // Turns 'delta' pages on: forward is to the left when the pages turn right to left.
    void TurnPage(int delta, bool atBottom = false);
    // The page that 'side' turns to: -1 left, 1 right.
    int SideStep(int side) const { return m_rightToLeft ? -side : side; }
    // Scrolls the page by 'pixels' down (up when negative); false when it is already at that end.
    bool ScrollPage(double pixels);
    void SetPageFit(PageFit fit);
    // 'leaving': the book is closed or stepped past.
    void SaveProgress(bool leaving = false);
    void ViewSize(UINT &width, UINT &height, float &scale) const;
    void PaintPageBar(Renderer &renderer, float level) const;
    void PaintBarButton(Renderer &renderer, int button, float level) const;
    // ---- Video ----
    bool IsVideo() const { return m_video.GetStatus() != VideoPlayer::Status::kNothing; }
    // The video's window is up, covering the viewer: the overlays go to libmpv.
    bool VideoShown() const { return m_video.GetStatus() == VideoPlayer::Status::kPlaying; }
    Controls LayoutControls() const;
    double SeekTime(int x) const;
    int VolumeAt(int x) const;
    std::wstring ControlsState() const;
    RECT MenuRect() const;
    std::vector<std::wstring> MenuItems() const;
    int MenuSelected() const;
    void ChooseMenuItem(int item);
    void PaintControls(Renderer &renderer, float level) const;
    void PaintControlButton(Renderer &renderer, int button, float level) const;
    void PaintSlider(Renderer &renderer, const RECT &area, double fraction, const D2D1_COLOR_F &fill, float level,
        bool fromRight = false) const;
    void PaintMenu(Renderer &renderer, float level) const;
    // Sets the playing speed, and names it over the video for a moment.
    void SetSpeed(double speed);
    // Steps through kSpeeds by 'delta' (clamped at the ends), or by one (wrapping round) when 'wrap'.
    void StepSpeed(int delta, bool wrap = false);
    RECT ToastRect() const;
    void PaintToast(Renderer &renderer, float level) const;

    // Zoom is in pixels of the screen per pixel of the picture. A book's page fits as its PageFit says, and may be
    // enlarged to; it can be zoomed out down to the whole page.
    double FitZoom() const;
    double MinZoom() const;
    double Zoom() const { return m_fit ? FitZoom() : m_zoom; }
    void ZoomAt(POINT pt, double zoom);
    void ZoomToFit();
    void ToggleActualSize(POINT pt);
    void KeepInView();
    D2D1_RECT_F PictureRect() const;
    POINT Center() const;

    float OverlayLevel() const;
    void ShowOverlays();
    void FadeOverlays(float to);

    RECT ButtonRect(int button) const;
    bool ButtonShown(int button) const;
    bool ButtonEnabled(int button) const;
    int ButtonAt(POINT pt) const;
    void PaintButton(Renderer &renderer, int button, float level) const;
    Info LayoutInfo(Renderer &renderer) const;
    void PaintInfo(Renderer &renderer, float level) const;
    void PaintChips(Renderer &renderer, const Info &info, float level) const;
    // The add button or a chip's x under 'pt', or -1; and where one is.
    int TagButtonAt(POINT pt) const;
    RECT TagButtonRect(int button) const;
    void PaintMessage(Renderer &renderer) const;
    void PaintNote(Renderer &renderer) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    ImageDecoder m_decoder;
    VideoPlayer m_video;
    BookReader m_reader;
    bool m_fullscreen = false;
    bool m_relayout = false;  // m_fullscreen changed: the window has to lay itself out again

    bool m_open = false;
    std::vector<std::string> m_paths;
    size_t m_index = 0;
    // Still on the file it was opened with: if Corpus can't show that one, it opens in its default program instead.
    bool m_first = false;

    // The picture: what the decoder said, the frame it made and its bitmap.
    ImageDecoder::Status m_status = ImageDecoder::Status::kNothing;
    bool m_loadingShown = false;
    bool m_reloading = false;  // decoding the picture again because the graphics device was lost
    DecodedFrame m_frame;      // its pixels are kept for an animation only, past the upload
    bool m_upload = false;     // m_frame waits to become the bitmap
    ComPtr<ID2D1Bitmap> m_bitmap;
    unsigned m_bitmapGeneration = 0;

    // An animation shows m_frame while the decoder makes the next one. When it's time for it but it isn't there
    // yet, it is shown as soon as it arrives.
    bool m_animated = false;
    DecodedFrame m_next;
    bool m_hasNext = false;
    bool m_due = false;
    ULONGLONG m_nextDue = 0;  // when the next frame is due, by GetTickCount64; 0 before the first

    // Fitted, or zoomed with the picture's point (m_x, m_y) in the middle of the view.
    bool m_fit = true;
    double m_zoom = 1.0;
    double m_x = 0.0;
    double m_y = 0.0;

    bool m_dragging = false;
    POINT m_dragFrom = {};
    double m_dragX = 0.0;
    double m_dragY = 0.0;

    POINT m_pointer = { -32768, -32768 };
    HoverFade m_hover;  // by button
    float m_overlayFrom = 1.0f;
    float m_overlayTo = 1.0f;
    ULONGLONG m_overlayStart = 0;

    // Over a video: what the controls showed when last drawn, the slider being dragged, the menu that is open, and
    // the pixels of the overlay being made.
    std::wstring m_controlsState;
    bool m_seeking = false;
    bool m_settingVolume = false;
    Menu m_menu = Menu::kNone;
    std::wstring m_toast;  // the speed just set; empty when nothing shows
    std::vector<BYTE> m_layer;
    mutable bool m_overlaysQueued = false;  // a kViewerOverlays is on its way

    // The book shown: the page asked for (m_page) and the one on screen, the page count once known (0 before),
    // how its pages fit and turn, and the progress not yet saved. A page shows its top unless m_pageAtBottom.
    // An EPUB reflows, and its page on screen is at m_location in the text.
    bool m_book = false;
    bool m_reflow = false;
    size_t m_page = 0;
    size_t m_pageShown = 0;
    size_t m_pageSaved = 0;  // the page saved last; -1 until the book's first page is up
    std::string m_locationSaved;  // and its place in the text, for an EPUB
    bool m_adoptPage = false;     // an EPUB was opened or laid out again: the page that arrives next is where it is
    size_t m_pageCount = 0;
    std::string m_location;
    PageFit m_pageFit = PageFit::kPage;
    bool m_rightToLeft = false;
    bool m_pageAtBottom = false;
    bool m_progressDirty = false;
    int m_wheelTurn = 0;      // wheel turned towards a page turn, in WHEEL_DELTA units of 1/120
    bool m_clickTurn = false;  // the button went down near a side: going up there without a drag turns the page
    POINT m_pressedAt = {};
    bool m_pageSeeking = false;  // the page slider follows the pointer
    LastPage m_lastPage;
    SavePage m_savePage;

    // The tags of the file shown, sorted, when it is in the library.
    TagsOf m_tagsOf;
    bool m_inLibrary = false;
    std::vector<std::string> m_tags;
    std::string m_tagToRemove;
    bool m_covered = false;
    bool m_resumeUncovered = false;  // the video played when covered, and plays again after
};

#endif
