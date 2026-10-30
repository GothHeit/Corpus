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

// The in-window viewer, like Quick Look: pictures, videos and music (through VideoPlayer) and books (through
// BookReader). The buttons and bars fade out after two seconds of stillness, and so does the pointer; music keeps
// them. Tags show only on Ctrl+T, in a row over the bar with a field to type more. The window does the tagging
// (kAddTag, kRemoveTag) and keeps the reading position (SetReading).
//
// Closed while music plays, the viewer keeps playing it and draws a strip at the bottom of the file area instead
// (StripShown), until it is stopped there or the viewer opens something else.
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
        kAddTag,          // add the tags typed in TagText() to the file shown, then ClearTagText()
        kRemoveTag,       // take TagToRemove() off the file shown
        kReopen,          // the strip's name was clicked: open the viewer again with Reopen()
        kStrip,           // the strip came or went: lay the window out again
    };

    // Whether the viewer shows the file: a picture Windows can decode, a video libmpv can play, or a book.
    static bool CanShow(const std::string &path)
    {
        return IsDecodableImage(path) || IsPsd(path) || IsPlayableVideo(path) || IsPlayableAudio(path) || IsBook(path);
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
    // can't be tagged. 'complete' gives the rest of the tag a word being typed starts, or nothing. RefreshTags asks
    // again for the file shown, after the library changed.
    using TagsOf = std::function<bool(const std::string &path, std::vector<std::string> &tags)>;
    using CompleteTag = std::function<std::wstring(const std::wstring &word)>;
    void SetTags(TagsOf tagsOf, CompleteTag complete);
    void RefreshTags();
    const std::string &TagToRemove() const { return m_tagToRemove; }
    // The tag row's field: the window checks each character before it goes in (OnChar).
    bool TagRowOpen() const { return m_open && m_tagRow; }
    const std::wstring &TagText() const { return m_tagText; }
    void ClearTagText();
    void OnChar(wchar_t ch);
    // A sheet is up over the viewer: a video pauses and hides its window, which would cover the sheet, until then.
    void SetCovered(bool covered);

    void Start(HWND hWnd);
    // The playback switches in Settings, kept between runs.
    bool BestVideo() const { return m_bestVideo; }
    bool BitPerfect() const { return m_bitPerfect; }
    void SetBestVideo(bool best);
    void SetBitPerfect(bool on);
    void Stop();
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);

    // Shows paths[index]; the arrows step through 'paths'. Each must be one CanShow takes. Music playing on in the
    // strip stops, unless paths[index] plays in its place.
    Action Open(std::vector<std::string> paths, size_t index);
    // Music playing goes on in the strip.
    void Close();
    // Opens the viewer again on the music playing in the strip, without starting it over.
    Action Reopen();
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

    // The strip at the bottom of the file area while music plays with the viewer closed: play and pause, the name
    // (which opens the viewer again), the time, and an x that stops it.
    bool StripShown() const { return m_background; }
    int StripHeight() const;
    void SetStripBounds(const RECT &bounds) { m_stripBounds = bounds; }
    void PaintStrip(Renderer &renderer) const;
    // Returns kNone for a click elsewhere too; 'pt' landed on it if StripContains.
    bool StripContains(POINT pt) const { return m_background && PtInRect(&m_stripBounds, pt); }
    Action OnStripClick(POINT pt, bool doubleClick);

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
        kTagChip = 201,       // the tag row: the x of tag chip 0; kTagChip + i for chip i
    };

    // The libmpv overlays the viewer draws over a video, from the back.
    enum Overlay { kCloseOverlay, kPreviousOverlay, kNextOverlay, kControlsOverlay, kTagsOverlay, kMenuOverlay, kToastOverlay, kOverlayCount };

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
        RECT speed = {};      // the button that cycles the speed; for music only a label, while not at 1x
        RECT fullscreen = {}; // not for music
    };

    // A tag chip of the tag row.
    struct Chip
    {
        RECT rect = {};
        int textWidth = 0;
    };

    // The row Ctrl+T brings up over the bar: the tags of the file, then the field that adds more.
    struct TagRow
    {
        RECT row = {};
        bool shown = false;
        std::vector<Chip> chips;  // the first m_tags, as many as fit
        std::wstring more;        // "+3" for the tags left out
        RECT moreRect = {};
        RECT field = {};
    };

    // Where the strip's parts are.
    struct Strip
    {
        RECT play = {};
        RECT name = {};
        RECT time = {};
        RECT close = {};
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
    // ---- Video and music ----
    bool IsVideo() const { return m_video.GetStatus() != VideoPlayer::Status::kNothing; }
    // A video or music is open: the control bar shows.
    bool Playing() const { return m_video.GetStatus() == VideoPlayer::Status::kPlaying; }
    // The video's window is up, covering the viewer: the overlays go to libmpv.
    bool VideoShown() const { return Playing() && !m_audio; }
    // Music: where its cover goes (the video's window, or the tile for one without), with the name under it.
    RECT CoverRect() const;
    void PaintAudio(Renderer &renderer) const;
    // Music that ended goes on to the next music among the files, if there is one.
    void PlayNextAudio();
    // The soft shade at the bottom, under the bars and the tag row, and whether anything there needs it.
    RECT ShadeRect() const;
    bool ShadeShown() const;
    void PaintShade(Renderer &renderer, float level) const;
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
    void OpenTagRow(bool open);
    void TagTyped();
    TagRow LayoutTagRow(Renderer &renderer) const;
    void PaintTagRow(Renderer &renderer, float level) const;
    // A chip's x under 'pt', or -1; and where one is.
    int TagButtonAt(POINT pt) const;
    RECT TagButtonRect(int button) const;
    Strip LayoutStrip() const;
    void StopStrip();
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
    bool m_bestVideo = true;
    bool m_bitPerfect = false;
    bool m_pageAtBottom = false;
    bool m_progressDirty = false;
    int m_wheelTurn = 0;      // wheel turned towards a page turn, in WHEEL_DELTA units of 1/120
    bool m_clickTurn = false;  // the button went down near a side: going up there without a drag turns the page
    POINT m_pressedAt = {};
    bool m_pageSeeking = false;  // the page slider follows the pointer
    LastPage m_lastPage;
    SavePage m_savePage;

    // The file shown is music (its cover and name in the middle, the bar always up).
    bool m_audio = false;
    // Music plays on with the viewer closed, in the strip at m_stripBounds.
    bool m_background = false;
    RECT m_stripBounds = {};

    // The tag row is up, with what is typed in its field and the grey rest of the tag it completes.
    bool m_tagRow = false;
    std::wstring m_tagText;
    std::wstring m_tagSuggestion;
    CompleteTag m_completeTag;

    // The tags of the file shown, sorted, when it is in the library.
    TagsOf m_tagsOf;
    bool m_inLibrary = false;
    std::vector<std::string> m_tags;
    std::string m_tagToRemove;
    bool m_covered = false;
    bool m_resumeUncovered = false;  // the video played when covered, and plays again after
};

#endif
