#ifndef VIEWER_SHARED_HPP
#define VIEWER_SHARED_HPP

// What the files of Viewer (viewer.cpp, viewer_*.cpp) share besides the class itself.

#include <windows.h>
#include <cstddef>

// One wheel notch, or one press of + or -, zooms by this much.
const double kZoomStep = 1.25;
const double kMaxZoom = 32.0;
// A picture that takes longer than this to decode says "Loading" meanwhile; a quicker one just appears.
const UINT kLoadingDelayMs = 250;
// While the window is minimized, an animation only checks this often whether it is back.
const UINT kMinimizedFrameMs = 250;
// A frame that went up later than this after it was due starts the animation's clock again, rather than
// rushing the frames after it to catch up.
const ULONGLONG kLateFrameMs = 50;

// How often the time and the seek bar move on while the controls over a video show.
const UINT kVideoTimeMs = 250;
// The arrows seek this far into a video, and the wheel and the up and down arrows change its volume this much.
const double kSeekStepSeconds = 5.0;
const int kVolumeStep = 5;
// The speeds [ and ] step through, and the speed button cycles through; Backspace goes back to 1x.
const double kSpeeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };
// A speed set by key or button is named over the video this long.
const UINT kToastMs = 1200;

// A book: a click this close to either side (a fraction of the width) turns the page, unless it drags. The
// up and down arrows scroll a page this much of the view, a wheel notch this much, and Page Up and Page Down
// a view at a time less this much of it, which shows again.
const double kTurnZone = 0.3;
const double kArrowScroll = 0.15;
const double kWheelScroll = 0.2;
const double kPageOverlap = 0.1;
// The page reached is saved once the reader stayed on it this long, and when the book is left.
const UINT kProgressDelayMs = 1000;
const size_t kNoPage = static_cast<size_t>(-1);
// An EPUB's pages are laid out again for a new size once the view stayed that size this long.
const UINT kReflowDelayMs = 150;

// How books are read is kept for next time, next to the window's settings.
const wchar_t *const kSettingsKey = L"Software\\Corpus";
const wchar_t *const kFitWidthValue = L"ReaderFitWidth";
const wchar_t *const kRightToLeftValue = L"ReaderRightToLeft";

inline void SaveSetting(const wchar_t *name, bool on)
{
    DWORD value = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &value, sizeof(value));
}

inline int Width(const RECT &r)
{
    return r.right - r.left;
}

#endif
