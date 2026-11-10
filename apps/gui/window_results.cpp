#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "../../include/library.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include "../../include/database.hpp"
#include "../../include/tag_query.hpp"
#include "filter_query.hpp"
#include "file_kind.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <shlwapi.h>
#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>

// Why library::search rejected a query, for the message under the search bar. Empty for a valid query.
static const wchar_t *QueryError(SearchError error)
{
    switch (error)
    {
    case SearchError::UnclosedBrace: return strings::kUnclosedBrace;
    case SearchError::BadExpression: return strings::kInvalidTags;
    case SearchError::None:          break;
    }
    return L"";
}

// Runs the query typed in the search bar in the Library view picked in the sidebar and shows the result in the grid.
// An invalid query marks the search bar and keeps the last valid result on screen.
void Window::RefreshResults()
{
    // Missing is the files of the last check of the disk, which the window keeps rather than the library. Pictures,
    // Videos, Audio and Books are the files of that kind.
    SidebarView view = m_sidebar.View();
    SearchResult result = m_lib.search(WideToUtf8(m_searchBar.Query()), view == SidebarView::kUntagged ? LibraryView::Untagged : LibraryView::All,
        ContentSearching());
    if (view == SidebarView::kMissing)
    {
        result.files.erase(std::remove_if(result.files.begin(), result.files.end(),
            [this](const file *f) { return !m_missingPaths.count(f->get_path()); }), result.files.end());
    }
    else if (view == SidebarView::kPictures || view == SidebarView::kVideos || view == SidebarView::kAudio ||
        view == SidebarView::kBooks)
    {
        FileKind kind = view == SidebarView::kPictures ? FileKind::kPicture : view == SidebarView::kVideos ? FileKind::kVideo
            : view == SidebarView::kAudio ? FileKind::kAudio : FileKind::kBook;
        result.files.erase(std::remove_if(result.files.begin(), result.files.end(),
            [kind](const file *f) { return KindOf(f->get_path()) != kind; }), result.files.end());
    }
    m_searchBar.SetError(QueryError(result.error));
    if (result.valid())
    {
        m_lastValidQuery = WideToUtf8(m_searchBar.Query());
        SortFiles(result.files);
        m_fileGrid.SetFiles(result.files);
        if (BoardMode())
            RefreshBoard(result.files);
        // The files found only through their text get the "in text" mark, and the info pane its snippet.
        m_contentHitPaths.clear();
        for (const file *f : result.content_hits)
            m_contentHitPaths.insert(f->get_path());
        m_fileGrid.SetContentHits(&m_contentHitPaths);
        SetContentWords();
        m_infoPane.SetContentSearch(m_contentWords, &m_contentHitPaths);
    }
    FileGrid::Empty empty = FileGrid::Empty::kNoMatches;
    if (m_lib.get_files().empty())
        empty = FileGrid::Empty::kLibrary;
    else if (view == SidebarView::kMissing && !m_missingChecked)
        empty = FileGrid::Empty::kChecking;
    m_fileGrid.SetEmpty(empty);
    UpdateBanner();
    // The grid keeps the last valid results, and lets go of them before files are deleted, so they are safe to count.
    m_sidebar.Update(m_searchBar.Query(), m_fileGrid.Files(), HiddenByExclusions(m_searchBar.Query(), view));
    SyncViewLabel();
    // The selection may have lost the files that left the view, and the tags of the files may have changed.
    SelectionChanged();
}

// For each tag the search excludes, how many files of the view that exclusion hides: the files the search would
// show without it that carry the tag (or one under it). Shown as the excluded row's count.
std::unordered_map<std::string, size_t> Window::HiddenByExclusions(const std::wstring &query, SidebarView view)
{
    std::unordered_map<std::string, size_t> hidden;
    std::string text = WideToUtf8(query);
    for (const auto &entry : TagStates(text))
    {
        if (entry.second != TagState::kExcluded)
            continue;
        SearchResult without = m_lib.search(WithTagState(text, entry.first, TagState::kOff),
            view == SidebarView::kUntagged ? LibraryView::Untagged : LibraryView::All, ContentSearching());
        std::unique_ptr<QueryNode> leaf = without.valid() ? ParseQuery(entry.first) : nullptr;
        size_t count = 0;
        for (const file *f : without.files)
        {
            if (leaf && leaf->Evaluate(f->get_tags()) && InView(f, view))
                count++;
        }
        hidden[entry.first] = count;
    }
    return hidden;
}

