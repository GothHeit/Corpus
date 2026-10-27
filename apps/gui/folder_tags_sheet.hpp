#ifndef FOLDER_TAGS_SHEET_HPP
#define FOLDER_TAGS_SHEET_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "sheet.hpp"
#include "ui_util.hpp"

class Renderer;

// A tag the folders of an import suggest, and the files (by path) it would go on.
struct FolderTag
{
    std::string name;
    std::vector<std::string> paths;
    bool on = true;  // ticked
};

// The sheet that follows an import of folders, to confirm the tags their names suggest:
//
//   +-------------------------------------+
//   |  Tag by folder name                 |
//   |  Tick the folder names to use as    |
//   |  tags on the new files.             |
//   |   [x] Holiday_2023             214  |
//   |   [x] Beach                     90  |
//   |   [ ] Raw                       31  |
//   |                  [Skip] [Add tags]  |
//   +-------------------------------------+
//
// Every tag starts ticked. Eight rows show at a time; the wheel scrolls the rest.
class FolderTagsSheet : public Sheet
{
public:
    // Opens the sheet over 'bounds' (the client area).
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, std::vector<FolderTag> tags);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    void SetBounds(const RECT &bounds) override { m_bounds = bounds; }

    const std::vector<FolderTag> &Tags() const { return m_tags; }

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnMouseWheel(POINT pt, int delta) override;
    void OnHover(POINT pt) override;

private:
    static constexpr int kVisibleRows = 8;

    int Visible() const;
    RECT CardRect() const;
    RECT ListRect() const;
    RECT RowRect(int slot) const;
    RECT ConfirmRect() const;
    RECT SkipRect() const;
    int RowAt(POINT pt) const;
    void ScrollTo(int first);
    void Pick(int index);
    int Scale(int value) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    bool m_open = false;
    std::vector<FolderTag> m_tags;
    int m_first = 0;    // the first row shown
    int m_pick = 0;     // the row Space ticks
    int m_hover = -1;   // a row, or -2 for Skip, -3 for Add tags
};

#endif
