#ifndef THEME_HPP
#define THEME_HPP

#include <windows.h>

// Colors and shared layout sizes for the whole GUI.
// Sizes are in 96-DPI pixels; scale them with ScaleForWindow before use.
namespace theme
{
    constexpr COLORREF kBackground         = RGB(28, 28, 28);
    constexpr COLORREF kText               = RGB(230, 230, 230);
    constexpr COLORREF kPlaceholderText    = RGB(140, 140, 140);

    constexpr COLORREF kCloseButton        = RGB(255, 95, 86);
    constexpr COLORREF kMinimizeButton     = RGB(255, 189, 46);
    constexpr COLORREF kMaximizeButton     = RGB(39, 201, 63);

    constexpr COLORREF kSearchFill         = RGB(45, 45, 50);
    constexpr COLORREF kSearchError        = RGB(220, 60, 60);

    constexpr COLORREF kFileIcon           = RGB(60, 60, 66);
    constexpr COLORREF kFileIconSelected   = RGB(70, 92, 130);
    constexpr COLORREF kSelectionFill      = RGB(40, 52, 72);
    constexpr COLORREF kSelectionBorder    = RGB(80, 130, 210);
    constexpr COLORREF kChipFill           = RGB(52, 58, 70);
    constexpr COLORREF kChipPartialFill    = RGB(40, 40, 45);
    constexpr COLORREF kScrollThumb        = RGB(85, 85, 91);
    constexpr COLORREF kScrollThumbActive  = RGB(130, 130, 136);

    constexpr int kTitleBarHeight  = 20;
    constexpr int kSearchBarHeight = 28;
    constexpr int kTagPanelHeight  = 28;
    constexpr int kSectionGap      = 10;  // between title bar, search bar, grid and tag panel
    constexpr int kPageMargin      = 20;  // left/right margin of the content
    constexpr int kResizeBorder    = 6;   // invisible border that resizes the window
    constexpr int kMinWindowWidth  = 320;
    constexpr int kMinWindowHeight = 240;
}

#endif
