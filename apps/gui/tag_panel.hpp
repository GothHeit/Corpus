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
    enum Kind { kNone, kAdd, kRemove, kFilter };

    Kind kind = kNone;
    std::vector<std::string> tags;  // UTF-8 names (kRemove: level names; kFilter: the paths a chip stands for)
};

// The strip under the grid that shows the tags of the selected files and takes them off:
//
//   3 files  [+ Add tag]  [anime x] [4k x] [:wallpaper 2/3 x:]
//
// "+ Add tag" opens the tag sheet. A chip's x takes the tag off every selected file. A dashed chip is on only some
// of them, and clicking it adds it to the rest. The chips scroll sideways with the wheel.
class Renderer;
enum class Font;

// One chip of a file's tags: a level name, whichever paths it sits in. A file tagged Queen/Live and Metallica/Live
// has the chips Queen, Metallica and Live; Live's hover names both paths and a click filters by both.
struct LevelChip
{
    std::string name;                // the level, spelled as first met
    std::vector<std::string> paths;  // the tags that end in it (those it is a parent of are the other chips' paths)
    size_t files = 0;                // how many of the files have it
};

// The chips of these files, each file given as the names of its tags: the levels they have (case aside), the ones
// every file has first, then parents before children, then by name.
std::vector<LevelChip> LevelChips(const std::vector<std::vector<std::string>> &fileTags);

// A tooltip for a chip: its paths joined by " · ", or nothing worth showing when the chip is just its own name.
std::wstring LevelChipTip(const LevelChip &chip);

// The search text with a click on a chip applied (TagEdit::kFilter): the paths required, as one group {A OR B} for
// several, and a single path that is required already turned off again.
std::string FilterByPaths(const std::string &query, const std::vector<std::string> &paths);

// The last part of a tag path: "Rock" for "Music/Rock". The tooltips carry the whole path.
std::wstring LeafTagName(const std::wstring &path);

// A tag path for a chip that has 'maxWidth' pixels for it: whole if it fits, else the root and the last parts with
// "…" for the middle ("Music/…/Disc_1"). The caller draws it with an ellipsis if even that is too long.
std::wstring ShortTagPath(Renderer &renderer, const std::wstring &path, Font font, float maxWidth);

class TagPanel
{
public:
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    void OnDpiChanged() { MeasureChips(); }
    void Paint(Renderer &renderer) const;

    // Shows the tags of these files. Call it whenever the selection or the tags of the selected files change.
    void SetSelection(const std::vector<file*> &files);

    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }
    // The whole path of the shortened chip under 'pt', and where that chip is.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;

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
        std::wstring label; // the name as shown: shortened when it is long
        bool partial;       // only some of the selected files have it
        std::wstring count; // "2/5" on a partial chip
        int textWidth;      // the name and the count
        int offset;         // x from the start of the chip strip
        int width;
        std::wstring tip;   // the paths it stands for, when they say more than its name
        LevelChip level;
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
