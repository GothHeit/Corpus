#ifndef TAG_SIDEBAR_HPP
#define TAG_SIDEBAR_HPP

#include <windows.h>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include "filter_query.hpp"
#include "text_field.hpp"
#include "ui_util.hpp"
#include "../../include/library.hpp"

class file;
class Renderer;

// The Library views of the sidebar: which files the grid shows before the search narrows them. Pictures, Videos and
// Books go by the file's kind (see KindOf); Untagged and Missing are the library's own views.
enum class SidebarView { kAll, kPictures, kVideos, kBooks, kUntagged, kMissing };

// The sidebar left of the grid, which picks the Library view and filters the files by tag:
//
//   Library
//   ▦ All files  1,240  the view shown: a violet highlight
//   ▭ Pictures     800
//   ▷ Videos        90
//   ▯ Books         45
//   ◇ Untagged     310
//   △ Missing        4  as of the last check of the disk (see Window::CheckMissing)
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
// The Settings row sits at the foot, below the list. The Library counts are of the whole library, whatever the search: they say how big each view is.
class TagSidebar
{
public:
    explicit TagSidebar(const library &lib);

    // An empty rectangle hides the sidebar.
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);
    void OnDpiChanged() { m_field.OnDpiChanged(); }
    // The label of the icon-only button under 'pt', if there is one, and where that button is.
    bool TipAt(POINT pt, RECT &anchor, const wchar_t *&text) const;
    void Paint(Renderer &renderer) const;

    // Lists the tags of the library, marked as the search text 'query' asks for, with how many of 'results' have
    // each. Call it whenever the query, the results, the files or the tags change.
    void Update(const std::wstring &query, const std::vector<file*> &results);
    // Counts the files of each Library view, 'missing' being the Missing one's, unknown until the disk was
    // checked. Call it whenever the files or their tags change, not on every keystroke: it goes through them all.
    void CountViews(std::optional<size_t> missing);

    SidebarView View() const { return m_view; }
    void SetView(SidebarView view);
    // The highlight of the view shown turns grey while another window is active.
    void SetActive(bool active);

    bool Contains(POINT pt) const { return PtInRect(&m_bounds, pt) != 0; }
    void Blur() { m_field.Blur(); }
    // Puts the keyboard in the tag filter (Tab from the search or the grid).
    void Focus() { m_field.Focus(); }
    bool IsFocused() const { return m_field.IsFocused(); }
    bool OverField(POINT pt) const { return m_field.Contains(pt); }

    // Returns true when the click landed on the sidebar. A click on a tag rewrites 'query', the search text, to
    // cycle it; a click on a Library view sets 'view' to it, even the one already shown; one on
    // the Settings row at the foot sets 'openSettings'.
    bool OnLButtonDown(POINT pt, std::wstring &query, std::optional<SidebarView> &view, bool &openSettings);
    // The name of the tag whose row is under the point, or empty. A press on a row can start dragging the tag onto files.
    std::string TagNameAt(POINT pt) const;
    // Files are being dragged over the sidebar: outlines the tag they would land on ('name' as the library has it;
    // empty for none) and scrolls the list a step when the pointer is near its top or bottom edge. Returns true if
    // it scrolled, so the caller keeps scrolling while the pointer rests there.
    void SetDropTag(const std::string &name);
    bool DragScroll(POINT pt);
    // The keyboard goes to the sidebar only while its field is focused.
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM key, bool ctrl);
    bool OnMouseWheel(POINT pt, int delta);
    // Highlights the tag under the pointer. 'pt' is outside the window when the pointer left it.
    void OnHover(POINT pt);
    void OnTimer(UINT_PTR timer);

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

    RECT ViewRect(int index) const;
    int ViewAt(POINT pt) const;
    int TagsTop() const;
    RECT FieldRect() const;
    RECT SettingsSlot() const;
    RECT SettingsRect() const;
    RECT ListRect() const;
    RECT RowRect(size_t index) const;
    int RowAt(POINT pt) const;
    int MaxScroll() const;
    void ScrollTo(int y);
    void Filter();
    void FilterChanged();

    void PaintView(Renderer &renderer, int index, float hover) const;
    void PaintViewIcon(Renderer &renderer, int index, float x, float cy, const D2D1_COLOR_F &color) const;
    void PaintRow(Renderer &renderer, const Row &row, const RECT &rect, float hover) const;
    void PaintSettings(Renderer &renderer) const;
    void PaintScrollbar(Renderer &renderer) const;

    const library &m_lib;
    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    TextField m_field;

    static constexpr SidebarView kViews[] = { SidebarView::kAll, SidebarView::kPictures, SidebarView::kVideos,
        SidebarView::kBooks, SidebarView::kUntagged, SidebarView::kMissing };
    static constexpr int kViewCount = static_cast<int>(std::size(kViews));

    SidebarView m_view = SidebarView::kAll;
    size_t m_viewCounts[kViewCount] = {};  // by SidebarView
    bool m_missingKnown = false;           // the Missing count is shown once the disk was checked
    bool m_active = true;
    HoverFade m_viewHover;  // by SidebarView
    HoverFade m_settingsHover;  // 0 while the pointer is over the Settings row

    std::vector<Row> m_rows;       // every tag, by name
    std::vector<size_t> m_visible; // the rows the field leaves, in order
    int m_scrollY = 0;
    std::string m_dropTag;  // see SetDropTag
    POINT m_hoverPoint = { -1, -1 };
    HoverFade m_hover;  // by index into m_visible
};

#endif
