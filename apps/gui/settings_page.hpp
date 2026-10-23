#ifndef SETTINGS_PAGE_HPP
#define SETTINGS_PAGE_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "ui_util.hpp"

class Renderer;

// The settings page: covers everything under the title row while it is open, as the viewer does. A column holds
// the few settings that exist (whether the sidebar shows, whether Windows offers Corpus in "Open with") and an About block. Clicking the name in the About
// block five times in a row opens the licenses page, which is deliberately not advertised: it shows the notices
// and license texts of the code inside the .exe, scrolling with the wheel and the keys.
//
//   Settings                               [ Done ]
//   Sidebar
//   +--------------------------------------------+
//   | Show sidebar                  Ctrl+B  [ o] |
//   +--------------------------------------------+
//   Files
//   +--------------------------------------------+
//   | Thumbnail size              128  ---o----- |
//   +--------------------------------------------+
//   Windows
//   +--------------------------------------------+
//   | Show Corpus in "Open with"             [ o] |
//   +--------------------------------------------+
//   About
//   +--------------------------------------------+
//   | Corpus   <- five clicks: Licenses          |
//   | A catalog for your personal files. ...     |
//   +--------------------------------------------+
class SettingsPage
{
public:
    enum class Action { kNone, kClose, kToggleSidebar, kToggleOpenWith, kTileSize };

    bool IsOpen() const { return m_open; }
    void Open();
    void Close();
    // Whether the sidebar shows, for the switch.
    void SetSidebarShown(bool shown);
    // Whether Corpus is in the "Open with" list of Windows, for the switch.
    void SetOpenWithOn(bool on);
    // The thumbnail size (96-DPI px) the slider shows, and the one it was last set to by the user.
    void SetTileSize(int size);
    int TileSize() const { return m_tileSize; }
    bool Dragging() const { return m_dragging; }

    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    bool Contains(POINT pt) const { return m_open && PtInRect(&m_bounds, pt) != 0; }
    void Paint(Renderer &renderer) const;

    Action OnLButtonDown(POINT pt);
    Action OnKeyDown(WPARAM key);
    // While the slider is held: the new size comes back as kTileSize.
    Action OnMouseMove(POINT pt);
    void OnLButtonUp() { m_dragging = false; }
    void OnCaptureChanged() { m_dragging = false; }
    void OnMouseWheel(int delta);
    // 'pt' is outside the window when the pointer left it.
    void OnHover(POINT pt);
    void OnTimer();
    void OnDpiChanged() { m_layoutWidth = -1; }

private:
    // What the licenses page is made of, from top to bottom.
    struct Line
    {
        enum Kind { kIntro, kName, kSummary, kText } kind;
        std::wstring text;
        int top = 0;     // from the top of the content
        int height = 0;
    };

    int Scale(int value) const;
    void Invalidate() const;
    int ColumnLeft() const;
    int ColumnWidth() const;
    RECT HeaderButtonRect() const;
    RECT SidebarRowRect() const;
    RECT TileRowRect() const;
    RECT SliderRect() const;
    bool SetTileFromPoint(POINT pt);
    RECT OpenWithRowRect() const;
    RECT AboutRect() const;
    RECT AboutNameRect() const;
    RECT ListRect() const;
    void Build();
    void LayoutLines();
    int MaxScroll() const;
    void ScrollTo(int y);
    void ShowLicenses(bool shown);

    void PaintSettings(Renderer &renderer) const;
    void PaintLicenses(Renderer &renderer) const;
    void PaintSwitchRow(Renderer &renderer, const RECT &row, const wchar_t *label, const wchar_t *keys, bool on,
        float hover) const;
    void PaintSizeRow(Renderer &renderer, const RECT &row, float hover) const;
    void PaintHeaderButton(Renderer &renderer, const wchar_t *label) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    bool m_licenses = false;
    bool m_sidebarShown = true;
    bool m_openWithOn = false;
    int m_tileSize = 128;
    bool m_dragging = false;
    int m_nameClicks = 0;
    ULONGLONG m_lastNameClick = 0;
    HoverFade m_hover;  // 0 the header button, 1 the sidebar row, 2 the "Open with" row, 3 the size row

    std::vector<Line> m_lines;
    int m_layoutWidth = -1;  // the column width the lines were laid out for
    int m_contentHeight = 0;
    int m_scroll = 0;
};

#endif
