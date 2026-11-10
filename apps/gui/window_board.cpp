#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "image_decoder.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include "../../include/library.hpp"
#include <shobjidl.h>
#include <algorithm>
#include <string>

// The board view (see board_view.hpp) is the file area while the sidebar's Boards row is shown. A board is a saved
// search: the one search box holds its query, and the sidebar's tags rewrite it as they always do, so this adds no
// query box of its own. What the window does here is hand the board its files, keep the selection in step with the
// (hidden) grid so the info pane, tag bar and the keys keep working, and run the board's menus, rename, delete and
// PNG export.

// The board's selection is also the grid's, so everything that works on "the selected files" works on the board.
void Window::SyncBoardSelection()
{
    if (!m_board.TakeSelectionChanged())
        return;
    m_fileGrid.SetSelection(m_board.SelectedFiles());
    SelectionChanged();
}

// After anything the board did on its own (a press, a key): the selection, a refused save, the tag bar's place.
void Window::AfterBoardInput()
{
    SyncBoardSelection();
    std::wstring error = m_board.TakeError();
    if (!error.empty())
        ShowNotice(error, true);
    PlaceTagPanel();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// The selection bar floats over the bottom of the canvas, above the tray.
void Window::PlaceTagPanel()
{
    if (BoardMode())
        m_tagPanel.SetBounds(m_hWnd, m_renderer, m_board.CanvasRect());
}

// The search box's files, handed to the board; its query is saved as it changes.
void Window::RefreshBoard(const std::vector<file*> &files)
{
    // After a reload the library made new boards: the view gets its board again by id.
    if (!m_board.Current())
        m_board.SetBoards(m_lib.get_boards(), m_lib.find_board(m_boardId));
    m_board.SetFiles(files);
    m_board.SetQueryEmpty(m_searchBar.Query().empty());
    m_board.TakeSelectionChanged();
    m_fileGrid.SetSelection(m_board.SelectedFiles());
    PlaceTagPanel();
}

// The board's saved search is what was last committed: Enter or leaving the search, a click on a tag or elsewhere,
// another tab, leaving the Boards row. Typing shows results at once but only a commit is saved, and Esc in the search
// puts the saved text back, so an accidental edit costs nothing.
void Window::CommitBoardQuery(bool notify, bool record)
{
    board *b = m_board.Current();
    if (!b || !BoardMode())
        return;
    if (m_lastValidQuery == b->get_query())
        return;
    BoardEdit edit;
    edit.board = b->get_db_id();
    edit.label = strings::kBoardUndoSearch;
    edit.query = true;
    edit.queryBefore = b->get_query();
    edit.queryAfter = m_lastValidQuery;
    if (!m_lib.set_board_query(b, m_lastValidQuery))
    {
        ShowNotice(strings::kBoardSaveFailed, true);
        return;
    }
    if (record)
        RememberBoardEdit(std::move(edit));
    // A click on a tag changes the saved search without the user having typed it: say so, and that Ctrl+Z takes it back.
    if (notify)
        ShowNotice(strings::kBoardQueryChanged);
}

// A move, placement or take-off on the canvas, or a changed search, for Ctrl+Z.
void Window::RememberBoardEdit(BoardEdit edit)
{
    // Keys held down nudge or resize over and over: one step for the run, from the first 'before' to the last 'after'.
    ULONGLONG now = GetTickCount64();
    bool repeat = now - m_lastBoardEdit < 400;
    m_lastBoardEdit = now;
    if (repeat && !edit.query && m_redo.empty() && !m_undo.empty() && m_undo.back().boardEdits.size() == 1 &&
        (edit.label == strings::kBoardUndoMoved || edit.label == strings::kBoardUndoResized))
    {
        BoardEdit &top = m_undo.back().boardEdits[0];
        bool same = !top.query && top.board == edit.board && top.label == edit.label && top.items.size() == edit.items.size();
        for (size_t i = 0; same && i < edit.items.size(); i++)
            same = top.items[i].file == edit.items[i].file;
        if (same)
        {
            for (size_t i = 0; i < edit.items.size(); i++)
            {
                top.items[i].hasAfter = edit.items[i].hasAfter;
                top.items[i].after = edit.items[i].after;
            }
            return;
        }
    }
    UndoStep step;
    step.label = edit.label;
    step.boardEdits.push_back(std::move(edit));
    Remember(std::move(step));
}

// Makes a board change again ('forward') or takes it back, inside the change RunStep has begun. A file gone since is
// skipped. 'shown' gets the board the change was on, so the view can go there afterwards.
bool Window::RunBoardEdit(const BoardEdit &edit, bool forward, std::int64_t &shown)
{
    board *b = m_lib.find_board(edit.board);
    if (!b)
        return true;
    shown = edit.board;
    if (edit.query)
    {
        bool ok = m_lib.set_board_query(b, forward ? edit.queryAfter : edit.queryBefore);
        if (BoardMode() && b == m_board.Current())
            m_searchBar.SetText(Utf8ToWide(b->get_query()));
        return ok;
    }
    std::vector<board_item> place;
    std::vector<std::int64_t> takeOff;
    // A file removed from the library since can't be placed again, and one refusal would drop the whole batch.
    std::unordered_set<std::int64_t> alive;
    for (const file *f : m_lib.get_files())
        alive.insert(f->get_db_id());
    for (const BoardEdit::Entry &entry : edit.items)
    {
        if (!alive.count(entry.file))
            continue;
        bool placed = forward ? entry.hasAfter : entry.hadBefore;
        if (placed)
            place.push_back(forward ? entry.after : entry.before);
        else
            takeOff.push_back(entry.file);
    }
    bool ok = true;
    if (!place.empty())
        ok = m_lib.place_on_board(b, place);
    if (!takeOff.empty())
        ok = m_lib.unplace_from_board(b, takeOff) && ok;
    return ok;
}

void Window::BoardsChanged()
{
    CountViews();
}

// Shows a board (nullptr: none exists): its query goes in the search box, which is where it is edited.
void Window::OpenBoard(board *b, bool commit)
{
    if (commit && b != m_board.Current())
        CommitBoardQuery();
    m_boardId = b ? b->get_db_id() : 0;
    if (b)
        m_searchBar.SetText(Utf8ToWide(b->get_query()));
    m_board.SetBoards(m_lib.get_boards(), b);
    LayoutComponents();
    RefreshResults();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// A board of what the search finds now. It is named "Board 2", "Board 3"...; the tab's menu renames it.
void Window::NewBoard()
{
    std::string query = WideToUtf8(m_searchBar.Query());
    std::string name;
    for (size_t n = m_lib.get_boards().size() + 1;; n++)
    {
        name = WideToUtf8(strings::kBoardName) + " " + std::to_string(n);
        bool taken = false;
        for (const board *other : m_lib.get_boards())
            taken = taken || other->get_name() == name;
        if (!taken)
            break;
    }
    board *b = m_lib.add_board(name, query);
    if (!b)
    {
        ShowNotice(strings::kBoardSaveFailed, true);
        return;
    }
    BoardsChanged();
    OpenBoard(b, false);
}

void Window::HandleBoardAction(BoardView::Action action)
{
    const std::vector<board*> &boards = m_lib.get_boards();
    switch (action)
    {
    case BoardView::Action::kSwitch:
    {
        size_t index = static_cast<size_t>(m_board.SwitchIndex());
        if (index < boards.size() && boards[index] != m_board.Current())
            OpenBoard(boards[index]);
        break;
    }
    case BoardView::Action::kNewBoard:
        NewBoard();
        break;
    case BoardView::Action::kExport:
        ExportBoard();
        break;
    case BoardView::Action::kOpen:
        if (file *f = m_board.OpenedFile())
            ViewFile(f, m_fileGrid.Files());
        break;
    case BoardView::Action::kPlayVideo:
        PlayBoardVideo(m_board.OpenedFile());
        break;
    case BoardView::Action::kPlaySong:
        PlayBoardSong(m_board.OpenedFile());
        break;
    default:
        break;
    }
}

// A right click on a board's tab or on a file of the board.
void Window::OpenBoardMenu(BoardView::Action action, POINT pt)
{
    ContextMenu::Kind kind = ContextMenu::Kind::kBoardFile;
    if (action == BoardView::Action::kTabMenu)
    {
        const std::vector<board*> &boards = m_lib.get_boards();
        size_t index = static_cast<size_t>(m_board.SwitchIndex());
        if (index >= boards.size())
            return;
        // The menu's actions are about the board in view.
        m_menuBoard = boards[index]->get_db_id();
        if (boards[index] != m_board.Current())
            OpenBoard(boards[index]);
        kind = ContextMenu::Kind::kBoard;
    }
    else if (action == BoardView::Action::kFileMenu)
        AfterBoardInput();
    else if (action == BoardView::Action::kCanvasMenu)
        kind = ContextMenu::Kind::kBoardCanvas;
    else
        return;
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, pt, kind);
    m_contextMenu.OnHover(pt);
}

void Window::StartBoardRename(std::int64_t id)
{
    board *b = m_lib.find_board(id);
    if (!b)
        return;
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renameBoard = id;
    m_renameTag.clear();
    m_renamePath.clear();
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_rename.Open(m_hWnd, m_renderer, client, Utf8ToWide(b->get_name()), true, strings::kBoardRenameTitle);
    m_rename.OnHover(CursorInWindow());
}

// The rename sheet's answer for a board. On failure 'error' says why, to show in the sheet.
bool Window::RenameBoardNow(std::wstring &error)
{
    board *b = m_lib.find_board(m_renameBoard);
    std::wstring name = m_rename.Name();
    while (!name.empty() && name.back() == L' ')
        name.pop_back();
    size_t first = name.find_first_not_of(L' ');
    name = first == std::wstring::npos ? L"" : name.substr(first);
    if (!b)
        return true;  // gone meanwhile
    if (name.empty())
    {
        error = strings::kRenameEmpty;
        return false;
    }
    if (!m_lib.rename_board(b, WideToUtf8(name)))
    {
        error = strings::kBoardSaveFailed;
        return false;
    }
    m_board.SetBoards(m_lib.get_boards(), m_board.Current());
    return true;
}

void Window::AskDeleteBoard(std::int64_t id)
{
    board *b = m_lib.find_board(id);
    if (!b)
        return;
    ConfirmSheet::Question question;
    question.title = strings::kBoardDeleteTitle;
    question.body = strings::kBoardDeleteBody;
    question.details.push_back(Utf8ToWide(b->get_name()));
    question.confirm = L"Delete";
    question.danger = true;
    Ask(question, [this, id]()
    {
        board *gone = m_lib.find_board(id);
        if (!gone)
            return;
        // The view lets go of the board before the library frees it.
        m_board.SetBoards(m_lib.get_boards(), nullptr);
        if (!m_lib.delete_board(gone))
        {
            ShowNotice(strings::kBoardSaveFailed, true);
            return;
        }
        BoardsChanged();
        m_board.SweepFrames();
        const std::vector<board*> &boards = m_lib.get_boards();
        OpenBoard(boards.empty() ? nullptr : boards.front(), false);
    });
}

// Asks where to save the PNG, then draws the board on a thread of its own.
void Window::ExportBoard()
{
    board *b = m_board.Current();
    if (!b || m_boardExporter.Running())
        return;
    std::vector<BoardExportItem> items = m_board.ExportItems();
    if (items.empty())
    {
        ShowNotice(strings::kBoardNothingToExport, true);
        return;
    }
    for (BoardExportItem &item : items)
    {
        item.psd = IsPsd(item.path);
        item.picture = !item.psd && IsDecodableImage(item.path);
    }

    ComPtr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);
    COMDLG_FILTERSPEC filter = { strings::kBoardExportFile, L"*.png" };
    dialog->SetFileTypes(1, &filter);
    dialog->SetDefaultExtension(L"png");
    std::wstring name = Utf8ToWide(b->get_name());
    for (wchar_t &ch : name)
    {
        if (ch < L' ' || wcschr(L"\\/:*?\"<>|", ch))
            ch = L'_';
    }
    dialog->SetFileName((name + L".png").c_str());
    if (dialog->Show(m_hWnd) != S_OK)
        return;
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    std::wstring target;
    if (SUCCEEDED(dialog->GetResult(item.GetAddressOf())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        target = path;
    CoTaskMemFree(path);
    if (target.empty())
        return;
    if (m_boardExporter.Start(m_hWnd, target, std::move(items)))
        m_board.SetExporting(true);
}

void Window::OnBoardExported(bool done)
{
    m_board.SetExporting(false);
    ShowNotice(done ? strings::kBoardExported : strings::kBoardExportFailed, !done);
}

// A video on the board plays in its tile, over any music playing (user choice: the two may sound together); M mutes
// it (ToggleBoardMute).
void Window::PlayBoardVideo(file *f)
{
    if (!f)
        return;
    m_board.PlayVideo(f, m_viewer.Volume(), m_viewer.OutputDevice(false));
    AfterBoardInput();
}

// M on the board: the video in its tile goes silent or sounds again, for the rest of the session. False with none.
bool Window::ToggleBoardMute()
{
    bool muted = false;
    if (!m_board.ToggleVideoMute(muted))
        return false;
    ShowNotice(muted ? strings::kBoardVideoMuted : strings::kBoardVideoSound);
    return true;
}

// A song on the board plays in the strip, with the board's songs as its list; the board stays. The song already in
// the strip plays or pauses instead, like Space on it in the grid.
void Window::PlayBoardSong(file *f)
{
    if (!f)
        return;
    std::string song;
    bool playing = false;
    if (m_viewer.NowPlaying(song, playing) && song == f->get_path())
    {
        HandleViewerAction(m_viewer.OnMedia(Viewer::Media::kPlayPause));
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }
    std::vector<std::string> paths;
    size_t index = 0;
    for (const file *other : m_fileGrid.Files())
    {
        if (other == f)
            index = paths.size();
        if (other == f || IsPlayableAudio(other->get_path()))
            paths.push_back(other->get_path());
    }
    if (paths.empty() || paths[index] != f->get_path())
    {
        paths = { f->get_path() };
        index = 0;
    }
    // Opened and closed at once: the music goes on in the strip without the stage ever showing.
    HandleViewerAction(m_viewer.Open(std::move(paths), index));
    if (m_viewer.IsOpen())
        m_viewer.Close();
    LayoutComponents();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Before each paint: the board's video hides under whatever the window draws over it and pauses under the viewer and
// the settings. Music and the video may play together.
void Window::SyncBoardPlayer()
{
    if (!BoardMode())
    {
        m_board.StopVideo();
        return;
    }
    std::string song;
    bool playing = false;
    if (!m_viewer.NowPlaying(song, playing))
        song.clear(), playing = false;
    m_board.SetSong(song, playing);
    bool away = m_viewer.IsOpen() || m_settings.IsOpen();
    if (away)
        m_board.PauseVideo();
    m_board.SetPlayerCovered(away || OpenSheet() != nullptr || m_contextMenu.IsOpen() || m_dragActive || m_fileDragActive);
    m_board.SetObstacle(m_tagPanel.ShownRect());
}
