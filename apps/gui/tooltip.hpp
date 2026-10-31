#ifndef TOOLTIP_HPP
#define TOOLTIP_HPP

#include <windows.h>
#include <string>
#include "ui_util.hpp"

class Renderer;

// The small label that names an icon-only button once the pointer rested on it for a moment:
//
//   [▯]
//   Sidebar  Ctrl+B
//
// The window tells it what is under the pointer ('anchor' is the control, 'text' its label) on every move.
// Clicking, scrolling, typing or leaving the control hides it until the pointer reaches another control.
class Tooltip
{
public:
    // The control under the pointer is 'anchor' with the label 'text'; a null 'text' means none.
    void Update(HWND hWnd, const wchar_t *text, const RECT &anchor);
    // Hides it until the pointer leaves the control.
    void Dismiss(HWND hWnd);
    void OnTimer(HWND hWnd);
    void Paint(Renderer &renderer) const;

private:
    void Invalidate() const;

    HWND m_hWnd = nullptr;
    std::wstring m_text;
    bool m_hasText = false;
    RECT m_anchor = {};
    bool m_shown = false;
    bool m_dismissed = false;
};

#endif
