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
void Window::CommitBoardQuery()
{
    board *b = m_board.Current();
    if (!b || !BoardMode())
        return;
    if (m_lastValidQuery != b->get_query() && !m_lib.set_board_query(b, m_lastValidQuery))
        ShowNotice(strings::kBoardSaveFailed, true);
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
