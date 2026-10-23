#ifndef INFO_PANE_HPP
#define INFO_PANE_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "ui_util.hpp"

class file;
class Renderer;

// The pane on the right that tells about the selected files, opened from the context menu or with Ctrl+I:
//
//   Info                        x
//   photo.jpg
//   JPEG file
//   Size      2.4 MB
//   Modified  12 Oct 2026
//   Created   3 Jan 2026
//   Path      C:\Pictures\photo.jpg
//   Tags      [anime] [4k]
//             [+ Add tag]
//
// With one file it shows that file; with several, how many there are, their size together and the tags they
// all have. The size and the dates are read from the disk when the selection changes.
class InfoPane
{
public:
    // The width the pane takes, in physical pixels, or 0 while it is closed. 'available' is the width of the
    // window to the right of the sidebar: the grid keeps a minimum of it.
    int Width(HWND hWnd, int available) const;

    bool IsOpen() const { return m_open; }
    void SetOpen(bool open);

    void SetBounds(HWND hWnd, const RECT &bounds);
    void OnDpiChanged() { m_closeHover.Reset(m_hWnd, kInfoHoverTimer); }
    void Paint(Renderer &renderer) const;

    // Call it whenever the selection or the tags of the selected files change.
    void SetSelection(const std::vector<file*> &files);

    bool Contains(POINT pt) const { return m_open && PtInRect(&m_bounds, pt) != 0; }
    // Returns true when the click landed on the pane; 'close' is set if it hit the close button, 'addTag' if it hit
    // the "Add tag" button under the tags.
    bool OnLButtonDown(POINT pt, bool &close, bool &addTag);
    void OnHover(POINT pt);
    void OnTimer();

private:
    struct Details
    {
        std::wstring name;
        std::wstring kind;       // "JPEG file"
        std::wstring size;
        std::wstring modified;
        std::wstring created;
        std::wstring path;
        std::vector<std::wstring> tags;
        size_t count = 0;
    };

    int Scale(int value) const;
    RECT CloseRect() const;
    void Invalidate() const;

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    Details m_details;
    HoverFade m_closeHover;  // 0 the close button, 1 the Add tag button
    mutable RECT m_addRect = {};  // where Paint drew the Add tag button; empty when there is none
};

#endif
