#ifndef TAG_SIDEBAR_HPP
#define TAG_SIDEBAR_HPP

#include <windows.h>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include "filter_query.hpp"
#include "text_field.hpp"
#include "ui_util.hpp"
#include "../../include/library.hpp"

class file;
class Renderer;

// The Library views of the sidebar: which files the grid shows before the search narrows them. Pictures, Videos and
// Audio and Books go by the file's kind (see KindOf); Untagged and Missing are the library's own views.
enum class SidebarView { kAll, kPictures, kVideos, kAudio, kBooks, kUntagged, kMissing };

// The sidebar left of the grid, which picks the Library view and filters the files by tag:
//
//   Library
//   ▦ All files  1,240  the view shown: a violet highlight
//   ▭ Pictures     800
//   ▷ Videos        90
//   ♪ Audio        120
//   ▯ Books         45
//   ◇ Untagged     310
//   △ Missing        4  as of the last check of the disk (see Window::CheckMissing); no row while none is
//
//   Tags                    Name Count   the order of the tags
//   [ Filter tags   ]
//   ▾ ■ Music        60     a tag is a path: names split on '/' make a tree, a parent counts the files under it
//       □ Album      40       (each file once), and its arrow opens or closes the branch
//   — 4k                    excluded: a rose dash, the name struck through in rose
//   □ wallpaper    3        off: a hollow square; required is a violet square and the name in violet
//
// Clicking a tag cycles it by rewriting the search text (see filter_query.hpp), which stays the one source of
// truth; a parent is searched as {Music}, which finds what is under it too. A tag's count is of the current
// results; tags with none are dimmed but stay in the list. While a tag is toggled, the tags the results don't use
// sink below the others of their branch (toggled tags and the parents above them keep their place). A parent needn't exist as a
// tag of its own. With text in the filter field the tree gives way to a flat list of the matching tags, each
// with the branch it is in. The Library counts are of the whole library, whatever the search.
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
    // The path of the tag whose row is under the point, or empty (also over a disclosure arrow). A press on a row can
    // start dragging the tag onto files; the path may be of a parent that is no tag of its own.
    std::string TagNameAt(POINT pt) const;
    // The same, but also over the disclosure arrow: for the menu and F2, which are about the row, not a press on it.
    std::string RowTagAt(POINT pt) const;
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
    // A tag path: a node of the tree built from the names of the tags. Parents that aren't tags are nodes too.
    struct Node
    {
        std::string path;        // UTF-8, as the library has it (a parent: as its first child spells it)
        std::string folded;      // the path as the search compares it (the tag's identity)
        std::wstring label;      // the last part, for drawing
        std::wstring parentLabel;  // the last part of the parent's path, for the flat list; empty at the top
        std::wstring full;       // the whole path, for the tooltip
        std::string searchable;  // the folded path with '_' as spaces, for the filter field
        int parent = -1;
        int depth = 0;
        std::vector<int> kids;
        TagState state = TagState::kOff;
        size_t count = 0;        // result files that have it or a tag under it, each once
        bool relevant = true;    // with a tag toggled: the results use it, or it (or one under it) is toggled
    };
    struct RowBox
    {
        RECT arrow, name, count;  // the disclosure arrow's hit area, where the name goes and the count
        float marker;             // x of the state marker
    };
    enum class Sort { kName, kCount };

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
    void Arrange(std::vector<size_t> &out, int node) const;
    void SortNodes(std::vector<int> &nodes) const;
    int RowHeight() const;
    RowBox Box(const Node &node, const RECT &rect) const;
    bool Truncated(const Node &node, const RECT &rect) const;
    RECT SortRect(Sort sort) const;
    void ViewsChanged();
    int ViewIndex(int slot) const { return m_shownViews[static_cast<size_t>(slot)]; }
    int ShownViewCount() const { return static_cast<int>(m_shownViews.size()); }

    void PaintView(Renderer &renderer, int index, float hover) const;
    void PaintViewIcon(Renderer &renderer, int index, float x, float cy, const D2D1_COLOR_F &color) const;
    void PaintRow(Renderer &renderer, const Node &node, const RECT &rect, float hover) const;
    void PaintSort(Renderer &renderer) const;
    void PaintSettings(Renderer &renderer) const;
    void PaintScrollbar(Renderer &renderer) const;

    const library &m_lib;
    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    TextField m_field;

    static constexpr SidebarView kViews[] = { SidebarView::kAll, SidebarView::kPictures, SidebarView::kVideos,
        SidebarView::kAudio, SidebarView::kBooks, SidebarView::kUntagged, SidebarView::kMissing };
    static constexpr int kViewCount = static_cast<int>(std::size(kViews));

    SidebarView m_view = SidebarView::kAll;
    size_t m_viewCounts[kViewCount] = {};  // by SidebarView
    bool m_missingKnown = false;           // the Missing count is shown once the disk was checked
    bool m_active = true;
    HoverFade m_viewHover;  // by SidebarView
    HoverFade m_settingsHover;  // 0 while the pointer is over the Settings row

    std::vector<int> m_shownViews;  // the views with a row, as indexes into kViews (Missing only while there are some)

    std::vector<Node> m_nodes;     // every tag path and the parents above them
    std::vector<size_t> m_visible; // the nodes with a row now, in order: the open branches, or the field's matches
    bool m_flat = false;           // the field has text: a flat list, two lines a row
    bool m_filtering = false;      // a tag is required or excluded: the tags the results don't use sink to the bottom
    bool m_branches = false;       // some tag has others under it: rows leave room for the arrow
    Sort m_sort = Sort::kName;
    std::unordered_set<std::string> m_open;    // the folded paths of the branches shown open, for the session
    std::unordered_set<std::string> m_marked;  // the folded paths that were required or excluded at the last update
    mutable std::wstring m_tipText;
    std::string m_dropFolded;
    int m_scrollY = 0;
    POINT m_hoverPoint = { -1, -1 };
    HoverFade m_hover;  // by index into m_visible
    HoverFade m_sortHover;  // 0 Name, 1 Count
};

#endif
