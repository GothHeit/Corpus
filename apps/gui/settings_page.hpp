#ifndef SETTINGS_PAGE_HPP
#define SETTINGS_PAGE_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "ui_util.hpp"

class Renderer;

// The settings page: covers everything under the title row, as the viewer does, and scrolls when the window is too
// short. Clicking the name in the About block five times opens the licenses page, which is deliberately not
// advertised.
class SettingsPage
{
public:
    enum class Action { kNone, kClose, kToggleSidebar, kToggleOpenWith, kToggleBestVideo, kToggleBitPerfect, kTileSize, kStopWatching, kTagByFolder, kExport, kImport,
        kRestore };

    bool IsOpen() const { return m_open; }
    void Open();
    void Close();
    void SetSidebarShown(bool shown);
    void SetOpenWithOn(bool on);
    void SetPlayback(bool bestVideo, bool bitPerfect);
    // The thumbnail size (96-DPI px) the slider shows, and the one it was last set to by the user.
    void SetTileSize(int size);
    int TileSize() const { return m_tileSize; }
    void SetWatchedFolders(std::vector<std::wstring> folders);
    // After kStopWatching or kTagByFolder: which folder of the list.
    size_t StopWatchingIndex() const { return m_stopIndex; }
    void SetBackups(std::vector<std::wstring> labels);
    // After kRestore: which backup of the list.
    size_t RestoreIndex() const { return m_restoreIndex; }
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
    int Top() const;
    RECT WatchedPanelRect() const;
    RECT WatchedRowRect(size_t index) const;
    RECT StopButtonRect(size_t index) const;
    RECT TagFolderButtonRect(size_t index) const;
    int WatchedNoteHeight() const;
    int MaxSettingsScroll() const;
    void ScrollSettingsTo(int y);
    RECT OpenWithRowRect() const;
    RECT PlaybackRowRect(int index) const;
    RECT BackupPanelRect() const;
    RECT BackupRowRect(int index) const;
    RECT RestorePanelRect() const;
    RECT RestoreRowRect(size_t index) const;
    RECT RestoreButtonRect(size_t index) const;
    void PaintBackup(Renderer &renderer) const;
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
    void PaintWatched(Renderer &renderer) const;
    void PaintScrollbar(Renderer &renderer, const RECT &area, int content, int scroll, int max) const;
    void PaintHeaderButton(Renderer &renderer, const wchar_t *label) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    bool m_licenses = false;
    bool m_sidebarShown = true;
    bool m_openWithOn = false;
    bool m_bestVideo = true;
    bool m_bitPerfect = false;
    int m_tileSize = 128;
    bool m_dragging = false;
    int m_nameClicks = 0;
    ULONGLONG m_lastNameClick = 0;
    std::vector<std::wstring> m_watched;
    size_t m_stopIndex = 0;
    std::vector<std::wstring> m_backups;
    size_t m_restoreIndex = 0;
    int m_settingsScroll = 0;
    // 0 the header button, 1 the sidebar row, 2 the "Open with" row, 3 the size row, 120 + 0/1 the playback rows, 4 + i the Stop watching button
    // of folder i, 100 + 0/1 the export and import rows, 110 + i the Restore button of backup i
    HoverFade m_hover;

    std::vector<Line> m_lines;
    int m_layoutWidth = -1;  // the column width the lines were laid out for
    int m_contentHeight = 0;
    int m_scroll = 0;
};

#endif
