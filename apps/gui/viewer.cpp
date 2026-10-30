#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <utility>

namespace
{
    bool LoadSetting(const wchar_t *name)
    {
        DWORD value = 0;
        DWORD size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) ==
            ERROR_SUCCESS && value != 0;
    }

    // The volume of videos and music, kept between runs; full when never set.
    const wchar_t *const kVolumeValue = L"Volume";
    const wchar_t *const kMutedValue = L"Muted";

    int LoadVolume()
    {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kVolumeValue, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
            return 100;
        return static_cast<int>(std::min<DWORD>(value, 100));
    }

    void SaveVolume(int volume)
    {
        DWORD value = static_cast<DWORD>(volume);
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kVolumeValue, REG_DWORD, &value, sizeof(value));
    }
}

int Viewer::Scale(int value) const
{
    return ScaleForWindow(m_hWnd, value);
}

// Over a video, the overlays are drawn again too, once for all the changes made meanwhile.
void Viewer::Invalidate() const
{
    if (!m_hWnd || !m_open)
        return;
    InvalidateRect(m_hWnd, &m_bounds, FALSE);
    if (VideoShown() && !m_overlaysQueued)
        m_overlaysQueued = PostMessage(m_hWnd, kViewerOverlays, 0, 0) != FALSE;
}

// What an event made the window do, plus laying it out again if the viewer grew or shrank meanwhile.
Viewer::Action Viewer::Result(Action action)
{
    if (m_relayout && m_open)
    {
        m_relayout = false;
        if (action == Action::kNone)
            return Action::kLayout;
    }
    return action;
}

void Viewer::SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds)
{
    m_hWnd = hWnd;
    m_renderer = &renderer;
    m_bounds = bounds;
    m_video.SetBounds(m_audio ? CoverRect() : bounds);
    if (m_open && m_reflow)
        SetTimer(m_hWnd, kViewerReflowTimer, kReflowDelayMs, nullptr);
    KeepInView();
    Invalidate();
}

void Viewer::SetReading(LastPage lastPage, SavePage savePage)
{
    m_lastPage = std::move(lastPage);
    m_savePage = std::move(savePage);
}

void Viewer::SetTags(TagsOf tagsOf, CompleteTag complete)
{
    m_tagsOf = std::move(tagsOf);
    m_completeTag = std::move(complete);
}

void Viewer::SetCovered(bool covered)
{
    covered = covered && m_open;
    if (covered == m_covered)
        return;
    m_covered = covered;
    // The overlays stay up under the sheet, and the pointer with them.
    ShowOverlays();
    if (covered)
    {
        m_resumeUncovered = VideoShown() && !m_video.Paused();
        if (m_resumeUncovered)
            m_video.TogglePause();
    }
    else if (m_resumeUncovered && VideoShown() && m_video.Paused())
        m_video.TogglePause();
    if (!covered)
        m_resumeUncovered = false;
    m_video.SetCovered(covered);
    Invalidate();
}

void Viewer::Start(HWND hWnd)
{
    m_decoder.Start(hWnd);
    m_reader.Start(hWnd);
    m_pageFit = LoadSetting(kFitWidthValue) ? PageFit::kWidth : PageFit::kPage;
    m_rightToLeft = LoadSetting(kRightToLeftValue);
    // Before libmpv starts: it starts with these.
    m_video.SetVolume(LoadVolume());
    if (LoadSetting(kMutedValue))
        m_video.ToggleMute();
}

// The window closes the viewer, which saves the page reached, before it is destroyed.
void Viewer::Stop()
{
    SaveVolume(m_video.Volume());
    SaveSetting(kMutedValue, m_video.Muted());
    m_decoder.Stop();
    m_reader.Stop();
    m_video.Shutdown();
}

void Viewer::SetFullscreen(bool fullscreen)
{
    if (fullscreen == m_fullscreen)
        return;
    m_fullscreen = fullscreen;
    m_relayout = true;
}

