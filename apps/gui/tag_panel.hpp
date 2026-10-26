#ifndef TAG_PANEL_HPP
#define TAG_PANEL_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "ui_util.hpp"

class file;
class Renderer;

// A change to the tags of the selected files that the user asked for. The window applies it.
struct TagEdit
{
    enum Kind { kNone, kAdd, kRemove };

    Kind kind = kNone;
    std::vector<std::string> tags;  // UTF-8 names
};

// The strip under the grid that shows the tags of the selected files and takes them off:
//
//   3 files  [+ Add tag]  [anime x] [4k x] [:wallpaper 2/3 x:]
//
// "+ Add tag" opens the tag sheet (see TagSheet), where tags are typed. Each chip is a tag of the selection, and its x
// takes it off every selected file. A chip with a dashed outline is on only some of them, and says how many: clicking
// it adds it to the rest. The chips scroll sideways with the mouse wheel.
class TagPanel
{
public:
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    void OnDpiChanged() { MeasureChips(); }
    void Paint(Renderer &renderer) const;

    // Shows the tags of these files. Call it whenever the selection or the tags of the selected files change.
    void SetSelection(const std::vector<file*> &files);

    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }

    // Returns true when the click landed on the panel. A click on a chip's x or a partial chip fills 'edit'; one on
    // "+ Add tag" sets 'addTag'.
    bool OnLButtonDown(POINT pt, TagEdit &edit, bool &addTag);
    bool OnMouseWheel(POINT pt, int delta);
    // Highlights the chip under the pointer. 'pt' is outside the window when the pointer left it.
    void OnHover(POINT pt);
    void OnTimer();

private:
    struct Chip
    {
        std::wstring name;
        bool partial;       // only some of the selected files have it
        std::wstring count; // "2/5" on a partial chip
        int textWidth;      // the name and the count
        int offset;         // x from the start of the chip strip
        int width;
    };

    int Scale(int value) const;
    void Invalidate() const;

    RECT AddRect() const;
    RECT ChipArea() const;
    RECT ChipRect(const Chip &chip) const;
    int CloseLeft(const Chip &chip) const;
    int ChipAt(POINT pt, bool &onClose) const;
    void MeasureChips();
    void ScrollChipsTo(int x);

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};

    size_t m_selectedCount = 0;
    std::vector<Chip> m_chips;
    int m_chipsWidth = 0;
    int m_chipScroll = 0;
    POINT m_hoverPoint = { -1, -1 };
    HoverFade m_hover;  // chip i is 2i, its x 2i + 1; the add button is -1 in ChipAt's terms, id kAddHover
};

#endif
