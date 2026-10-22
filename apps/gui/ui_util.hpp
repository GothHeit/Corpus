#ifndef UI_UTIL_HPP
#define UI_UTIL_HPP

#include <windows.h>
#include <d2d1.h>
#include <string>

// Converts a size in 96-DPI pixels to the DPI of the monitor the window is on.
int ScaleForWindow(HWND hWnd, int value);

// Windows 11 rounds the corners of our frameless window through DWM; maximized windows shouldn't be rounded.
void SetRoundedCorners(HWND hWnd, bool rounded);

// The same rectangle, for Direct2D.
D2D1_RECT_F ToRectF(const RECT &rect);

std::wstring Utf8ToWide(const std::string &s);
std::string WideToUtf8(const std::wstring &s);

// A count with the user's thousands separator: 1,240 (or 1.240, depending on the region set in Windows).
std::wstring FormatCount(size_t count);

// The window's timers. Each component that animates has its own, so they start and stop independently.
enum Timer : UINT_PTR
{
    kImportTimer = 1,   // redraws the import bar while files are added
    kResultTimer,       // hides the import outcome
    kGridHoverTimer,
    kGridButtonTimer,
    kChipHoverTimer,
    kCancelHoverTimer,
};

// False when "Animation effects" are off in the Windows settings: things then change at once.
bool AnimationsEnabled();

// 'color' with its opacity multiplied by 'level' (0 to 1).
D2D1_COLOR_F Faded(const D2D1_COLOR_F &color, float level);
// From 'from' at 0 to 'to' at 1.
D2D1_COLOR_F Mix(const D2D1_COLOR_F &from, const D2D1_COLOR_F &to, float level);

// The hover fill of what is under the pointer fades in, and that of what it just left fades out, as on macOS.
// Things are told apart by an id (a tile's index, say), -1 being nothing. While a fade runs, a window timer
// fires every frame: the component redraws on it and calls Step, which stops the timer once the fade is done, so
// an idle window uses no CPU.
class HoverFade
{
public:
    // The thing under the pointer is now 'id'. Returns true if that changed.
    bool Set(HWND hWnd, UINT_PTR timer, int id);
    // Forgets both things at once, with no fade: the ids no longer mean the same things.
    void Reset(HWND hWnd, UINT_PTR timer);
    void Step(HWND hWnd, UINT_PTR timer);

    int Current() const { return m_current; }
    // How lit 'id' is now: 0 not at all, 1 fully.
    float Level(int id) const;

private:
    float Progress() const;

    int m_current = -1;
    int m_previous = -1;
    float m_currentFrom = 0.0f;   // the level each one started from
    float m_previousFrom = 0.0f;
    ULONGLONG m_start = 0;
    bool m_animated = false;
};

#endif
