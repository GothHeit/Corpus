#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "../../include/library.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include "shell_files.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <cmath>
#include <string>

// Files dragged past the edge of the window leave it: Windows takes the drag over, and they can be dropped in Explorer,
// a chat or any program that takes files from Explorer, as copies. The call returns once they are dropped.
void Window::DragFilesOut()
{
    std::vector<std::string> paths = PathsOf(m_fileGrid.SelectedFiles());
    EndFileDrag();
    ReleaseCapture();
    if (paths.empty())
        return;
    OnHover(kNoPointer);
    m_draggingOut = true;
    shell_files::DragOut(m_hWnd, paths);
    PostMessage(m_hWnd, kDragOutDone, 0, 0);
}

// A click in the sidebar: a tag cycles through required, excluded and off (it keeps its place once clicked, so
// the second click of a double-click cycles it on), a Library view is shown, the foot opens the settings.
bool Window::HandleSidebarPress(POINT pt, bool doubleClick)
{
    std::wstring query = m_searchBar.Query();
    std::optional<SidebarView> view;
    bool openSettings = false;
    if (!m_sidebar.OnLButtonDown(pt, query, view, openSettings))
        return false;
    if (openSettings && !doubleClick)
        OpenSettings();
    else if (view && !doubleClick)
        ShowView(*view);
    else if (query != m_searchBar.Query())
        SetQuery(query);
    return true;
}

// The files a tag dropped at 'pt' goes to: the selection if the file under the pointer is in it, else that file alone.
std::vector<file*> Window::DropFilesAt(POINT pt) const
{
    file *f = m_fileGrid.FileAt(pt);
    if (!f)
        return {};
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (std::find(selected.begin(), selected.end(), f) != selected.end())
        return selected;
    return { f };
}

void Window::EndTagDrag()
{
    m_dragTag.clear();
    m_dragActive = false;
    m_fileGrid.SetDropTargets({});
    InvalidateRect(m_hWnd, nullptr, FALSE);
    ResumeHeldResults();
    OfferFolderTags();
}

void Window::EndFileDrag()
{
    KillTimer(m_hWnd, kDragScrollTimer);
    m_fileDragPending = m_fileDragDeferred = m_fileDragActive = false;
    m_sidebar.SetDropTag(std::string());
    InvalidateRect(m_hWnd, nullptr, FALSE);
    ResumeHeldResults();
    OfferFolderTags();
}

// A press goes to what is on top: a sheet, the menu, the viewer or the settings, else the component under it.
// 'doubleClick': the second press of a double-click. 'keys': the message's MK_ flags.
void Window::OnLButtonDown(POINT pt, WPARAM keys, bool doubleClick)
{
    m_tooltip.Dismiss(m_hWnd);

    // The second click of a double-click on the viewer's close button or a menu row is dropped: it would
    // open the tile now under the pointer.
    bool afterOverlay = doubleClick && m_closedOverlay;
    m_closedOverlay = false;
    if (afterOverlay)
        return;

    if (Sheet *sheet = OpenSheet())
    {
        HandleSheetAction(sheet, sheet->OnLButtonDown(pt, doubleClick));
        return;
    }

    // A click closes the context menu, and picks the item it lands on.
    if (m_contextMenu.IsOpen())
    {
        RunMenuItem(m_contextMenu.OnLButtonDown(pt));
        OnHover(pt);
        // A quick second click would reach the file under the menu as a double-click.
        m_closedOverlay = true;
        return;
    }

    if (m_viewer.IsOpen())
    {
        if (m_viewer.Contains(pt))
        {
            HandleViewerAction(m_viewer.OnLButtonDown(pt, doubleClick));
            m_closedOverlay = !m_viewer.IsOpen();
        }
        else
        {
            TitleBar::Action action = TitleBar::Action::kNone;
            m_titleBar.OnLButtonDown(pt, doubleClick, action);
            HandleTitleAction(action);
        }
        return;
    }

    if (m_settings.IsOpen())
    {
        if (m_settings.Contains(pt))
        {
            HandleSettingsAction(m_settings.OnLButtonDown(pt));
            if (m_settings.Dragging())
                SetCapture(m_hWnd);
        }
        else
        {
            TitleBar::Action action = TitleBar::Action::kNone;
            m_titleBar.OnLButtonDown(pt, doubleClick, action);
            HandleTitleAction(action);
        }
        return;
    }

    // The second click of a double-click on a grid button is dropped: the first one may have filled the grid
    // ("Clear filters"), and it would select or open the file now under the pointer.
    bool afterGridButton = doubleClick && m_pressedGridButton;
    m_pressedGridButton = false;

    if (PressPanes(pt, doubleClick) || afterGridButton)
        return;
    PressGrid(pt, keys, doubleClick);
}

