#ifndef THEME_HPP
#define THEME_HPP

#include <d2d1.h>

// Colors and shared layout sizes for the whole GUI. Only the dark palette exists for now; a light one would be
// a second set of these colors.
// Sizes are in 96-DPI pixels; scale them with ScaleForWindow before use.
namespace theme
{
    constexpr D2D1_COLOR_F Rgb(unsigned hex, float alpha = 1.0f)
    {
        return { ((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha };
    }

    constexpr D2D1_COLOR_F White(float alpha)
    {
        return { 1.0f, 1.0f, 1.0f, alpha };
    }

    // Surfaces, from the back to the front.
    constexpr D2D1_COLOR_F kBase    = Rgb(0x1B191E);  // window, title and search rows, tag strip
    constexpr D2D1_COLOR_F kLayer   = Rgb(0x221F26);  // file area
    constexpr D2D1_COLOR_F kControl = Rgb(0x2A2730);  // text fields, file icons
    constexpr D2D1_COLOR_F kChip    = Rgb(0x36323D);  // tag chips
    constexpr D2D1_COLOR_F kStroke  = White(0.07f);   // 1px edge between areas

    constexpr D2D1_COLOR_F kText1 = Rgb(0xEFECF3);  // primary
    constexpr D2D1_COLOR_F kText2 = Rgb(0xBDB6C8);  // secondary
    constexpr D2D1_COLOR_F kText3 = Rgb(0x8D8698);  // hints, placeholders, counts

    constexpr D2D1_COLOR_F kHover   = White(0.05f);  // anything clickable under the pointer
    constexpr D2D1_COLOR_F kPressed = White(0.03f);
    constexpr D2D1_COLOR_F kPlate   = White(0.09f);  // behind a selected file icon

    constexpr D2D1_COLOR_F kYuri     = Rgb(0x9A6FE0);  // accent: selection, focus
    constexpr D2D1_COLOR_F kOnAccent = Rgb(0x140F1C);  // text on the accent
    constexpr D2D1_COLOR_F kRose     = Rgb(0xFF8FA3);  // excluded tags, invalid query
    constexpr D2D1_COLOR_F kOxblood     = Rgb(0xB23A4C);  // import progress, drop target
    constexpr D2D1_COLOR_F kOxbloodFill = Rgb(0x8E2433);

    constexpr D2D1_COLOR_F kScrollThumb       = White(0.16f);
    constexpr D2D1_COLOR_F kScrollThumbActive = White(0.30f);

    // The window buttons look like the macOS ones: a fill with a thin darker rim.
    constexpr D2D1_COLOR_F kCloseButton       = Rgb(0xFF5F57);
    constexpr D2D1_COLOR_F kCloseRim          = Rgb(0xE0443E);
    constexpr D2D1_COLOR_F kMinimizeButton    = Rgb(0xFEBC2E);
    constexpr D2D1_COLOR_F kMinimizeRim       = Rgb(0xDEA123);
    constexpr D2D1_COLOR_F kMaximizeButton    = Rgb(0x28C840);
    constexpr D2D1_COLOR_F kMaximizeRim       = Rgb(0x1AAB29);
    constexpr D2D1_COLOR_F kInactiveButton    = Rgb(0x3A373F);  // all three, while another window is active
    constexpr D2D1_COLOR_F kInactiveRim       = Rgb(0x45424A);
    constexpr D2D1_COLOR_F kButtonGlyph       = { 0.0f, 0.0f, 0.0f, 0.5f };

    // Font sizes.
    constexpr int kBodySize    = 14;
    constexpr int kCaptionSize = 12;

    constexpr int kTitleRowHeight  = 40;  // the window buttons and the title
    constexpr int kSearchRowHeight = 32;
    constexpr int kSearchHeight    = 30;
    constexpr int kControlHeight   = 28;
    constexpr int kTagStripHeight  = 44;
    constexpr int kContentGap      = 12;  // between the search row and the file area
    constexpr int kPageMargin      = 16;  // left/right margin of the content
    constexpr int kControlRadius   = 3;   // text fields and file icons
    constexpr int kChipRadius      = 4;   // tag chips and selection highlights
    constexpr int kResizeBorder    = 6;   // invisible border that resizes the window
    constexpr int kMinWindowWidth  = 360;
    constexpr int kMinWindowHeight = 280;
}

#endif