// Whether the file belongs to the Library view by its kind or its state (Missing as of the last check).
bool Window::InView(const file *f, SidebarView view) const
{
    switch (view)
    {
    case SidebarView::kMissing:  return m_missingPaths.count(f->get_path()) != 0;
    case SidebarView::kPictures: return KindOf(f->get_path()) == FileKind::kPicture;
    case SidebarView::kVideos:   return KindOf(f->get_path()) == FileKind::kVideo;
    case SidebarView::kAudio:    return KindOf(f->get_path()) == FileKind::kAudio;
    case SidebarView::kBooks:    return KindOf(f->get_path()) == FileKind::kBook;
    default:                     return true;
    }
}

// Replaces the search text, as clicking a tag in the sidebar or "Clear filters" does, and shows its results.
void Window::SetQuery(const std::wstring &query, bool user)
{
    m_searchBar.SetText(query);
    RefreshResults();
    CommitBoardQuery(user, user);
}

// The files or their tags changed: the sidebar counts its views again, which typing in the search doesn't need.
void Window::LibraryChanged()
{
    CountViews();
    RefreshResults();
    m_viewer.RefreshTags();
}

// The Missing count is that of the files the last check found, and unknown until the first one ends.
void Window::CountViews()
{
    std::optional<size_t> missing;
    if (m_missingChecked)
    {
        size_t count = 0;
        if (!m_missingPaths.empty())
        {
            for (const file *f : m_lib.get_files())
                count += m_missingPaths.count(f->get_path());
        }
        missing = count;
    }
    m_sidebar.CountViews(missing);
    // Missing has no row while no file is: with none left, the view falls back to All files.
    if (missing && *missing == 0 && m_sidebar.View() == SidebarView::kMissing)
        ShowView(SidebarView::kAll);
}

// Shows a Library view, Missing as of the last check of the disk, while a new one runs.
void Window::ShowView(SidebarView view)
{
    if (view == SidebarView::kMissing)
        CheckMissing();
    bool wasBoards = BoardMode();
    if (wasBoards && view != SidebarView::kBoards)
        CommitBoardQuery();
    m_sidebar.SetView(view);
    SyncViewLabel();
    if (view == SidebarView::kBoards)
    {
        // The board in view when the row was last left, else the first; with none, the empty view offers to make one.
        board *b = m_lib.find_board(m_boardId);
        if (!b && !m_lib.get_boards().empty())
            b = m_lib.get_boards().front();
        OpenBoard(b, false);
        return;
    }
    if (wasBoards)
        LayoutComponents();
    RefreshResults();
    InvalidateRect(m_hWnd, nullptr, FALSE);  // the sidebar button's mark
}

// The grid's files in the order picked with the sort button, by the rank RankFiles gave every file.
void Window::SortFiles(std::vector<file*> &files)
{
    if (m_sortDirty)
        RankFiles();
    std::vector<std::pair<size_t, file*>> ranked;
    ranked.reserve(files.size());
    bool fresh = false;
    for (file *f : files)
    {
        auto rank = m_sortRank.find(f);
        // A file the ranking doesn't know came since: rank them all again, once.
        if (rank == m_sortRank.end() && !fresh)
        {
            RankFiles();
            fresh = true;
            rank = m_sortRank.find(f);
        }
        ranked.push_back({ rank == m_sortRank.end() ? m_sortRank.size() : rank->second, f });
    }
    if (m_sortDescending)
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    else
        std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < files.size(); i++)
        files[i] = ranked[i].second;
}