// A press on what surrounds the grid: the text fields, the sidebar, the info pane, the tag strip, the import bar and
// the title bar. False if it missed them all.
bool Window::PressPanes(POINT pt, bool doubleClick)
{
    // Every click goes to the text fields first so they can drop focus when clicked outside. The sidebar's goes
    // first: there is only one caret, and the search bar may be about to take it.
    if (!m_sidebar.Contains(pt))
        m_sidebar.Blur();
    if (m_searchBar.OnLButtonDown(pt))
        return true;
    // A press on a tag waits to learn whether it is a click or the start of a drag onto files.
    if (!m_sidebar.TagNameAt(pt).empty())
    {
        m_dragTag = m_sidebar.TagNameAt(pt);
        m_dragStart = m_dragPoint = pt;
        m_dragDouble = doubleClick;
        m_dragActive = false;
        SetCapture(m_hWnd);
        return true;
    }
    if (HandleSidebarPress(pt, doubleClick))
        return true;
    bool closeInfo = false, addTag = false;
    if (m_infoPane.OnLButtonDown(pt, closeInfo, addTag))
    {
        if (closeInfo && !doubleClick)
            ToggleInfo();
        else if (addTag && !doubleClick)
            OpenTagSheet();
        return true;
    }
    TagEdit edit;
    bool openTagSheet = false;
    if (m_tagPanel.OnLButtonDown(pt, edit, openTagSheet))
    {
        // A chip changes under the cursor once clicked (it goes away, or moves to the tags every file has),
        // so the second click of a double-click would hit another one.
        if (!doubleClick)
        {
            if (openTagSheet)
                OpenTagSheet();
            else
                ApplyTagEdit(edit);
        }
        return true;
    }
    if (m_viewer.StripContains(pt))
    {
        HandleViewerAction(m_viewer.OnStripClick(pt, doubleClick));
        return true;
    }
    bool cancel = false;
    if (m_importBar.OnLButtonDown(pt, cancel))
    {
        if (cancel && !doubleClick)
            CancelImport();
        return true;
    }
    TitleBar::Action titleAction = TitleBar::Action::kNone;
    if (m_titleBar.OnLButtonDown(pt, doubleClick, titleAction))
    {
        HandleTitleAction(titleAction);
        return true;
    }
    return false;
}