Viewer::Action Viewer::Open(std::vector<std::string> paths, size_t index)
{
    if (paths.empty())
        return Action::kNone;
    SaveProgress(true);
    m_paths = std::move(paths);
    m_open = true;
    m_first = true;
    m_overlayFrom = m_overlayTo = 1.0f;
    bool background = m_background;
    m_background = false;
    KillTimer(m_hWnd, kViewerStripTimer);
    Action action = Show(std::min(index, m_paths.size() - 1));
    // After Show: the music of the strip is stopped or replaced, so its window doesn't show meanwhile.
    if (background)
        m_video.SetCovered(false);
    ShowOverlays();
    return action;
}

// Music that plays, or is about to, goes on in the strip; its window hides.
void Viewer::Close()
{
    if (!m_open)
        return;
    SaveProgress(true);
    m_open = false;
    m_background = m_audio && (Playing() || m_video.GetStatus() == VideoPlayer::Status::kLoading);
    if (m_background)
        SetTimer(m_hWnd, kViewerStripTimer, kStripTimeMs, nullptr);
    m_decoder.Close();
    m_reader.Close();
    m_book = m_reflow = false;
    m_pageCount = 0;
    m_location.clear();
    if (!m_background)
        m_video.Stop();
    m_fullscreen = m_relayout = false;
    m_menu = Menu::kNone;
    m_controlsState.clear();
    if (m_dragging || m_seeking || m_settingVolume || m_pageSeeking)
        ReleaseCapture();
    m_dragging = m_seeking = m_settingVolume = m_pageSeeking = m_clickTurn = false;
    for (UINT_PTR timer : { kViewerIdleTimer, kViewerFadeTimer, kViewerFrameTimer, kViewerLoadingTimer, kViewerVideoTimer,
             kViewerReflowTimer, kViewerToastTimer, kViewerTagTimer })
        KillTimer(m_hWnd, timer);
    m_toast.clear();
    m_hover.Reset(m_hWnd, kViewerHoverTimer);
    m_tagRow = false;
    m_tagText.clear();
    m_tagSuggestion.clear();

    // The strip steps on through them.
    if (!m_background)
    {
        m_paths.clear();
        m_audio = false;
    }
    m_covered = m_resumeUncovered = false;
    m_video.SetCovered(m_background);
    m_inLibrary = false;
    m_tags.clear();
    m_status = ImageDecoder::Status::kNothing;
    m_frame = DecodedFrame();
    m_next = DecodedFrame();
    m_bitmap.Reset();
    m_upload = m_animated = m_hasNext = m_due = m_reloading = m_loadingShown = false;
}

Viewer::Action Viewer::Reopen()
{
    if (!m_background || m_paths.empty())
        return Action::kNone;
    m_background = false;
    KillTimer(m_hWnd, kViewerStripTimer);
    m_open = true;
    m_first = false;
    m_overlayFrom = m_overlayTo = 1.0f;
    RefreshTags();
    m_video.SetBounds(CoverRect());
    m_video.SetCovered(false);
    ShowOverlays();
    Invalidate();
    return Action::kNone;
}

