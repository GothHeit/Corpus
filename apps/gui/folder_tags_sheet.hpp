#ifndef FOLDER_TAGS_SHEET_HPP
#define FOLDER_TAGS_SHEET_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "sheet.hpp"
#include "ui_util.hpp"

class Renderer;

// A tag the folders of an import suggest: the path of a folder with files of its own ("NGE_Music/Album/Disc_1"), and
// those files (by path).
struct FolderTag
{
    std::string name;
    std::vector<std::string> paths;
    bool on = true;  // ticked
};

// The sheet that follows an import of folders, to confirm the tags their names suggest. The folders show as the tree
// they are, and each file gets the tag of its own folder (the folders above it are its parents, never stored on it):
//
//   +-----------------------------------------------+
//   |  Tag 214 files by folder?                     |
//   |  Each file gets the tag of its own folder...  |
//   |  [ Only NGE_Music | Every folder ]             |
//   |  +-----------------------------------------+  |
//   |  | v [x] NGE_Music                     214 |  |
//   |  |   v [x] Rebuild                      90 |  |
//   |  |       > [ ] Disc_1   in Rebuild      40 |  |
//   |  +-----------------------------------------+  |
//   |  [ ] Remove the old folder tags               |
//   |  12 tags, shown as a tree under NGE_Music     |
//   |                          [Skip] [Tag 214 files]|
//   +-----------------------------------------------+
//
// Every folder starts ticked. An unticked folder gives its files the tag of the nearest ticked folder above it
// ("in Rebuild"), or none. Only the root and its first level start open. Tags() is what to apply. The option to remove
// the old (flat) folder tags shows only when the sheet is opened from Settings for files already in the library.
class FolderTagsSheet : public Sheet
{
public:
    // Opens the sheet over 'bounds' (the client area). 'tags' is the suggestion for the folders of the import.
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, std::vector<FolderTag> tags, bool offerRemoveOld = false);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    void SetBounds(const RECT &bounds) override { m_bounds = bounds; }

    // The tags to put on the files, as the ticks have them: one entry (all ticked) per tag, with every file that gets it.
    // Files whose folders are all unticked are in none.
    std::vector<FolderTag> Tags() const;
    // The "Remove the old folder tags" option was offered and is ticked.
    bool RemoveOld() const { return m_offerRemoveOld && m_removeOld; }

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnMouseWheel(POINT pt, int delta) override;
    void OnHover(POINT pt) override;

private:
    // A folder of the tree. The nodes are in tree order (a folder, then what is under it).
    struct Node
    {
        std::string path;
        std::wstring name;            // the last part
        int parent = -1;
        int depth = 0;
        bool hasKids = false;
        bool open = false;
        bool on = true;
        std::vector<std::string> files;  // those directly in the folder
        size_t total = 0;                // those in it and under it
    };
    enum class Preset { kRoot, kEvery, kCustom };

    static constexpr int kMaxRows = 10;
    // What the pointer or the keyboard is on besides the folders.
    static constexpr int kHoverNone = -1;
    static constexpr int kHoverSkip = -2;
    static constexpr int kHoverApply = -3;
    static constexpr int kHoverRoot = -4;
    static constexpr int kHoverEvery = -5;
    static constexpr int kHoverRemove = -6;

    int Scale(int value) const;
    int RowsShown() const;
    RECT CardRect() const;
    RECT PresetRect(Preset which) const;
    RECT TreeRect() const;
    RECT RowRect(int slot) const;
    RECT RemoveRect() const;
    RECT ConfirmRect() const;
    RECT SkipRect() const;
    RECT SummaryRect() const;
    RECT ArrowRect(const RECT &row, const Node &node) const;
    RECT TickRect(const RECT &row, const Node &node) const;
    int RowAt(POINT pt) const;
    int Target(POINT pt) const;

    void Build(std::vector<FolderTag> tags);
    void Rebuild();
    int EffectiveTag(int node) const;
    size_t TaggedFiles() const;
    size_t TagCount() const;
    std::wstring ApplyLabel() const;
    bool CanApply() const;
    std::wstring RootName() const;
    void ApplyPreset(Preset preset);
    void ToggleOpen(int visibleIndex);
    void ScrollTo(int first);
    void Pick(int index);
    void Invalidate() const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    std::vector<Node> m_nodes;
    std::vector<int> m_rows;   // the nodes with a row now: those whose parents are all open
    size_t m_fileCount = 0;
    Preset m_preset = Preset::kEvery;
    bool m_offerRemoveOld = false;
    bool m_removeOld = true;
    int m_first = 0;    // the first row shown
    int m_pick = 0;     // the row Space ticks; one past the last is the "Remove" option
    int m_hover = kHoverNone;
    bool m_keys = false;  // the keyboard moved last: the row it is on is outlined
};

#endif