// Ranks every file of the library by the sort key, from the first in ascending order: by when it was added (the
// order of its row in the database), its name as Explorer sorts names (2.jpg before 10.jpg), its size (as of the
// last check of the disk; unknown counts as 0) or its type (the extension), the name breaking ties.
void Window::RankFiles()
{
    struct Entry
    {
        file *f;
        std::int64_t added;
        std::wstring name;
        std::wstring type;
        std::uint64_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(m_lib.get_files().size());
    bool names = m_sortKey != SortKey::kAdded;
    for (file *f : m_lib.get_files())
    {
        Entry e = { f, f->get_db_id(), {}, {}, 0 };
        if (names)
        {
            std::string path = f->get_path();
            size_t slash = path.find_last_of("\\/");
            e.name = Utf8ToWide(slash == std::string::npos ? path : path.substr(slash + 1));
            if (m_sortKey == SortKey::kType)
                e.type = Utf8ToWide(FileExtension(path));
            else if (m_sortKey == SortKey::kSize)
            {
                auto size = m_sizes.find(path);
                e.size = size == m_sizes.end() ? 0 : size->second;
            }
        }
        entries.push_back(std::move(e));
    }

    auto byName = [](const Entry &a, const Entry &b)
    {
        int order = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
        return order != 0 ? order < 0 : a.added < b.added;
    };
    switch (m_sortKey)
    {
    case SortKey::kAdded:
        // Files are given ids as they are added; the library's order breaks ties between unsaved ones.
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.added < b.added; });
        break;
    case SortKey::kName:
        std::sort(entries.begin(), entries.end(), byName);
        break;
    case SortKey::kSize:
        std::sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b)
        {
            return a.size != b.size ? a.size < b.size : byName(a, b);
        });
        break;
    case SortKey::kType:
        std::sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b)
        {
            int order = a.type.compare(b.type);
            return order != 0 ? order < 0 : byName(a, b);
        });
        break;
    }

    m_sortRank.clear();
    m_sortRank.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); i++)
        m_sortRank[entries[i].f] = i;
    m_sortDirty = false;
}

// From the sort menu: the key and the order, kept for the next run.
void Window::SetSort(SortKey key, bool descending)
{
    if (key == m_sortKey && descending == m_sortDescending)
        return;
    m_sortDirty = m_sortDirty || key != m_sortKey;
    m_sortKey = key;
    m_sortDescending = descending;
    SaveSort(static_cast<int>(key), descending);
    LayoutComponents();
    RefreshResults();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

static const wchar_t *SortLabel(int key)
{
    static const wchar_t *const labels[] = { strings::kSortAdded, strings::kSortName, strings::kSortSize, strings::kSortType };
    return labels[key];
}

// The sort button names the key, unless the window is too narrow for words.
void Window::UpdateSortButton()
{
    RECT client;
    GetClientRect(m_hWnd, &client);
    std::wstring label = client.right >= Scale(theme::kSidebarMinWindow) ? SortLabel(static_cast<int>(m_sortKey)) : L"";
    m_titleBar.SetSort(label, m_sortDescending, TitleBar::SortButtonWidth(m_hWnd, m_renderer, label));
}

// The sort button's menu, under it: the keys, then the two orders named for the key in use.
void Window::OpenSortMenu()
{
    static const ContextMenu::Item keys[] = { ContextMenu::Item::kSortAdded, ContextMenu::Item::kSortName,
        ContextMenu::Item::kSortSize, ContextMenu::Item::kSortType };
    static const wchar_t *const ascending[] = { strings::kSortOldest, strings::kSortAToZ, strings::kSortSmallest, strings::kSortAToZ };
    static const wchar_t *const descending[] = { strings::kSortNewest, strings::kSortZToA, strings::kSortLargest, strings::kSortZToA };
    int key = static_cast<int>(m_sortKey);
    m_contextMenu.SetSortMarks(keys[key], m_sortDescending ? ContextMenu::Item::kSortDescending : ContextMenu::Item::kSortAscending,
        ascending[key], descending[key]);

    RECT button = m_titleBar.SortButtonRect();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, POINT{ button.right, button.bottom + Scale(4) }, ContextMenu::Kind::kSort, true);
    m_contextMenu.OnHover(CursorInWindow());
}

