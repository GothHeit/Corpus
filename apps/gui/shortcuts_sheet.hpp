#ifndef SHORTCUTS_SHEET_HPP
#define SHORTCUTS_SHEET_HPP

#include <windows.h>
#include <vector>
#include "sheet.hpp"

class Renderer;

// The keyboard shortcut cheat sheet (F1, or the row in Settings). It slides down from under the search row over a
// dimmed window, boxless: sections of text, three columns wide (one, scrolling, in a narrow window), keys on key caps.
// The section for where the user is comes first, in violet. THE LIST of shortcuts is the table in shortcuts_sheet.cpp:
// when a shortcut changes (window_keys.cpp, viewer_input.cpp, file_grid.cpp), change it there too.
class ShortcutsSheet : public Sheet
{
public:
    // Where the user was when the sheet opened: it picks the section that goes first.
    enum class Context { kGrid, kPicture, kVideo, kMusic, kBook, kSettings };

    // 'top' is where the sheet starts, under the search row (client coordinates, scaled).
    void Open(HWND hWnd, Renderer &renderer, const RECT &bounds, int top, Context context);
    void Close() override;
    bool IsOpen() const override { return m_open; }
    void SetBounds(const RECT &bounds) override;

    void Paint(Renderer &renderer) const override;
    Action OnLButtonDown(POINT pt, bool doubleClick) override;
    Action OnKeyDown(WPARAM key, bool ctrl) override;
    void OnMouseWheel(POINT pt, int delta) override;
    void OnHover(POINT) override {}
    // A frame of the slide; false once it has landed (the timer then stops).
    bool OnTimer();

private:
    struct Block
    {
        int section;
        int column;
        int top;     // from the top of the content
        int height;
    };

    int Scale(int value) const;
    void Layout();
    float Progress() const;
    void ScrollTo(int y);
    int AreaHeight() const { return static_cast<int>(m_bounds.bottom) - m_top; }

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    int m_top = 0;
    bool m_open = false;
    Context m_context = Context::kGrid;
    ULONGLONG m_start = 0;  // 0: no slide
    std::vector<Block> m_blocks;
    int m_columns = 3;
    int m_columnWidth = 0;
    int m_left = 0;
    int m_contentHeight = 0;
    int m_scroll = 0;
};

#endif