// A press on the grid: its buttons, or a file, which it selects or opens (the second press of a double-click), and
// which may start a drag.
void Window::PressGrid(POINT pt, WPARAM keys, bool doubleClick)
{
    FileGrid::Button button = m_fileGrid.ButtonAt(pt);
    if (button != FileGrid::Button::kNone)
    {
        if (doubleClick)
            return;
        m_pressedGridButton = true;
        if (button == FileGrid::Button::kAddFolder)
            ChooseFolders();
        else if (button == FileGrid::Button::kBanner)
            FindMovedFiles();
        else
        {
            // Back to every file, as a filtered view can hold none.
            m_sidebar.SetView(SidebarView::kAll);
            SetQuery(L"");
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        return;
    }
    // A press on a file starts a drag of the selection onto a tag, or out of the window. One on a file that is
    // already selected leaves the selection alone until the release, so that all of it can be dragged.
    bool ctrlDown = (keys & MK_CONTROL) != 0, shiftDown = (keys & MK_SHIFT) != 0;
    file *pressed = !doubleClick && !ctrlDown && !shiftDown ? m_fileGrid.FileAt(pt) : nullptr;
    bool deferred = false;
    if (pressed)
    {
        std::vector<file*> selected = m_fileGrid.SelectedFiles();
        deferred = std::find(selected.begin(), selected.end(), pressed) != selected.end();
    }
    if (!deferred && m_fileGrid.OnLButtonDown(pt, ctrlDown, shiftDown))
        SelectionChanged();
    if (pressed)
    {
        m_fileDragPending = true;
        m_fileDragDeferred = deferred;
        m_fileDragActive = false;
        m_fileDragStart = m_fileDragPoint = pt;
        SetCapture(m_hWnd);
    }

    if (doubleClick)
    {
        if (file *f = m_fileGrid.FileAt(pt))
            ViewFile(f, m_fileGrid.Files());
    }
}

// A right click: the menu of the sidebar's tag under the pointer, or of the files (see OpenContextMenu). A click
// while a menu is open only closes it.
void Window::OnRButtonDown(POINT pt)
{
    m_tooltip.Dismiss(m_hWnd);
    // A drag waits for its drop: Esc is the way out.
    if (Dragging())
        return;
    bool wasOpen = m_contextMenu.IsOpen();
    m_contextMenu.Close();
    if (!wasOpen && !m_viewer.IsOpen() && !m_settings.IsOpen() && !OpenSheet())
    {
        // A tag of the sidebar has a menu of its own.
        std::string overTag = m_sidebar.TagNameAt(pt);
        if (overTag.empty())
            OpenContextMenu(pt);
        else
        {
            m_menuTag = overTag;
            m_searchBar.Blur();
            m_sidebar.Blur();
            OnHover(kNoPointer);
            m_contextMenu.Open(m_hWnd, pt, ContextMenu::Kind::kTag);
            m_contextMenu.OnHover(pt);
        }
    }
}
// The pointer moved: hover, and the drags. False if no one took the move.
bool Window::OnMouseMove(POINT pt)
{
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
        m_trackingMouse = TrackMouseEvent(&track) != FALSE;
    }
    OnHover(pt);
    if (Sheet *sheet = OpenSheet())
    {
        sheet->OnMouseMove(pt);
        return true;
    }
    if (m_settings.Dragging())
    {
        HandleSettingsAction(m_settings.OnMouseMove(pt));
        return true;
    }
    if (!m_dragTag.empty())
    {
        if (!m_dragActive && (abs(pt.x - m_dragStart.x) >= Scale(5) || abs(pt.y - m_dragStart.y) >= Scale(5)))
            m_dragActive = true;
        if (m_dragActive)
        {
            m_dragPoint = pt;
            m_fileGrid.SetDropTargets(DropFilesAt(pt));
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        return true;
    }
    if (m_fileDragPending)
    {
        if (!m_fileDragActive && (abs(pt.x - m_fileDragStart.x) >= Scale(5) || abs(pt.y - m_fileDragStart.y) >= Scale(5)))
            m_fileDragActive = true;
        if (m_fileDragActive)
        {
            // Past the edge of the window, other programs take the files.
            RECT client;
            GetClientRect(m_hWnd, &client);
            if (!PtInRect(&client, pt))
            {
                DragFilesOut();
                return true;
            }
            m_fileDragPoint = pt;
            // Resting at the top or bottom edge of the tags keeps scrolling them.
            if (m_sidebar.DragScroll(pt))
                SetTimer(m_hWnd, kDragScrollTimer, theme::kFrameMs * 2, nullptr);
            else
                KillTimer(m_hWnd, kDragScrollTimer);
            m_sidebar.SetDropTag(m_sidebar.TagNameAt(pt));
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        return true;
    }
    if (!m_viewer.IsOpen() && !m_settings.IsOpen() && m_fileGrid.OnMouseMove(pt))
        return true;
    return false;
}

// A release ends a drag (dropping a tag or files) or the click a pending drag put off.
void Window::OnLButtonUp(POINT pt)
{
    if (!m_dragTag.empty())
    {
        std::string name = m_dragTag;
        bool active = m_dragActive;
        bool doubleClick = m_dragDouble;
        POINT start = m_dragStart;
        std::vector<file*> files = active ? DropFilesAt(pt) : std::vector<file*>();
        EndTagDrag();
        ReleaseCapture();
        if (!active)
            HandleSidebarPress(start, doubleClick);
        else if (!files.empty())
        {
            TagEdit edit;
            edit.kind = TagEdit::kAdd;
            edit.tags.push_back(name);
            ApplyTagEdit(edit, files);
        }
        return;
    }
    if (m_fileDragPending)
    {
        bool active = m_fileDragActive, deferred = m_fileDragDeferred;
        POINT start = m_fileDragStart;
        std::string target = active ? m_sidebar.TagNameAt(pt) : std::string();
        EndFileDrag();
        ReleaseCapture();
        if (!active)
        {
            // It was a click after all: the one the press put off.
            if (deferred && m_fileGrid.OnLButtonDown(start, false, false))
                SelectionChanged();
        }
        else if (!target.empty())
        {
            std::vector<file*> files = m_fileGrid.SelectedFiles();
            TagEdit edit;
            edit.kind = TagEdit::kAdd;
            edit.tags.push_back(target);
            size_t lacking = FilesLacking(m_lib, files, edit.tags);
            NoticeTagged(ApplyTagEdit(edit, files), lacking, edit.tags);
        }
        return;
    }
    if (m_settings.Dragging())
        ReleaseCapture();
    m_settings.OnLButtonUp();
    m_rename.OnLButtonUp();
    m_tagSheet.OnLButtonUp();
    m_fileGrid.OnLButtonUp();
    m_viewer.OnLButtonUp();
}

void Window::OnCaptureChanged()
{
    if (!m_dragTag.empty())
        EndTagDrag();
    if (m_fileDragPending)
        EndFileDrag();
    m_settings.OnCaptureChanged();
    m_rename.OnCaptureChanged();
    m_tagSheet.OnCaptureChanged();
    m_fileGrid.OnCaptureChanged();
    m_viewer.OnCaptureChanged();
}

void Window::OnMouseWheel(POINT pt, int delta)
{
    // Unlike the other mouse messages, the wheel comes with screen coordinates: 'pt' is on the screen.
    ScreenToClient(m_hWnd, &pt);
    m_contextMenu.Close();
    m_tooltip.Dismiss(m_hWnd);
    if (Sheet *sheet = OpenSheet())
    {
        sheet->OnMouseWheel(pt, delta);
        return;
    }
    if (Dragging())
        return;
    if (m_viewer.IsOpen())
        m_viewer.OnMouseWheel(pt, delta);
    else if (m_settings.IsOpen())
        m_settings.OnMouseWheel(delta);
    else if (!m_infoPane.OnMouseWheel(pt, delta) &&
        !m_tagPanel.OnMouseWheel(pt, delta) &&
        !m_sidebar.OnMouseWheel(pt, delta))
        m_fileGrid.OnMouseWheel(delta);
}
