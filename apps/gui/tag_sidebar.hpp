#ifndef TAG_SIDEBAR_HPP
#define TAG_SIDEBAR_HPP

#include <windows.h>
#include <string>
#include <vector>
#include "filter_query.hpp"
#include "text_field.hpp"
#include "ui_util.hpp"

class file;
class library;
class Renderer;

// The sidebar left of the grid that filters the files by tag:
//
//   Tags
//   [ Filter tags   ]
//   ■ anime       12    required: a violet square, the name in violet
//   — 4k                excluded: a rose dash, the name struck through in rose
//   □ wallpaper    3    off: a hollow square
//
// Clicking a tag cycles it through required, excluded and off by rewriting the search text (see filter_query.hpp),
// which stays the one source of truth: typing {anime} in the search marks the tag here too. The count is how many
// files of the current results have the tag; tags none of them have are dimmed but stay in place, so the list
// doesn't move under the pointer. The field above the list narrows it to the tags whose name contains its text.
class TagSidebar
{
public:
    explicit TagSidebar(const library &lib);

    // An empty rectangle hides the sidebar.
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    void OnDpiChanged() { m_field.OnDpiChanged(); }
    void Paint(Renderer &renderer) const;

    // Lists the tags of the library, marked as the search text 'query' asks for, with how many of 'results' have
    // each. Call it whenever the query, the results or the tags change.
    void Update(const std::wstring &query, const std::vector<file*> &results);

    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }
    void Blur() { m_field.Blur(); }
    bool IsFocused() const { return m_field.IsFocused(); }
    bool OverField(POINT pt) const { return m_field.Contains(pt); }

    // Returns true when the click landed on the sidebar. A click on a tag rewrites 'query', the search text, to
    // cycle it.
    bool OnLButtonDown(POINT pt, std::wstring &query);
    // The keyboard goes to the sidebar only while its field is focused.
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM key, bool ctrl);
    bool OnMouseWheel(POINT pt, int delta);
    // Highlights the tag under the pointer. 'pt' is outside the window when the pointer left it.
    void OnHover(POINT pt);
    void OnTimer();

private:
    struct Row
    {
        std::string name;    // UTF-8, as the library has it
        std::wstring label;  // the name, for drawing
        std::string folded;  // the name as the search compares it
        TagState state;
        size_t count;        // files of the results that have it
    };

    int Scale(int value) const;
    void Invalidate() const;
    bool Visible() const { return m_bounds.right > m_bounds.left; }

    RECT FieldRect() const;
    RECT ListRect() const;
    RECT RowRect(size_t index) const;
    int RowAt(POINT pt) const;
    int MaxScroll() const;
    void ScrollTo(int y);
    void Filter();
    void FilterChanged();

    void PaintRow(Renderer &renderer, const Row &row, const RECT &rect, float hover) const;
    void PaintScrollbar(Renderer &renderer) const;

    const library &m_lib;
    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    TextField m_field;

    std::vector<Row> m_rows;       // every tag, by name
    std::vector<size_t> m_visible; // the rows the field leaves, in order
    int m_scrollY = 0;
    POINT m_hoverPoint = { -1, -1 };
    HoverFade m_hover;  // by index into m_visible
};

#endif
