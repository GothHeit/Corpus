#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "viewer.hpp"
#include "viewer_shared.hpp"
#include <cmath>

Viewer::Action Viewer::OnKeyDown(WPARAM key, bool repeat)
{
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    // Ctrl+T brings up the tag row, with its field taking the keys, and puts it away again.
    if (key == 'T' && ctrl)
    {
        if (!repeat && m_inLibrary)
            OpenTagRow(!m_tagRow);
        return Action::kNone;
    }

    // The tag row's field: Enter adds what is typed, Tab completes the tag, Backspace takes off a character (a word
    // with Ctrl), Esc puts the row away. Every other key is the field's too, so nothing steps away while typing.
    if (m_tagRow)
    {
        switch (key)
        {
        case VK_ESCAPE:
            if (!repeat)
                OpenTagRow(false);
            break;
        case VK_RETURN:
            if (!repeat && !m_tagText.empty())
                return Action::kAddTag;
            break;
        case VK_TAB:
            if (!m_tagSuggestion.empty())
            {
                m_tagText += m_tagSuggestion + L" ";
                TagTyped();
            }
            break;
        case VK_BACK:
            if (!m_tagText.empty())
            {
                size_t keep = m_tagText.size() - 1;
                if (ctrl)
                {
                    keep = m_tagText.find_last_not_of(L' ');
                    keep = keep == std::wstring::npos ? 0 : m_tagText.find_last_of(L' ', keep);
                    keep = keep == std::wstring::npos ? 0 : keep + 1;
                }
                m_tagText.erase(keep);
                TagTyped();
            }
            break;
        }
        return Action::kNone;
    }

    // Over a video or music: the left and right arrows seek while it plays and step through the files while it is
    // paused (with Ctrl, they always step), up and down set the volume, Space and K play and pause, M mutes, and Esc
    // closes the menu first. Page Up and Page Down still step through the files.
    if (Playing())
    {
        switch (key)
        {
        case VK_ESCAPE:
            if (!repeat && m_menu != Menu::kNone)
            {
                m_menu = Menu::kNone;
                Invalidate();
                return Action::kNone;
            }
            break;
        case VK_LEFT:
        case VK_RIGHT:
            // Paused, the arrows go to the previous and next file; playing, they seek.
            if (m_video.Paused() || ctrl)
            {
                Step(key == VK_LEFT ? -1 : 1);
                return Action::kNone;
            }
            // ShowOverlays reads where the video is first.
            ShowOverlays();
            m_video.Seek(m_video.Position() + (key == VK_LEFT ? -kSeekStepSeconds : kSeekStepSeconds), true);
            Invalidate();
            return Action::kNone;
        case VK_UP:
        case VK_DOWN:
            m_video.SetVolume(m_video.Volume() + (key == VK_UP ? kVolumeStep : -kVolumeStep));
            ShowOverlays();
            Invalidate();
            return Action::kNone;
        case VK_SPACE:
        case 'K':
            if (!repeat)
            {
                m_video.TogglePause();
                ShowOverlays();
                Invalidate();
            }
            return Action::kNone;
        // [ and ] slow down and speed up, Backspace plays at normal speed again. Which virtual key carries the
        // brackets depends on the keyboard layout, so the character of the key says.
        case VK_BACK:
            if (!repeat)
                SetSpeed(1.0);
            return Action::kNone;
        case VK_OEM_4:
        case VK_OEM_5:
        case VK_OEM_6:
        {
            wchar_t ch = static_cast<wchar_t>(LOWORD(MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_CHAR)));
            if (!repeat && (ch == L'[' || ch == L']'))
                StepSpeed(ch == L'[' ? -1 : 1);
            return Action::kNone;
        }
        case 'M':
            if (!repeat)
            {
                m_video.ToggleMute();
                ShowOverlays();
                Invalidate();
            }
            return Action::kNone;
        }
    }

    // In a book: the left and right arrows turn the page that way, up and down scroll it and turn it at its ends,
    // Page Up and Page Down scroll a view at a time, Home and End go to the first and last page, W switches how it
    // fits and R which way the pages turn. With Ctrl, the arrows, Page Up, Page Down, Home and End step through the
    // files instead.
    if (PageBarShown() && !Playing())
    {
        double view = m_bounds.bottom - m_bounds.top;
        switch (key)
        {
        case VK_LEFT:
        case VK_RIGHT:
            if (ctrl)
                Step(key == VK_LEFT ? -1 : 1);
            else
                TurnPage(SideStep(key == VK_LEFT ? -1 : 1));
            return Result(Action::kNone);
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        {
            bool back = key == VK_UP || key == VK_PRIOR;
            if (ctrl && (key == VK_PRIOR || key == VK_NEXT))
                Step(back ? -1 : 1);
            else if (!ScrollPage((back ? -view : view) * (key == VK_UP || key == VK_DOWN ? kArrowScroll : 1.0 - kPageOverlap)))
                TurnPage(back ? -1 : 1, back);
            return Result(Action::kNone);
        }
        // Space reads on like Page Down, and Shift+Space back like Page Up.
        case VK_SPACE:
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                if (!ScrollPage(-view * (1.0 - kPageOverlap)))
                    TurnPage(-1, true);
            }
            else if (!ScrollPage(view * (1.0 - kPageOverlap)))
                TurnPage(1);
            return Result(Action::kNone);
        case VK_HOME:
        case VK_END:
            if (ctrl)
                Step(key == VK_HOME ? -static_cast<int>(m_index) : static_cast<int>(m_paths.size() - 1 - m_index));
            else
                GoToPage(key == VK_HOME ? 0 : m_pageCount - 1);
            return Result(Action::kNone);
        case 'W':
            if (!repeat && !m_reflow)
                SetPageFit(m_pageFit == PageFit::kPage ? PageFit::kWidth : PageFit::kPage);
            return Action::kNone;
        case 'R':
            if (!repeat)
            {
                m_rightToLeft = !m_rightToLeft;
                SaveSetting(kRightToLeftValue, m_rightToLeft);
                Invalidate();
            }
            return Action::kNone;
        }
    }

    switch (key)
    {
    // Esc first leaves the whole window to the rest of Corpus again.
    case VK_ESCAPE:
        if (repeat)
            return Action::kNone;
        if (!m_fullscreen)
            return Action::kClose;
        SetFullscreen(false);
        break;
    // Space closes, like Quick Look; a book takes it to read on (above), even before its first page is up.
    case VK_SPACE:
        return repeat || m_book ? Action::kNone : Action::kClose;
    // Enter opened the viewer, so in it Enter does nothing: it never leaves for the default program.
    case VK_RETURN:
        return Action::kNone;
    case 'F':
        if (!repeat && !m_audio)
            SetFullscreen(!m_fullscreen);
        break;
    case VK_LEFT:
    case VK_UP:
    case VK_PRIOR:
        Step(-1);
        break;
    case VK_RIGHT:
    case VK_DOWN:
    case VK_NEXT:
        Step(1);
        break;
    case VK_HOME:
        Step(-static_cast<int>(m_index));
        break;
    case VK_END:
        Step(static_cast<int>(m_paths.size() - 1 - m_index));
        break;
    case VK_OEM_PLUS:
    case VK_ADD:
        ZoomAt(Center(), Zoom() * kZoomStep);
        break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        ZoomAt(Center(), Zoom() / kZoomStep);
        break;
    case '0':
    case VK_NUMPAD0:
        ZoomToFit();
        break;
    // Z, like a double click in the middle: a picture between fitted and actual size.
    case 'Z':
        if (!repeat && !m_book)
            ToggleActualSize(Center());
        break;
    }
    return Result(Action::kNone);
}

