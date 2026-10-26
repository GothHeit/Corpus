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
#include <algorithm>
#include <string>

// Ctrl+F and Tab: the keyboard goes to the search, with its text selected so typing replaces it.
void Window::FocusSearch()
{
    m_sidebar.Blur();
    m_searchBar.Focus();
    m_searchBar.Select(0, m_searchBar.Query().size());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// The keyboard leaves the search for the grid. 'toFirstResult' (Enter, Down) selects the first file found, so the
// arrows, Space and Enter go on from there; with no result the search keeps the keyboard.
void Window::LeaveSearch(bool toFirstResult)
{
    if (toFirstResult)
    {
        const std::vector<file*> &files = m_fileGrid.Files();
        if (files.empty())
            return;
        m_fileGrid.SelectPath(files[0]->get_path());
        SelectionChanged();
    }
    m_searchBar.Blur();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// The tag filter takes the keyboard only while the sidebar shows.
bool Window::SidebarFieldReachable() const
{
    return m_sidebarOpen && RoomForSidebar();
}

// Typing goes to the open sheet, else to the focused text field.
void Window::OnChar(wchar_t ch)
{
    if (Sheet *sheet = OpenSheet())
    {
        sheet->OnChar(ch);
        return;
    }
    if (m_viewer.IsOpen() || m_settings.IsOpen() || Dragging())
        return;
    if (m_searchBar.IsFocused() && m_searchBar.OnChar(ch))
        RefreshResults();
    else if (m_sidebar.IsFocused())
        m_sidebar.OnChar(ch);
}

// Keyboard: a sheet, the menu, the viewer or the settings take every key while they are open; otherwise the
// shortcuts, then the focused text field, then the grid. False if no one took the key.
bool Window::OnKeyDown(WPARAM key, LPARAM lParam)
{
    m_tooltip.Dismiss(m_hWnd);
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool repeat = (lParam & 0x40000000) != 0;  // the key was already down: it is held

    // Esc drops a tag or files being dragged.
    if (Dragging())
    {
        if (key == VK_ESCAPE)
        {
            EndTagDrag();
            EndFileDrag();
            ReleaseCapture();
        }
        return true;
    }

    // A sheet takes every key while it is open.
    if (Sheet *sheet = OpenSheet())
    {
        HandleSheetAction(sheet, sheet->OnKeyDown(key, ctrl));
        return true;
    }

    // An open context menu takes every key.
    if (m_contextMenu.IsOpen())
    {
        RunMenuItem(m_contextMenu.OnKeyDown(key));
        return true;
    }

    // The viewer takes every key while it is open, but for Ctrl+Z and Ctrl+Y: a tag taken off in it can be put back.
    if (m_viewer.IsOpen())
    {
        if (ctrl && (key == 'Z' || key == 'Y'))
            Undo(key == 'Y' || shift);
        else
            HandleViewerAction(m_viewer.OnKeyDown(key, repeat));
        return true;
    }

    // Ctrl+comma: the settings, from anywhere. They take every key while they are open.
    if (m_settings.IsOpen())
    {
        if (ctrl && key == VK_OEM_COMMA)
            CloseSettings();
        else if (ctrl && key == 'B')
            ToggleSidebar();
        else
            HandleSettingsAction(m_settings.OnKeyDown(key));
        return true;
    }

    if (ShortcutKey(key, ctrl) || FieldKey(key, ctrl, shift))
        return true;
    return FileKey(key, ctrl, shift, repeat);
}

// The shortcuts that work from anywhere but a sheet, the menu, the viewer and the settings.
bool Window::ShortcutKey(WPARAM key, bool ctrl)
{
    // Ctrl+comma: the settings.
    if (ctrl && key == VK_OEM_COMMA)
    {
        OpenSettings();
        return true;
    }

    // Ctrl+B: hide or show the sidebar, from anywhere.
    if (ctrl && key == 'B')
    {
        ToggleSidebar();
        return true;
    }

    // Ctrl+I: show or hide the info pane, from anywhere.
    if (ctrl && key == 'I')
    {
        ToggleInfo();
        return true;
    }

    // Ctrl+plus and Ctrl+minus: bigger or smaller thumbnails.
    if (ctrl && (key == VK_OEM_PLUS || key == VK_ADD))
    {
        SetTileSize(m_fileGrid.TileSize() + 16);
        return true;
    }
    if (ctrl && (key == VK_OEM_MINUS || key == VK_SUBTRACT))
    {
        SetTileSize(m_fileGrid.TileSize() - 16);
        return true;
    }

    // Ctrl+T: add tags to the selected files, from anywhere.
    if (ctrl && key == 'T')
    {
        OpenTagSheet();
        return true;
    }

    // Ctrl+O: add a folder, from anywhere.
    if (ctrl && key == 'O')
    {
        ChooseFolders();
        return true;
    }

    // Ctrl+F: to the search, its text selected to type over.
    if (ctrl && key == 'F')
    {
        FocusSearch();
        return true;
    }
    return false;
}

// The keys of the search and of the tag filter, while one has the keyboard, and Tab, which goes round them.
bool Window::FieldKey(WPARAM key, bool ctrl, bool shift)
{
    // Tab goes round the search, the grid and the tag filter; Shift+Tab the other way.
    if (m_searchBar.IsFocused())
    {
        // Esc empties the search, then leaves it. Enter and Down go to the first result.
        if (key == VK_ESCAPE)
        {
            if (m_searchBar.Query().empty())
                LeaveSearch(false);
            else
                SetQuery(L"");
        }
        else if (key == VK_RETURN || key == VK_DOWN)
            LeaveSearch(true);
        else if (key == VK_TAB)
        {
            LeaveSearch(false);
            if (shift && SidebarFieldReachable())
                m_sidebar.Focus();
        }
        else if (m_searchBar.OnKeyDown(key, ctrl))
            RefreshResults();
        return true;
    }

    if (m_sidebar.IsFocused())
    {
        if (key == VK_TAB)
        {
            m_sidebar.Blur();
            if (!shift)
                FocusSearch();
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        else
            m_sidebar.OnKeyDown(key, ctrl);
        return true;
    }

    if (key == VK_TAB)
    {
        if (!shift && SidebarFieldReachable())
        {
            m_sidebar.Focus();
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        else
            FocusSearch();
        return true;
    }
    return false;
}

// The keys of the grid: what to do with the selected files, then moving the selection.
bool Window::FileKey(WPARAM key, bool ctrl, bool shift, bool repeat)
{
    // Ctrl+Z takes the last change to tags or the library back; Ctrl+Y (or Ctrl+Shift+Z) makes it again.
    if (ctrl && (key == 'Z' || key == 'Y'))
    {
        Undo(key == 'Y' || shift);
        return true;
    }

    // Shift+F10 and the Menu key: the context menu of the focused file.
    if (key == VK_APPS || (shift && key == VK_F10))
    {
        POINT pt;
        if (m_fileGrid.MenuPoint(pt))
            OpenContextMenu(pt);
        return true;
    }

    // Ctrl+C: the selected files to the clipboard. In a text field it copies the text instead (see above).
    if (ctrl && key == 'C')
    {
        CopySelected();
        return true;
    }

    // Enter opens like a double-click: pictures and videos in the viewer, the rest in their default program.
    if (key == VK_RETURN)
    {
        OpenSelected();
        return true;
    }

    // Space: a quick look at the picture, among the selected files if there are several, or all on screen.
    if (key == VK_SPACE)
    {
        std::vector<file*> selected = m_fileGrid.SelectedFiles();
        file *f = m_fileGrid.FocusedFile();
        if (!f || std::find(selected.begin(), selected.end(), f) == selected.end())
            f = selected.empty() ? nullptr : selected[0];
        if (f && !repeat && Viewer::CanShow(f->get_path()))
        {
            bool inSelection = selected.size() > 1;
            ViewFile(f, inSelection ? selected : m_fileGrid.Files(), inSelection);
        }
        return true;
    }

    // F2 renames: the tag under the pointer in the sidebar, else the file. Del takes files out of the library;
    // Shift+Del also sends them to the Recycle Bin.
    if (key == VK_F2)
    {
        std::string overTag = m_sidebar.TagNameAt(CursorInWindow());
        if (!overTag.empty())
            StartTagRename(overTag);
        else
            StartRename();
        return true;
    }

    if (key == VK_DELETE)
    {
        if (shift)
            DeleteSelectedFiles();
        else
            RemoveSelectedFiles();
        return true;
    }

    if (m_fileGrid.OnKeyDown(key, ctrl, shift))
    {
        SelectionChanged();
        return true;
    }
    return false;
}