// Choosing a Library row. The Inbox row types "inbox:" into the one search box (or clears it); any other row first
// drops a bare inbox: from the text, so the Inbox does not quietly narrow a view it is not the row of (X2). On a
// board the text is the board's own search and stays.
void Window::ChooseView(SidebarView view)
{
    if (view == SidebarView::kInbox)
    {
        if (m_sidebar.View() != SidebarView::kAll)
            ShowView(SidebarView::kAll);
        SetQuery(m_sidebar.InboxShown() ? L"" : L"inbox:");
        return;
    }
    if (!BoardMode())
    {
        std::string text = WideToUtf8(m_searchBar.Query());
        std::string stripped = WithoutInbox(text);
        if (stripped != text)
            m_searchBar.SetText(Utf8ToWide(stripped));
    }
    ShowView(view);
}

// While the sidebar is hidden (Ctrl+B, or a window too narrow for it) the Library row shown is named at the left of
// the search row, so a view or a board is never an unmarked state (X3, decision D4). All files needs no name.
std::wstring Window::ViewLabelText() const
{
    bool sidebarShown = m_sidebarOpen && RoomForSidebar();
    if (sidebarShown || m_viewer.IsOpen() || m_settings.IsOpen())
        return L"";
    SidebarView view = m_sidebar.View();
    if (view == SidebarView::kAll)
        return m_sidebar.InboxShown() ? strings::kInbox : L"";
    return TagSidebar::ViewName(view);
}

void Window::SyncViewLabel()
{
    std::wstring label = ViewLabelText();
    if (label != m_viewLabel)
        LayoutComponents();
}

void Window::OpenViewMenu()
{
    static const SidebarView views[] = { SidebarView::kAll, SidebarView::kPictures, SidebarView::kVideos, SidebarView::kAudio,
        SidebarView::kBooks, SidebarView::kUntagged, SidebarView::kInbox, SidebarView::kBoards, SidebarView::kMissing };
    static const ContextMenu::Item items[] = { ContextMenu::Item::kViewAll, ContextMenu::Item::kViewPictures,
        ContextMenu::Item::kViewVideos, ContextMenu::Item::kViewAudio, ContextMenu::Item::kViewBooks, ContextMenu::Item::kViewUntagged,
        ContextMenu::Item::kViewInbox, ContextMenu::Item::kViewBoards, ContextMenu::Item::kViewMissing };
    SidebarView shown = m_sidebar.InboxShown() && m_sidebar.View() == SidebarView::kAll ? SidebarView::kInbox : m_sidebar.View();
    ContextMenu::Item mark = ContextMenu::Item::kNone;
    for (size_t i = 0; i < std::size(views); i++)
        mark = views[i] == shown ? items[i] : mark;
    m_contextMenu.SetSortMarks(mark, ContextMenu::Item::kNone, nullptr, nullptr);

    RECT label = m_titleBar.ViewLabelRect();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, POINT{ label.left, label.bottom + Scale(4) },
        m_sidebar.HasMissingRow() ? ContextMenu::Kind::kViewsMissing : ContextMenu::Kind::kViews);
    m_contextMenu.OnHover(CursorInWindow());
}

// Sets how the search treats a tag: required, excluded or off (the sidebar's click, its menu and keys).
void Window::SetTagState(const std::string &tag, TagState state)
{
    if (!CanFilterBy(tag))
        return;
    SetQuery(Utf8ToWide(WithTagState(WideToUtf8(m_searchBar.Query()), tag, state)));
}

// The tag menu of the sidebar, ticking the state the tag has now.
void Window::OpenTagMenu(const std::string &tag, POINT pt)
{
    m_menuTag = tag;
    TagState state = m_sidebar.StateOf(tag);
    m_sidebar.SetHold(true);  // the order stays as it is under the open menu (D3)
    m_contextMenu.SetSortMarks(state == TagState::kRequired ? ContextMenu::Item::kRequireTag
        : state == TagState::kExcluded ? ContextMenu::Item::kExcludeTag : ContextMenu::Item::kNone,
        ContextMenu::Item::kNone, nullptr, nullptr);
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, pt, ContextMenu::Kind::kTag);
    m_contextMenu.OnHover(pt);
}