Viewer::Action Viewer::OnLButtonDown(POINT pt, bool doubleClick)
{
    if (!Contains(pt))
        return Action::kNone;

    int button = ButtonAt(pt);
    // A click anywhere but in it closes the open menu, and does nothing else.
    if (m_menu != Menu::kNone && (button < kMenuItem))
    {
        bool onItsButton = (button == kSubtitleButton && m_menu == Menu::kSubtitles) ||
            (button == kAudioButton && m_menu == Menu::kAudio);
        m_menu = Menu::kNone;
        Invalidate();
        if (onItsButton || button < 0)
            return Action::kNone;
    }

    // A double-click would take off the chip that moved under the pointer after the first click.
    if (button >= kTagChip)
    {
        if (doubleClick)
            return Action::kNone;
        m_tagToRemove = m_tags[static_cast<size_t>(button - kTagChip)];
        return Action::kRemoveTag;
    }
    // The rest of the tag row does nothing: its field already has the keys.
    if (m_tagRow && m_renderer)
    {
        RECT row = LayoutTagRow(*m_renderer).row;
        if (PtInRect(&row, pt))
            return Action::kNone;
    }

    switch (button)
    {
    case -1:
        break;
    case kCloseButton:
        return doubleClick ? Action::kNone : Action::kClose;
    case kPreviousButton:
    case kNextButton:
    {
        int side = button == kPreviousButton ? -1 : 1;
        if (m_book)
            TurnPage(SideStep(side));
        else
            Step(side);
        return Result(Action::kNone);
    }
    case kFitButton:
        SetPageFit(m_pageFit == PageFit::kPage ? PageFit::kWidth : PageFit::kPage);
        return Action::kNone;
    case kDirectionButton:
        m_rightToLeft = !m_rightToLeft;
        SaveSetting(kRightToLeftValue, m_rightToLeft);
        Invalidate();
        return Action::kNone;
    case kPlayButton:
        m_video.TogglePause();
        Invalidate();
        return Action::kNone;
    case kMuteButton:
        m_video.ToggleMute();
        Invalidate();
        return Action::kNone;
    case kSubtitleButton:
        if (!doubleClick)
            m_menu = Menu::kSubtitles;
        Invalidate();
        return Action::kNone;
    case kAudioButton:
        if (!doubleClick)
            m_menu = Menu::kAudio;
        Invalidate();
        return Action::kNone;
    // Shift goes back down the speeds.
    case kSpeedButton:
        StepSpeed((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1, true);
        return Action::kNone;
    case kFullscreenButton:
        SetFullscreen(!m_fullscreen);
        return Result(Action::kNone);
    default:
        ChooseMenuItem(button - kMenuItem);
        return Action::kNone;
    }

    if (Playing())
    {
        // The seek bar and the volume slider follow the pointer while the button is down.
        Controls c = LayoutControls();
        if (m_overlayTo > 0.0f && PtInRect(&c.seek, pt) && m_video.Duration() > 0.0)
        {
            m_seeking = true;
            m_video.Seek(SeekTime(pt.x), false);
            SetCapture(m_hWnd);
            Invalidate();
            return Action::kNone;
        }
        if (m_overlayTo > 0.0f && PtInRect(&c.volume, pt))
        {
            m_settingVolume = true;
            m_video.SetVolume(VolumeAt(pt.x));
            SetCapture(m_hWnd);
            Invalidate();
            return Action::kNone;
        }
        if (m_overlayTo > 0.0f && PtInRect(&c.bar, pt))
            return Action::kNone;

        // A click plays or pauses; a double-click covers the window, or leaves it, and undoes its first click.
        m_video.TogglePause();
        if (doubleClick && !m_audio)
            SetFullscreen(!m_fullscreen);
        Invalidate();
        return Result(Action::kNone);
    }

    // A book: the slider goes to the page under the pointer, and follows it while the button is down. A click near
    // either side turns the page that way once the button goes up, unless it dragged; a double-click there turns
    // it again, and one in the middle switches how the page fits.
    if (PageBarShown())
    {
        PageBar b = LayoutPageBar();
        if (m_overlayTo > 0.0f && PtInRect(&b.slider, pt))
        {
            m_pageSeeking = true;
            GoToPage(PageAt(pt.x));
            SetCapture(m_hWnd);
            return Action::kNone;
        }
        if (m_overlayTo > 0.0f && PtInRect(&b.bar, pt))
            return Action::kNone;

        double width = Width(m_bounds);
        int side = pt.x < m_bounds.left + width * kTurnZone ? -1 : pt.x >= m_bounds.right - width * kTurnZone ? 1 : 0;
        if (doubleClick)
        {
            if (side != 0)
                TurnPage(SideStep(side));
            else if (!m_reflow)
                SetPageFit(m_pageFit == PageFit::kPage ? PageFit::kWidth : PageFit::kPage);
            return Action::kNone;
        }
        m_clickTurn = side != 0;
        m_pressedAt = pt;
        if (m_clickTurn && !m_bitmap)
            SetCapture(m_hWnd);
    }

    if (doubleClick)
    {
        ToggleActualSize(pt);
        return Action::kNone;
    }

    if (m_bitmap)
    {
        m_dragging = true;
        m_dragFrom = pt;
        m_dragX = m_x;
        m_dragY = m_y;
        SetCapture(m_hWnd);
    }
    return Action::kNone;
}

void Viewer::OnMouseMove(POINT pt)
{
    bool moved = pt.x != m_pointer.x || pt.y != m_pointer.y;
    m_pointer = pt;
    if (!m_open)
        return;

    if (m_pageSeeking)
    {
        if (moved)
            GoToPage(PageAt(pt.x));
        return;
    }
    // A press that drags no longer turns the page.
    if (m_clickTurn && (std::abs(pt.x - m_pressedAt.x) > Scale(4) || std::abs(pt.y - m_pressedAt.y) > Scale(4)))
        m_clickTurn = false;
    if (m_dragging)
    {
        double zoom = Zoom();
        m_x = m_dragX - (pt.x - m_dragFrom.x) / zoom;
        m_y = m_dragY - (pt.y - m_dragFrom.y) / zoom;
        KeepInView();
        Invalidate();
        return;
    }
    if (m_seeking)
    {
        if (moved)
        {
            m_video.Seek(SeekTime(pt.x), false);
            Invalidate();
        }
        return;
    }
    if (m_settingVolume)
    {
        if (moved)
        {
            m_video.SetVolume(VolumeAt(pt.x));
            Invalidate();
        }
        return;
    }

    if (!PtInRect(&m_bounds, pt))
    {
        if (m_hover.Set(m_hWnd, kViewerHoverTimer, -1))
            Invalidate();
        return;
    }
    // Windows also sends a move when nothing moved (a window closing above, say): that one doesn't count.
    if (moved)
        ShowOverlays();
    int button = ButtonAt(pt);
    if (m_hover.Set(m_hWnd, kViewerHoverTimer, button >= 0 && ButtonEnabled(button) ? button : -1))
        Invalidate();
}

void Viewer::OnLButtonUp()
{
    // The strip's seek bar lands on the very frame let go at, too.
    if (m_stripSeeking)
        m_video.Seek(StripSeekTime(LayoutStrip(), m_pointer.x), true);
    if (m_stripSeeking || m_stripVolume)
    {
        if (GetCapture() == m_hWnd)
            ReleaseCapture();
        m_stripSeeking = m_stripVolume = false;
        InvalidateRect(m_hWnd, &m_stripBounds, FALSE);
    }
    // Lands on the very frame let go at.
    if (m_seeking)
        m_video.Seek(SeekTime(m_pointer.x), true);
    bool turn = m_clickTurn && m_open && m_book;
    m_clickTurn = false;
    if (m_dragging || m_seeking || m_settingVolume || m_pageSeeking || GetCapture() == m_hWnd)
        ReleaseCapture();
    m_dragging = m_seeking = m_settingVolume = m_pageSeeking = false;
    if (turn)
        TurnPage(SideStep(m_pressedAt.x < (m_bounds.left + m_bounds.right) / 2 ? -1 : 1));
}

// A click to turn the page outlives the capture: the grid lets go of the pointer before the viewer hears of the
// button going up.
void Viewer::OnCaptureChanged()
{
    m_stripSeeking = m_stripVolume = false;
    m_dragging = m_seeking = m_settingVolume = m_pageSeeking = false;
}

void Viewer::OnMouseWheel(POINT pt, int delta)
{
    if (!Contains(pt))
        return;
    if (Playing())
    {
        m_video.SetVolume(m_video.Volume() + kVolumeStep * delta / WHEEL_DELTA);
        ShowOverlays();
        Invalidate();
    }
    // A book scrolls the page, and turns it once the wheel goes on past its end by a notch; a page that fits
    // turns at each notch. Ctrl zooms.
    else if (PageBarShown() && (GetKeyState(VK_CONTROL) & 0x8000) == 0)
    {
        double view = m_bounds.bottom - m_bounds.top;
        if (ScrollPage(-view * kWheelScroll * delta / WHEEL_DELTA))
        {
            m_wheelTurn = 0;
            return;
        }
        // Back the other way: start over.
        if ((m_wheelTurn < 0) != (delta < 0))
            m_wheelTurn = 0;
        m_wheelTurn += delta;
        if (std::abs(m_wheelTurn) >= WHEEL_DELTA)
        {
            bool back = m_wheelTurn > 0;
            m_wheelTurn = 0;
            TurnPage(back ? -1 : 1, back);
        }
    }
    else
        ZoomAt(pt, Zoom() * std::pow(kZoomStep, delta / static_cast<double>(WHEEL_DELTA)));
}
