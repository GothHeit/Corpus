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
    enum class Action { kNone, kClose, kToggleSidebar, kToggleOpenWith, kToggleBestVideo, kToggleBitPerfect, kOutputPicked, kScalingPicked, kTileSize, kStopWatching, kTagByFolder, kExport, kImport,
        kRestore, kPickLibmpv, kResetLibmpv, kShowShortcuts };

    bool IsOpen() const { return m_open; }
    void Open();
    void Close();
    void SetSidebarShown(bool shown);
    void SetOpenWithOn(bool on);
    void SetPlayback(bool bestVideo, bool bitPerfect);
    // After kOutputPicked: which mode's row (true: bit-perfect on) and the output picked (0 the system default).
    bool PickedExclusive() const { return m_pickedExclusive; }
    size_t PickedOutput() const { return m_pickedOutput; }
    // The scaling of enlarged pictures the list shows (0 Auto, 1 smooth, 2 sharp), and after kScalingPicked the one picked.
    void SetScaling(int mode);
    int Scaling() const { return m_scaling; }
    // The names of the outputs, the system default first, which of them each mode uses, and a line saying whether the
    // sound playing now is bit-perfect (empty for none).
    void SetOutputs(std::vector<std::wstring> names, size_t exclusiveIndex, size_t sharedIndex, std::wstring status);
    // The libmpv-2.dll chosen in Settings, empty for the one built in.
    void SetLibmpvPath(std::wstring path);
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
    RECT PictureRowRect() const;
    RECT PlaybackRowRect(int index) const;
    const std::vector<std::wstring> &PopupList() const;
    size_t PopupChosen() const;
    int PlaybackNoteHeight() const;
    // The list that opens under an output row (or over it when the window is too short below), and its items.
    size_t OutputVisibleItems() const;
    RECT OutputPopupRect() const;
    RECT OutputItemRect(size_t visibleIndex) const;
    void CloseOutputPopup();
    Action PickFromPopup(size_t index);
    bool ScrollOutputTo(int first);
    void PaintOutputPopup(Renderer &renderer) const;
    void PaintOutputRow(Renderer &renderer, const RECT &row, const wchar_t *label, size_t chosen, int hoverId, const std::vector<std::wstring> &names) const;
    void PaintPlaybackNote(Renderer &renderer) const;
    RECT LibmpvResetRect() const;
    void PaintLibmpvRow(Renderer &renderer) const;
    RECT BackupPanelRect() const;
    RECT BackupRowRect(int index) const;
    RECT RestorePanelRect() const;
    RECT RestoreRowRect(size_t index) const;
    RECT RestoreButtonRect(size_t index) const;
    void PaintBackup(Renderer &renderer) const;
    RECT HelpPanelRect() const;
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
    std::vector<std::wstring> m_outputs;
    size_t m_exclusiveOutput = 0;
    size_t m_sharedOutput = 0;
    std::wstring m_bitPerfectStatus;
    int m_popupRow = -1;          // 2 or 3 while an output list is open (the playback row it hangs from), 5 the scaling list
    int m_popupFirst = 0;         // the first item shown
    int m_popupItem = -1;         // the item lit by the pointer or the arrow keys
    int m_scaling = 0;
    bool m_pickedExclusive = false;
    size_t m_pickedOutput = 0;
    std::wstring m_libmpvPath;
    int m_tileSize = 128;
    bool m_dragging = false;
    int m_nameClicks = 0;
    ULONGLONG m_lastNameClick = 0;
    std::vector<std::wstring> m_watched;
    size_t m_stopIndex = 0;
    std::vector<std::wstring> m_backups;
    size_t m_restoreIndex = 0;
    int m_settingsScroll = 0;
    // 130 the shortcuts row, 0 the header button, 1 the sidebar row, 2 the "Open with" row, 3 the size row, 120 + 0/1 the switches, 122 the libmpv row, 124/125 the output rows, 123 the button that goes back to the built-in libmpv, 4 + i the Stop watching button
    // of folder i, 100 + 0/1 the export and import rows, 110 + i the Restore button of backup i
    HoverFade m_hover;

    std::vector<Line> m_lines;
    int m_layoutWidth = -1;  // the column width the lines were laid out for
    int m_contentHeight = 0;
    int m_scroll = 0;
};

#endif
