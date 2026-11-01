#ifndef VIEWER_HPP
#define VIEWER_HPP

#include <windows.h>
#include "renderer.hpp"
#include "image_decoder.hpp"
#include "video_player.hpp"
#include "book_pages.hpp"
#include "book_reader.hpp"
#include "ui_util.hpp"
#include "play_order.hpp"
#include "music_queue.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Posted by the viewer to itself when the overlays over a video have to be drawn again; the window hands it back.
const UINT kViewerOverlays = WM_APP + 5;

// The in-window viewer, like Quick Look: pictures, videos and music (through VideoPlayer) and books (through
// BookReader). The buttons and bars fade out after two seconds of stillness, and so does the pointer; music keeps
// them. Tags show only on Ctrl+T, in a row over the bar with a field to type more. The window does the tagging
// (kAddTag, kRemoveTag) and keeps the reading position (SetReading).
//
// Closed while music plays, the viewer keeps playing it and draws a strip at the bottom of the file area instead
// (StripShown), until it is stopped there or the viewer opens other music. A picture, book or text opened meanwhile
// leaves the music on, and the strip under it (the music is "parked"); a video pauses it and puts it back, from where
// it was, once the video is left.
//
// Nothing the window draws can cover the video's window, so over a video the overlays are drawn into memory and
// handed to libmpv instead (OnOverlays).
//
// Music in the viewer is a Now Playing stage (viewer_stage.cpp): the cover as big as the window allows over its own
// blurred and darkened colors, the title, artist, format and the controls under it, and from 1400 DIPs wide a rail
// with the songs coming up. The cover's window takes the shape of the cover (Viewer::FitCover), here and in the strip.
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
    // The rail of the music viewer shows the thumbnails the grid made: 'coverOf' gives one (nullptr while the file has
    // none) and 'wantCovers' asks for those of the files in the rail. OnCoversReady: some arrived.
    using CoverOf = std::function<ID2D1Bitmap*(Renderer &renderer, const std::string &path)>;
    using WantCovers = std::function<void(const std::vector<std::string> &paths)>;
    void SetCovers(CoverOf coverOf, WantCovers wantCovers);
    void OnCoversReady();
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
    // strip goes on under a picture, book or text, waits for a video, and gives way to other music.
    Action Open(std::vector<std::string> paths, size_t index);
    // Music playing goes on in the strip.
    void Close();
    // The songs among 'paths' join the queue (at its end, or before everything queued with 'next'); returns how many
    // were music. Queued songs play before the rest of the list.
    size_t QueueSongs(const std::vector<std::string> &paths, bool next);
    // The song of a rail row that is only the list carrying on (not queued) under 'pt'; false when there is none.
    bool ListedSongAt(POINT pt, std::string &path) const;
    size_t QueueSize() const { return m_queue.Size(); }
    // Opens the viewer again on the music playing in the strip, without starting it over.
    Action Reopen();

    // The media keys: they drive the video on screen, else the music (in the stage or in the strip).
    enum class Media { kPlayPause, kPlay, kPause, kStop, kNext, kPrevious };
    // A video or music is there for them to drive.
    // Music is loaded, on the stage or in the strip (not a video, which has the player when one is shown).
    bool MusicLoaded() const { return StageShown() || StripShown(); }
    bool MediaActive() const { return Playing() || StripShown(); }
    // Returns kNone, or kStrip when Stop took the strip away.
    Action OnMedia(Media command);
    // What Windows' media controls show.
    struct MediaInfo
    {
        bool active = false;
        bool playing = false;
        bool music = false;
        std::wstring title;
        std::wstring artist;
    };
    MediaInfo MediaState() const;
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
    // The monitor's color profile changed: a still picture on screen is decoded again in the new one.
    void ColorsChanged();
    // The book reader has a page: kPageReady.
    Action OnPageReady();
    // libmpv has news: kVideoEvent.
    Action OnVideoEvent();
    // Draws the overlays over the video again: kViewerOverlays.
    void OnOverlays();
    // The label of the icon-only button under 'pt', if there is one, and where that button is.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;

    // The strip at the bottom of the window while music plays with the viewer closed, in three parts: the cover,
    // name and details (which open the viewer again); shuffle, previous, play, next, repeat and the seek bar; and
    // the volume, the speed, a button to open the viewer and an x that stops the music. A narrow strip keeps only mute and
    // the x of the last part.
    bool StripShown() const { return m_background || (m_parked && !m_displaced && !m_fullscreen); }
    // Under a picture the strip is retracted: the picture takes the whole height, a hairline of progress marks
    // the music at the bottom edge, and the strip slides over the picture while the pointer is at that edge.
    // Books keep their bar at the bottom, so there the strip stays put under them.
    bool StripRetracted() const { return m_open && m_parked && !m_displaced && !m_fullscreen && !m_book; }
    // The strip is there to be seen and used: not retracted, or pulled out over the picture.
    bool StripLive() const { return StripShown() && (!StripRetracted() || m_stripOut); }
    // The strip takes room from the window's contents: not while it is retracted.
    bool StripReserved() const { return StripShown() && !StripRetracted(); }
    int StripHeight() const;
    void SetStripBounds(const RECT &bounds);
    void PaintStrip(Renderer &renderer) const;
    // Returns kNone for a click elsewhere too; 'pt' landed on it if StripContains.
    bool StripContains(POINT pt) const { return StripLive() && PtInRect(&m_stripBounds, pt); }
    Action OnStripClick(POINT pt, bool doubleClick);
    // 'pt' is anywhere in the window, outside the strip too; a seek or volume drag goes on past its edge.
    void OnStripMove(POINT pt);
    // With the viewer open, the strip takes the pointer's moves over it, and while its seek bar or volume slider is dragged;
    // not while the viewer has a drag of its own.
    bool StripTakesMove(POINT pt) const
    {
        return StripLive() && (m_stripSeeking || m_stripVolume ||
            (PtInRect(&m_stripBounds, pt) && !m_dragging && !m_seeking && !m_settingVolume && !m_pageSeeking));
    }
    // The label of the icon-only button of the strip under 'pt'.
    bool StripTipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;

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
        kExpandButton = 150,  // music: the button over the cover's corner that fills the viewer with it
        kCoverArea = 151,     // music: the pointer is over the cover, which brings that button up
        kTagChip = 201,       // the tag row: the x of tag chip 0; kTagChip + i for chip i
    };

    // The libmpv overlays the viewer draws over a video, from the back.
    enum Overlay { kCloseOverlay, kPreviousOverlay, kNextOverlay, kControlsOverlay, kTagsOverlay, kMenuOverlay, kToastOverlay, kExpandOverlay, kOverlayCount };

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

    // Where the strip's parts are. The right part is left out (all its rects empty) when the strip is narrow.
    struct Strip
    {
        RECT cover = {};
        RECT now = {};        // the cover and the text: a click opens the viewer
        RECT name = {};
        RECT detail = {};
        RECT shuffle = {};
        RECT previous = {};
        RECT play = {};
        RECT next = {};
        RECT repeat = {};
        RECT elapsed = {};
        RECT seek = {};       // where a click seeks; the line is drawn in its middle
        RECT duration = {};
        RECT speed = {};      // the label, while the speed isn't 1x
        RECT mute = {};
        RECT volume = {};
        RECT open = {};
        RECT close = {};
        RECT slot = {};       // the square the cover is fitted in; the text beside it goes by this
    };

    // The Now Playing stage: where its parts are. The controls reuse the strip's, which share its painting and clicks.
    struct Stage
    {
        RECT area = {};      // the viewer less the rail
        RECT rail = {};      // empty under kViewerRailMin
        RECT slot = {};      // the square the cover is fitted in
        RECT cover = {};     // the cover's window; the slot when the music has none
        RECT title = {};
        RECT subtitle = {};
        RECT format = {};
        Strip controls;
        RECT list = {};      // the room for the rail's rows
        int rows = 0;        // how many rows to fetch: more than fit, as the headings are lower than songs
        int fit = 0;         // how many songs fit
    };

    // The strip's buttons, for hover fades and tips. They follow kTagChip's range, which the tag row uses.
    enum StripButton
    {
        kStripShuffle = 400,
        kStripPrevious,
        kStripPlay,
        kStripNext,
        kStripRepeat,
        kStripSeek,
        kStripMute,
        kStripVolume,
        kStripOpen,
        kStripClose,
        kStripNow,
        kRailSwitch = 598,  // the "Play on" switch
        kRailClear = 599,
        kRailRow = 600,  // the rail's row i is kRailRow + i
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
    // The player holds the file shown, rather than music parked under a picture.
    bool PlayerShown() const { return !m_parked || m_displaced; }
    bool IsVideo() const { return PlayerShown() && m_video.GetStatus() != VideoPlayer::Status::kNothing; }
    // A video or music is open: the control bar shows.
    bool Playing() const { return PlayerShown() && m_video.GetStatus() == VideoPlayer::Status::kPlaying; }
    // The video's window is up, covering the viewer: the overlays go to libmpv.
    bool VideoShown() const { return Playing() && !m_audio; }
    // Music with a cover: the button in the cover's bottom right corner (hover) that shows the cover filling the viewer,
    // like a picture; Esc or a click puts the stage back. The cover is a libmpv window, so the button is an overlay.
    bool CoverFull() const { return m_coverFull && StageShown() && m_video.HasCover(); }
    bool ExpandShown() const;
    RECT ExpandRect() const;
    void PaintExpandButton(Renderer &renderer, float level) const;
    void SetCoverFull(bool full);
    void OnCoverOverlay();
    // The overlays are drawn into libmpv's window: over a video, and over a music cover.
    bool OverlaysDrawn() const { return VideoShown() || ExpandShown() || m_expandUp; }
    // Music: where its cover goes (the video's window, or the tile for one without), with the name under it.
    RECT CoverRect() const;
    void PaintAudio(Renderer &renderer) const;
    // ---- The Now Playing stage (viewer_stage.cpp) ----
    // The viewer shows music that plays: the stage, whose controls take the place of the control bar.
    bool StageShown() const { return m_open && m_audio && Playing(); }
    Stage LayoutStage() const;
    // 'slot' with the shape of the cover playing: as wide as the slot for wide art, as tall for tall art, centered.
    RECT FitCover(const RECT &slot) const;
    // The cover's window follows the cover's size once libmpv knows it.
    void RefitCover();
    // Reads the cover for the backdrop once libmpv can give it, and keeps what the rail needs of the tags.
    void TakeCover();
    void NoteTags();
    // Makes the backdrop's bitmap when the cover or the viewer's shape changed. Not while painting a frame.
    void PrepareStage(Renderer &renderer);
    void PaintBackdrop(Renderer &renderer) const;
    // Makes the cover's shadow bitmap when the cover's size changed. Not while painting a frame.
    void PrepareShadow(Renderer &renderer, const RECT &cover);
    void PaintRail(Renderer &renderer, const Stage &stage) const;
    void PaintRailAction(Renderer &renderer, const RECT &box, bool cross) const;
    void PaintPlayOn(Renderer &renderer, const RECT &header) const;
    // A row of the rail: the heading of the queue, a queued song, the heading of the list (with its "Play on" switch),
    // a song of the list, or the note that the list has run out.
    struct RailRow
    {
        enum Kind { kQueueHeader, kQueued, kListHeader, kListed, kEnd };
        Kind kind = kListed;
        std::string path;
        size_t at = 0;  // the place in the queue, or the index in the list
    };
    // The rows after the song playing, from 'skip' on, at most 'count': the queue (under its heading), then the heading
    // of the list and the rest of the list in its order, less the songs queued, which play from the queue. 'total' is
    // how many rows there are in all.
    std::vector<RailRow> UpNext(size_t skip, size_t count, size_t &total) const;
    // Whether the list passes over 'path': it is queued, or it already played from the queue.
    bool ListSkips(const std::string &path) const;
    int RailRowHeight(const RailRow &row) const;
    // Where each of 'rows' goes in the rail's list; rows below the list's bottom are cut off by its clip.
    std::vector<RECT> RailRects(const Stage &stage, const std::vector<RailRow> &rows) const;
    // The rows the rail shows now and their rects.
    std::vector<RailRow> RailShown(const Stage &stage, std::vector<RECT> &rects) const;
    RECT RailSwitchRect(const RECT &header) const;
    // How far on the "Play on" switch is now: 0 off, 1 on, between while it slides.
    float PlayOnLevel() const;
    void TogglePlayOn();
    // The place in the list the rest of it carries on from: the song playing, or for a queued song the one the queue
    // interrupted.
    size_t ListCurrent() const { return m_fromQueue ? m_anchor : SongIndex(); }
    // Plays 'path' as a queued song. False when it isn't music Corpus can play.
    bool PlayQueued(const std::string &path);
    // The rail's "Clear", the ✕ of a queued row, and the toggle on the divider.
    RECT RailClearRect(const RECT &header) const;
    // The button at the right end of a row: the \u2715 of a queued one, the + of a listed one.
    RECT RailRemoveRect(const RECT &row) const;
    void SaveQueue() const;
    // A queued row pressed in the rail follows the pointer (OnRailDrag) and lets go (OnRailRelease).
    void OnRailDrag(POINT pt);
    void OnRailRelease();
    void RemoveQueued(size_t at);
    int StageButtonAt(const Stage &stage, POINT pt) const;
    Action OnStageClick(POINT pt, bool doubleClick);
    // The shuffle, previous, play, next, repeat, mute, seek and volume of the strip or the stage; false for others.
    bool PlayerControl(const Strip &strip, int button, POINT pt);
    // A seek or volume drag follows the pointer; true while one is on.
    bool DragControls(const Strip &strip, POINT pt, bool moved);
    // The controls the pointer works: the stage's while it is up, else the strip's.
    Strip PlayerStrip() const { return StageShown() ? LayoutStage().controls : LayoutStrip(); }
    void InvalidateControls() const;
    void PaintSeekTip(Renderer &renderer, const RECT &seek, const RECT &limit, bool hours, float hover) const;
    void PaintMusicTile(Renderer &renderer, const RECT &cover, const std::wstring &badge) const;
    // Music that ended goes on as repeat says: again, to the next music in the play order, or nowhere.
    void PlayNextAudio();
    // Starts the music at 'index' of the list, in the viewer or the strip, whichever it is in.
    void PlayAudioAt(size_t index);
    // Next or previous music in the play order, wrapping round when repeating all; false when there is none.
    bool StepAudio(int delta);
    // The play order is made again for the list, around the music playing.
    void BuildOrder();
    void SetShuffle(bool shuffle);
    // Shuffle and repeat go to the registry as they change.
    void SavePlayback() const;
    // The strip shows play: paused, ended, or nothing playing yet.
    bool StripPaused() const { return m_video.Paused() || m_video.Ended() || m_video.GetStatus() != VideoPlayer::Status::kPlaying; }
    // The music of the strip: its list, the one playing and the order, wherever they are kept.
    const std::vector<std::string> &SongPaths() const { return m_parked ? m_song.paths : m_paths; }
    size_t SongIndex() const { return m_parked ? m_song.index : m_index; }
    std::vector<std::string> &SongPathsRef() { return m_parked ? m_song.paths : m_paths; }
    PlayOrder &SongOrder() { return m_parked ? m_song.order : m_order; }
    const PlayOrder &SongOrder() const { return m_parked ? m_song.order : m_order; }
    // Music playing or loading, in the viewer or the strip.
    bool SongOn() const;
    // Moves the music about to lose its list (a picture is opened, or stepped to) aside, to go on under it.
    void ParkSong();
    // The file shown is other music, which takes its place.
    void DropSong();
    // The file shown is a video, which takes the player: the music waits where it was.
    void DisplaceSong();
    // The file shown is not a video: music that waited for one plays again, in the strip.
    void ResumeSong();
    // Puts the cover of the parked music in the strip, or hides it where the strip is.
    void PlaceSongCover();
    // The viewer shows the music parked, without starting it over.
    Action ShowSong();
    void CycleRepeat();
    // The strip's parts are laid out once for an event or a paint and handed down.
    int StripButtonAt(const Strip &strip, POINT pt) const;
    RECT StripButtonRect(const Strip &strip, int button) const;
    // "FLAC \u00B7 24-bit / 96 kHz \u00B7 5 of 12".
    std::wstring StripDetail() const;
    // Where the cover of the music playing in the strip goes: the square slot, then the cover fitted in it.
    RECT StripCoverSlot() const;
    RECT StripCoverRect() const { return FitCover(StripCoverSlot()); }
    double StripSeekTime(const Strip &strip, int x) const;
    int StripVolumeAt(const Strip &strip, int x) const;
    void PaintStripButton(Renderer &renderer, const Strip &strip, int button) const;
    void PaintStripSlider(Renderer &renderer, const RECT &area, double fraction, float hover) const;
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
    // Music plays on with the viewer closed, in the strip at m_stripBounds. The strip's seek bar and volume slider
    // follow the pointer while its button is down.
    bool m_background = false;
    RECT m_stripBounds = {};
    bool m_stripOut = false;  // the retracted strip is pulled out over the picture
    void UpdateStripOut(POINT pt);
    bool m_stripSeeking = false;
    bool m_stripVolume = false;

    // The order the music plays in, and the switches that shape it (kept between runs).
    PlayOrder m_order;
    // The songs added by hand, whether the music stops once they have played (else the list carries on), and while a
    // queued song plays, the song of the list it came between (m_anchor) so the list resumes from there.
    MusicQueue m_queue;
    bool m_queueStops = false;
    // The "Play on" switch slides over a moment after a click (0 when it isn't).
    ULONGLONG m_switchSince = 0;
    // Songs that played from the queue since the list was set: the list skips them, so a song queued plays once.
    std::unordered_set<std::string> m_queuePlayed;
    bool m_fromQueue = false;
    size_t m_anchor = 0;
    // A press on a queued row: it plays on release, or moves where it is dragged to.
    int m_railPress = -1;
    int m_railDrop = -1;
    POINT m_railPressAt = {};
    bool m_railDragging = false;
    bool m_shuffle = false;
    Repeat m_repeat = Repeat::kOff;

    // Music parked under another file: its list, as the strip reads it, while the viewer's holds the file shown. A
    // video displaces it; it waits then at m_resumeAt, paused or not as it was.
    struct Playlist
    {
        std::vector<std::string> paths;
        size_t index = 0;
        PlayOrder order;
    };
    Playlist m_song;
    bool m_parked = false;
    bool m_displaced = false;
    double m_resumeAt = 0.0;
    double m_resumeSpeed = 1.0;
    bool m_resumePaused = false;

    // The tag row is up, with what is typed in its field and the grey rest of the tag it completes.
    bool m_tagRow = false;
    std::wstring m_tagText;
    std::wstring m_tagSuggestion;
    CompleteTag m_completeTag;

    // The tags of the file shown, sorted, when it is in the library.
    TagsOf m_tagsOf;
    bool m_inLibrary = false;
    std::vector<std::string> m_tags;      // the chips of the tag row: the level names of the file's tags
    std::vector<std::wstring> m_tagTips;  // the paths each chip stands for (empty when it is just its name)
    mutable std::wstring m_tipText;  // the whole path of the tag a tooltip shows
    std::string m_tagToRemove;
    bool m_covered = false;
    bool m_resumeUncovered = false;  // the video played when covered, and plays again after

    // The stage. The cover of the music playing as the backdrop is made of: read once per song, shrunk, then
    // blurred into a bitmap about as wide as 96 pixels that Direct2D stretches over the viewer. The size of the cover
    // the cover's window was fitted to, the rail's scroll in rows, what the rail last asked thumbnails for, and the
    // title and artist of songs already played (the rail shows them for those).
    struct SongTags
    {
        std::wstring title;
        std::wstring artist;
    };
    CoverOf m_coverOf;
    WantCovers m_wantCovers;
    std::string m_coverFor;
    std::vector<BYTE> m_coverPixels;
    UINT m_coverWidth = 0;
    UINT m_coverHeight = 0;
    bool m_coverRead = false;
    int m_coverTries = 0;
    mutable bool m_expandUp = false;  // the expand button's overlay is up in libmpv's window
    bool m_coverFull = false;  // the cover fills the viewer (CoverFull)
    ComPtr<ID2D1Bitmap> m_backdrop;
    unsigned m_backdropGeneration = 0;
    ComPtr<ID2D1Bitmap> m_shadow;  // the cover's shadow (PrepareShadow)
    unsigned m_shadowGeneration = 0;
    int m_shadowWidth = 0;
    int m_shadowHeight = 0;
    int m_shadowReach = 0;
    int m_backdropAspect = 0;  // the viewer's width over its height, in thousandths, when the backdrop was made
    int m_fitWidth = 0;
    int m_fitHeight = 0;
    int m_railScroll = 0;
    std::vector<std::string> m_railWanted;
    std::unordered_map<std::string, SongTags> m_songTags;
};

#endif
