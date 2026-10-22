#ifndef TAG_PANEL_HPP
#define TAG_PANEL_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "text_field.hpp"

class file;
class library;
class Renderer;

// A change to the tags of the selected files that the user asked for. The window applies it.
struct TagEdit
{
    enum Kind { kNone, kAdd, kRemove };

    Kind kind = kNone;
    std::vector<std::string> tags;  // UTF-8 names
};

// The strip under the grid that edits the tags of the selected files:
//
//   3 files  [ Add tags      ]  [anime x] [4k x] [:wallpaper 2/3 x:]
//
// Enter adds the tags typed in the field (separated by spaces) to every selected file. The last word is
// completed from the tags in the library, the most used first: Tab accepts, Up/Down pick another one.
// Each chip is a tag of the selection, and its x takes it off every selected file. A chip with a dashed
// outline is on only some of them, and says how many: clicking it adds it to the rest. The chips scroll
// sideways with the mouse wheel.
class TagPanel
{
public:
    explicit TagPanel(const library &lib);

    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    void Paint(Renderer &renderer) const;

    // Shows the tags of these files. Call it whenever the selection or the tags of the selected files change.
    void SetSelection(const std::vector<file*> &files);

    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }
    void Focus();
    void Blur();
    bool IsFocused() const { return m_field.IsFocused(); }

    // The mouse handlers return true when the event landed on the panel. The keyboard goes to the panel only
    // while its field is focused. Clicks and keys fill 'edit' when the user asks for a change.
    bool OnLButtonDown(POINT pt, TagEdit &edit);
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM key, bool ctrl, TagEdit &edit);
    bool OnMouseWheel(POINT pt, int delta);
    // Highlights the chip under the pointer. 'pt' is outside the window when the pointer left it.
    void OnHover(POINT pt);
    bool OverField(POINT pt) const { return m_field.Contains(pt); }

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

    RECT FieldRect() const;
    RECT ChipArea() const;
    RECT ChipRect(const Chip &chip) const;
    int CloseLeft(const Chip &chip) const;
    int ChipAt(POINT pt, bool &onClose) const;
    void MeasureChips();
    void ScrollChipsTo(int x);
    bool OnEverySelectedFile(const std::wstring &name) const;

    std::wstring CurrentWord() const;
    void UpdateSuggestion();
    void AcceptSuggestion();
    std::vector<std::string> TypedTags() const;

    const library &m_lib;
    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    TextField m_field;

    size_t m_selectedCount = 0;
    std::vector<Chip> m_chips;
    int m_chipsWidth = 0;
    int m_chipScroll = 0;
    POINT m_hoverPoint = { -1, -1 };
    int m_hoverChip = -1;
    bool m_hoverClose = false;

    std::vector<std::wstring> m_matches;  // tags that complete the word being typed, best first
    size_t m_matchIndex = 0;
};

#endif
