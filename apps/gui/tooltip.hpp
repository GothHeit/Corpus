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
// The window asks its components what is under the pointer ('anchor' is the control, 'text' its label) and
// tells it here on every pointer move. Clicking, scrolling, typing or leaving the control hides it, and it
// stays hidden until the pointer goes to another control. It is drawn last, over everything.
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
    const wchar_t *m_text = nullptr;
    RECT m_anchor = {};
    bool m_shown = false;
    bool m_dismissed = false;
};

#endif