// Starts decoding the file. The picture before stays up until the new one arrives, so stepping through quick
// pictures doesn't flash the backdrop; if it takes a while, it gives way to "Loading". A video starts playing
// instead, once its window is up. A book opens on the page it was left at.
Viewer::Action Viewer::Show(size_t index)
{
    SaveProgress(true);
    m_index = index;
    RefreshTags();
    m_status = ImageDecoder::Status::kNothing;
    m_loadingShown = false;
    m_reloading = false;
    m_animated = m_hasNext = m_due = false;
    m_next = DecodedFrame();
    m_nextDue = 0;
    m_menu = Menu::kNone;
    m_controlsState.clear();
    KillTimer(m_hWnd, kViewerFrameTimer);
    SetTimer(m_hWnd, kViewerLoadingTimer, kLoadingDelayMs, nullptr);

    m_book = IsBook(m_paths[m_index]);
    m_reflow = m_book && IsReflowable(m_paths[m_index]);
    m_audio = !m_book && IsPlayableAudio(m_paths[m_index]);
    m_pageCount = 0;
    m_location.clear();
    KillTimer(m_hWnd, kViewerReflowTimer);
    m_pageAtBottom = false;
    m_wheelTurn = 0;
    if (m_book)
    {
        m_decoder.Close();
        m_video.Stop();
        Bookmark bookmark = m_lastPage ? m_lastPage(m_paths[m_index]) : Bookmark();
        m_page = m_pageShown = bookmark.page;
        m_pageSaved = kNoPage;
        m_adoptPage = true;
        // Fixed pages are made as wide as the screen, so the width of one fits sharp even in fullscreen. An
        // EPUB's fill the view.
        BookReader::Sizes sizes;
        sizes.maxSide = m_renderer ? m_renderer->MaxBitmapSize() : 4096;
        MONITORINFO monitor = { sizeof(monitor) };
        sizes.width = 1920;
        if (GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
            sizes.width = static_cast<UINT>(Width(monitor.rcMonitor));
        sizes.width = std::min(sizes.width, sizes.maxSide);
        ViewSize(sizes.viewWidth, sizes.viewHeight, sizes.scale);
        m_reader.Open(Utf8ToWide(m_paths[m_index]), m_page, bookmark.location, sizes);
        Invalidate();
        return Action::kNone;
    }
    m_reader.Close();

    if (m_audio || IsPlayableVideo(m_paths[m_index]))
    {
        m_decoder.Close();
        m_frame = DecodedFrame();
        m_bitmap.Reset();
        m_upload = false;
        // Music starts playing at once, even stepped to from a paused video.
        bool started = m_video.Open(m_hWnd, m_audio ? CoverRect() : m_bounds, m_paths[m_index], m_stepPaused && !m_audio);
        Invalidate();
        // libmpv couldn't be loaded: the file it was opened with goes to its default program.
        if (!started && m_first)
            return Action::kFallBack;
        return Action::kNone;
    }

    m_video.Stop();
    m_decoder.Open(Utf8ToWide(m_paths[m_index]), m_renderer ? m_renderer->MaxBitmapSize() : 4096);
    Invalidate();
    return Action::kNone;
}

// Moves 'delta' files on, stopping at the first and last.
void Viewer::Step(int delta)
{
    long long target = static_cast<long long>(m_index) + delta;
    target = std::max(0LL, std::min(target, static_cast<long long>(m_paths.size()) - 1));
    if (static_cast<size_t>(target) == m_index)
        return;
    m_first = false;
    // Stepping from a paused video leaves the next one paused.
    m_stepPaused = Playing() && m_video.Paused();
    Show(static_cast<size_t>(target));
    m_stepPaused = false;
    ShowOverlays();
}

float Viewer::OverlayLevel() const
{
    if (!AnimationsEnabled())
        return m_overlayTo;
    float progress = std::min(1.0f, static_cast<float>(GetTickCount64() - m_overlayStart) / theme::kPaneMs);
    return m_overlayFrom + (m_overlayTo - m_overlayFrom) * progress;
}

bool Viewer::CursorHidden() const
{
    return m_open && !m_covered && !m_dragging && !m_seeking && !m_settingVolume && !m_pageSeeking && m_overlayTo == 0.0f &&
        OverlayLevel() == 0.0f;
}

// Shows the name and buttons, and hides them again once the pointer stands still for a while. Over a video, the
// time shown moves on meanwhile.
void Viewer::ShowOverlays()
{
    if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    FadeOverlays(1.0f);
    SetTimer(m_hWnd, kViewerIdleTimer, theme::kOverlayIdleMs, nullptr);
    if (Playing())
    {
        m_video.ReadPosition();
        SetTimer(m_hWnd, kViewerVideoTimer, kVideoTimeMs, nullptr);
    }
}

void Viewer::FadeOverlays(float to)
{
    if (to == m_overlayTo)
        return;
    m_overlayFrom = OverlayLevel();
    m_overlayTo = to;
    m_overlayStart = GetTickCount64();
    if (AnimationsEnabled())
        SetTimer(m_hWnd, kViewerFadeTimer, theme::kFrameMs, nullptr);
    else if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
        SetCursor(nullptr);
    Invalidate();
}

void Viewer::Paint(Renderer &renderer)
{
    if (!m_open)
        return;
    renderer.FillRect(ToRectF(m_bounds), theme::kViewerBackdrop);
    // The video's window covers the viewer, and libmpv draws the overlays on it.
    if (VideoShown())
        return;

    // The graphics device was lost with the bitmap. An animation still has its frame; a still picture had its
    // pixels dropped once uploaded, and is decoded again.
    unsigned generation = renderer.Generation();
    if (m_bitmap && m_bitmapGeneration != generation)
    {
        m_bitmap.Reset();
        if (m_animated)
            m_upload = true;
        else if (m_status == ImageDecoder::Status::kFrame && !m_reloading)
        {
            m_reloading = true;
            if (m_book)
                m_reader.Show(m_pageShown, true);
            else
                m_decoder.Open(Utf8ToWide(m_paths[m_index]), renderer.MaxBitmapSize());
        }
    }

    if (m_upload)
    {
        m_upload = false;
        if (m_bitmap && m_bitmapGeneration == generation && m_bitmap->GetPixelSize().width == m_frame.width &&
            m_bitmap->GetPixelSize().height == m_frame.height)
            m_bitmap->CopyFromMemory(nullptr, m_frame.pixels.data(), m_frame.width * 4);
        else
        {
            m_bitmap = renderer.CreateBitmap(m_frame.width, m_frame.height, m_frame.pixels.data());
            m_bitmapGeneration = generation;
        }
        if (!m_bitmap)
        {
            m_status = ImageDecoder::Status::kFailed;
            m_animated = false;
            KillTimer(m_hWnd, kViewerFrameTimer);
            m_decoder.Close();
        }
        if (!m_animated)
            std::vector<BYTE>().swap(m_frame.pixels);
    }

    renderer.PushClip(m_bounds);
    if (m_audio)
        PaintAudio(renderer);
    else if (m_bitmap)
    {
        D2D1_RECT_F rect = PictureRect();
        // Resampled smoothly while shown smaller than the bitmap is, or while a picture is fitted up to the view.
        renderer.DrawImage(m_bitmap.Get(), rect, rect.right - rect.left < m_frame.width || (m_fit && !m_book));
    }
    else
        PaintMessage(renderer);
    if (m_bitmap && !m_frame.note.empty())
        PaintNote(renderer);

    float level = OverlayLevel();
    if (level > 0.0f)
    {
        for (int button : { kCloseButton, kPreviousButton, kNextButton })
        {
            if (ButtonShown(button))
                PaintButton(renderer, button, level);
        }
        if (ShadeShown())
            PaintShade(renderer, level);
        if (PageBarShown())
            PaintPageBar(renderer, level);
        if (Playing())
            PaintControls(renderer, level);
        if (m_tagRow)
            PaintTagRow(renderer, level);
    }
    if (!m_toast.empty())
        PaintToast(renderer, 1.0f);
    renderer.PopClip();
}

// Why there is no picture: it can't be shown, the file is gone, or it is still being decoded (or opened, for a
// video).
void Viewer::PaintMessage(Renderer &renderer) const
{
    bool failed = m_status == ImageDecoder::Status::kFailed;
    bool missing = m_status == ImageDecoder::Status::kMissing;
    if (IsVideo())
    {
        failed = m_video.GetStatus() == VideoPlayer::Status::kFailed;
        missing = m_video.GetStatus() == VideoPlayer::Status::kMissing;
    }

    const wchar_t *message = nullptr;
    if (failed)
        message = strings::kCantShow;
    else if (missing)
        message = strings::kNotAtPath;
    else if (m_loadingShown)
        message = strings::kLoading;
    if (!message)
        return;

    int middle = Center().y;
    int line = static_cast<int>(renderer.LineHeight(Font::kBody) + 0.5f);
    RECT row = { m_bounds.left, middle - line / 2, m_bounds.right, middle - line / 2 + line };
    renderer.DrawTextLine(message, Font::kBody, row, theme::kText2, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// Says why the picture is not the whole file, in a chip at the bottom.
void Viewer::PaintNote(Renderer &renderer) const
{
    float pad = static_cast<float>(Scale(10));
    float width = renderer.TextWidth(m_frame.note, Font::kCaption) + 2 * pad;
    int line = static_cast<int>(renderer.LineHeight(Font::kCaption) + 0.5f);
    int middle = (m_bounds.left + m_bounds.right) / 2;
    int bottom = m_bounds.bottom - Scale(theme::kViewerMargin);
    D2D1_RECT_F chip = D2D1::RectF(middle - width / 2, static_cast<float>(bottom - line - Scale(8)),
        middle + width / 2, static_cast<float>(bottom));
    renderer.FillRoundedRect(chip, static_cast<float>(Scale(6)), theme::kOverlay);
    RECT text = { static_cast<LONG>(chip.left), static_cast<LONG>(chip.top), static_cast<LONG>(chip.right),
        static_cast<LONG>(chip.bottom) };
    renderer.DrawTextLine(m_frame.note, Font::kCaption, text, theme::kText1, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Viewer::OnTimer(UINT_PTR timer)
{
    switch (timer)
    {
    case kViewerIdleTimer:
        KillTimer(m_hWnd, kViewerIdleTimer);
        // Not while the pointer rests on a button, or drags something, or a sheet or the tag row is up over the
        // viewer, or while a video is paused or has a menu open. Music has nothing to uncover: its bar stays.
        if (m_covered || m_tagRow || m_audio || m_dragging || m_seeking || m_settingVolume || m_pageSeeking || ButtonAt(m_pointer) >= 0 ||
            (VideoShown() && (m_video.Paused() || m_video.Ended() || m_menu != Menu::kNone)))
            SetTimer(m_hWnd, kViewerIdleTimer, theme::kOverlayIdleMs, nullptr);
        else
            FadeOverlays(0.0f);
        break;

    case kViewerFadeTimer:
        Invalidate();
        if (OverlayLevel() == m_overlayTo)
        {
            KillTimer(m_hWnd, kViewerFadeTimer);
            if (CursorHidden() && PtInRect(&m_bounds, m_pointer))
                SetCursor(nullptr);
        }
        break;

    case kViewerHoverTimer:
        m_hover.Step(m_hWnd, kViewerHoverTimer);
        Invalidate();
        break;

    case kViewerFrameTimer:
        KillTimer(m_hWnd, kViewerFrameTimer);
        // Nothing to see while minimized: the animation waits.
        if (IsIconic(m_hWnd))
            SetTimer(m_hWnd, kViewerFrameTimer, kMinimizedFrameMs, nullptr);
        else if (m_hasNext)
            NextFrame();
        else
            m_due = true;
        break;

    case kViewerVideoTimer:
        // Only while the controls show: a video watched with them hidden doesn't wake the window.
        if (!Playing() || (m_overlayTo == 0.0f && OverlayLevel() == 0.0f))
        {
            KillTimer(m_hWnd, kViewerVideoTimer);
            break;
        }
        m_video.ReadPosition();
        if (ControlsState() != m_controlsState)
            Invalidate();
        break;

    case kViewerToastTimer:
        KillTimer(m_hWnd, kViewerToastTimer);
        m_toast.clear();
        Invalidate();
        break;

    case kViewerProgressTimer:
        SaveProgress();
        break;

    // Left alone with nothing typed (and the pointer off it), the tag row goes down again.
    case kViewerTagTimer:
        KillTimer(m_hWnd, kViewerTagTimer);
        if (m_tagRow && m_tagText.empty())
        {
            RECT row = m_renderer ? LayoutTagRow(*m_renderer).row : RECT{};
            if (PtInRect(&row, m_pointer))
                SetTimer(m_hWnd, kViewerTagTimer, kTagIdleMs, nullptr);
            else
                OpenTagRow(false);
        }
        break;

    case kViewerStripTimer:
        if (!m_background)
        {
            KillTimer(m_hWnd, kViewerStripTimer);
            break;
        }
        m_video.ReadPosition();
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
        break;

    case kViewerReflowTimer:
        KillTimer(m_hWnd, kViewerReflowTimer);
        if (m_open && m_reflow)
        {
            UINT width = 0, height = 0;
            float scale = 1.0f;
            ViewSize(width, height, scale);
            m_reader.Resize(width, height, scale);
            m_adoptPage = true;
        }
        break;

    case kViewerLoadingTimer:
        KillTimer(m_hWnd, kViewerLoadingTimer);
        if (IsVideo() ? m_video.GetStatus() == VideoPlayer::Status::kLoading : m_status == ImageDecoder::Status::kNothing)
        {
            m_loadingShown = true;
            m_bitmap.Reset();
            Invalidate();
        }
        break;
    }
}
