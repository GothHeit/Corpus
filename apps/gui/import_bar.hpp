#ifndef IMPORT_BAR_HPP
#define IMPORT_BAR_HPP

#include <windows.h>
#include "ui_util.hpp"
#include <string>

class Renderer;

// The status bar at the bottom of the file area, shown only while files are being added and for a few seconds
// after. While an import runs it shows what it is doing, a thin oxblood bar moving along its top edge and a
// Cancel button; then the outcome ("Added 120 files · 3 skipped"), with no button.
//
//   ===========-------------------------------------   the moving bar
//    Adding files · 1,240 found  Pictures\2023  Cancel
class ImportBar
{
public:
    void SetBounds(HWND hWnd, const RECT &bounds);
    void Paint(Renderer &renderer) const;

    // How tall the bar is while visible, in physical pixels.
    int Height() const;
    bool Visible() const { return m_state != State::kHidden; }
    bool Running() const { return m_state == State::kRunning; }

    // 'text' says what the import is doing, 'detail' where it is (a folder, drawn in mono and dimmer).
    void ShowProgress(const std::wstring &text, const std::wstring &detail);
    // An error outcome (nothing saved, a folder that couldn't be read) is drawn in rose.
    void ShowResult(const std::wstring &text, bool error);
    void Hide();

    // Redraws the moving bar. Called by the window's timer while an import runs.
    void Animate();

    // Returns true when the click landed on the bar; 'cancel' is set if it hit the Cancel button.
    bool OnLButtonDown(POINT pt, bool &cancel);
    void OnHover(POINT pt);
    void OnTimer();

private:
    enum class State { kHidden, kRunning, kResult };

    int Scale(int value) const;
    RECT CancelRect() const;
    RECT TrackRect() const;
    void Invalidate() const;

    HWND m_hWnd = nullptr;
    RECT m_bounds = {};

    State m_state = State::kHidden;
    std::wstring m_text;
    std::wstring m_detail;
    bool m_error = false;
    HoverFade m_hoverCancel;
    ULONGLONG m_started = 0;  // when the import started: the moving bar's position comes from it
};

#endif
