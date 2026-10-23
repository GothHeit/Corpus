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
    constexpr D2D1_COLOR_F kBase    = Rgb(0x1B191E);  // window, title and search rows, sidebar, tag strip
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
    constexpr D2D1_COLOR_F kCaution     = Rgb(0xE8B04A);  // missing files

    // The viewer: a near-black backdrop, and the name and buttons floating over the picture.
    constexpr D2D1_COLOR_F kViewerBackdrop = Rgb(0x0E0D10);
    constexpr D2D1_COLOR_F kOverlay        = Rgb(0x1B191E, 0.9f);
    constexpr D2D1_COLOR_F kOverlayHover   = White(0.08f);
    constexpr D2D1_COLOR_F kSliderTrack    = White(0.18f);  // the seek bar and the volume slider, behind their fill

    // An EPUB's pages: light text on the viewer's backdrop, whatever colors the book asks for.
    constexpr D2D1_COLOR_F kReaderPage = kViewerBackdrop;
    constexpr D2D1_COLOR_F kReaderText = Rgb(0xE4E0E9);
    constexpr D2D1_COLOR_F kReaderLink = Rgb(0xB496EA);  // links, a lighter Yuri so they read well as text
    constexpr D2D1_COLOR_F kReaderRule = Rgb(0x4A4652);  // borders and rules

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

    // Motion, in milliseconds.
    constexpr unsigned kHoverFadeMs = 83;
    constexpr unsigned kThumbnailFadeMs = 167;
    constexpr unsigned kFrameMs     = 16;  // between redraws while something moves
    constexpr unsigned kPaneMs      = 250;  // panes, sheets and the viewer's overlays
    constexpr unsigned kOverlayIdleMs = 2000;  // the viewer's overlays fade out once the pointer stood still this long

    // Font sizes.
    constexpr int kBodySize    = 14;
    constexpr int kCaptionSize = 12;

    constexpr int kTitleRowHeight  = 40;  // the window buttons and the title
    constexpr int kSearchRowHeight = 32;
    constexpr int kSearchHeight    = 30;
    constexpr int kControlHeight   = 28;
    constexpr int kTagStripHeight  = 44;
    constexpr int kSidebarWidth    = 220;
    constexpr int kSidebarRow      = 30;   // a tag in the sidebar
    constexpr int kSidebarMinWindow = 560; // narrower windows leave the sidebar out, so the grid keeps its room
    constexpr int kContentGap      = 12;  // between the search row and the file area
    constexpr int kPageMargin      = 16;  // left/right margin of the content
    constexpr int kControlRadius   = 3;   // text fields and file icons
    constexpr int kChipRadius      = 4;   // tag chips and selection highlights
    constexpr int kResizeBorder    = 6;   // invisible border that resizes the window
    constexpr int kMinWindowWidth  = 360;
    constexpr int kStartWidth      = 960;  // the window when it opens, if the screen has room
    constexpr int kStartHeight     = 620;
    constexpr int kMinWindowHeight = 280;
    constexpr int kViewerButton    = 32;  // the viewer's close, previous and next buttons
    constexpr int kViewerMargin    = 16;  // between the viewer's overlays and its edges
    constexpr int kViewerInfoHeight = 32; // the name under the picture
    constexpr int kViewerBarHeight = 40;  // the control bar under a video
    constexpr int kViewerBarWidth  = 720; // at most
    constexpr int kViewerPageBarWidth = 480;  // the page bar under a book, at most
    constexpr int kViewerBarGap    = 8;   // between the control bar and the name above it
    constexpr int kViewerVolumeWidth = 64;
    constexpr int kViewerMenuRow   = 28;  // a track in the menus of the control bar
    constexpr int kViewerMenuMark  = 16;  // room for the mark of the track playing
    constexpr int kSettingsColumn  = 560;  // the settings page's column, at most
    constexpr int kSettingsRow     = 40;
    // An EPUB's pages, in CSS pixels (96 DPI): the body text, the longest line, and the room around the text.
    constexpr int kReaderFontSize   = 19;
    constexpr int kReaderMeasure    = 680;
    constexpr int kReaderSideMargin = 40;
    constexpr int kReaderTopMargin  = 48;
    constexpr int kReaderBottomMargin = 56;
}

#endif
