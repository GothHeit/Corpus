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
#include "strings.hpp"
#include "ui_util.hpp"
#include <string>

// Checks the disk for every file of the library, on a thread: when the window opens, each time Missing is picked,
// and each time the window comes back to the front while it shows, so files deleted or put back in the meantime
// are counted. Until it ends, Missing shows what the check before found.
void Window::CheckMissing()
{
    std::vector<std::string> paths;
    paths.reserve(m_lib.get_files().size());
    for (const file *f : m_lib.get_files())
        paths.push_back(f->get_path());
    m_missingCheck.Start(m_hWnd, std::move(paths));
}

void Window::OnMissingChecked()
{
    if (!m_missingCheck.Take(m_missingPaths, m_sizes))
        return;
    for (const std::string &path : m_foundSinceCheck)
        m_missingPaths.erase(path);
    m_foundSinceCheck.clear();
    m_missingChecked = true;
    CountViews();
    // The sizes came with the check.
    if (m_sortKey == SortKey::kSize)
        m_sortDirty = true;
    if (m_sidebar.View() == SidebarView::kMissing || m_sortKey == SortKey::kSize)
        RefreshResults();
    // Missing files are faded in every view.
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Missing says what it means over its files and offers to find them, or how far that got.
void Window::UpdateBanner()
{
    if (m_sidebar.View() != SidebarView::kMissing)
        m_fileGrid.SetBanner(L"", nullptr);
    else if (m_movedSearch.Running())
        m_fileGrid.SetBanner(std::wstring(strings::kLookingIn) + L" " + FolderName(m_movedSearch.Folder()) + L"…", strings::kCancel);
    else
        m_fileGrid.SetBanner(strings::kMissingBanner, strings::kFindMoved);
}

// "Find moved files..." in the Missing view: asks for a folder (the one the files were moved to, or one above it) and
// looks there for the missing files on a thread of its own (see MovedSearch). While it looks, the button cancels it.
void Window::FindMovedFiles()
{
    if (m_movedSearch.Running())
    {
        m_movedSearch.Stop();
        UpdateBanner();
        return;
    }
    std::vector<std::string> folders = PickFolders(m_hWnd, false);
    if (folders.empty())
        return;

    std::vector<MovedSearch::Missing> missing;
    for (const file *f : m_lib.get_files())
    {
        std::string path = f->get_path();
        if (m_missingPaths.count(path))
            missing.push_back({ path, f->get_identity() });
    }
    if (missing.empty())
        return;
    m_movedSearch.Start(m_hWnd, folders[0], std::move(missing));
    UpdateBanner();
}

// The search ended: what it found is shown, and only relinked if the user says so, since a file on a drive that
// reuses its file IDs could be taken for another.
void Window::OnMovedFound()
{
    std::string folder = m_movedSearch.Folder();
    std::vector<MovedSearch::Found> found;
    std::error_code error;
    if (!m_movedSearch.Take(found, error))
        return;
    UpdateBanner();

    // Only files still missing, going where no file of the library is. One found where the library already has a
    // file was imported again after it moved: said, rather than dropped without a word.
    std::vector<MovedSearch::Found> moves;
    size_t already = 0;
    for (MovedSearch::Found &f : found)
    {
        if (!m_lib.find_file(f.path) || !m_missingPaths.count(f.path))
            continue;
        if (m_lib.find_file(f.newPath))
            already++;
        else
            moves.push_back(std::move(f));
    }
    if (moves.empty())
    {
        if (error == std::errc::cross_device_link)
            ShowNotice(strings::kOtherDrive, true);
        else
        {
            std::wstring text = std::wstring(strings::kNoneMovedIn) + L" " + FolderName(folder);
            if (already > 0)
                text += L" \u00B7 " + FormatCount(already) + L" " + strings::kAlreadyThere;
            ShowNotice(text);
        }
        return;
    }

    size_t missing = 0;
    for (const file *f : m_lib.get_files())
        missing += m_missingPaths.count(f->get_path());
    ConfirmSheet::Question question;
    question.title = strings::kRelinkBefore + FormatCount(moves.size()) + strings::kRelinkOf + FormatCount(missing) +
        strings::kRelinkIn + FolderName(folder);
    question.body = strings::kRelinkBody;
    const size_t kShown = 5;
    for (size_t i = 0; i < moves.size() && i < kShown; i++)
        question.details.push_back(FolderName(moves[i].path) + L"  \u2192  " + Utf8ToWide(moves[i].newPath));
    if (moves.size() > kShown)
        question.details.push_back(std::wstring(strings::kRelinkMore) + L" " + FormatCount(moves.size() - kShown) + L" " + strings::kRelinkMoreAfter);
    question.confirm = strings::kRelinkConfirm;

    Ask(std::move(question), [this, moves]()
    {
        if (!BeginChange())
            return;
        for (const MovedSearch::Found &move : moves)
        {
            // Looked up again: the library may have changed while the question was up.
            file *f = m_lib.find_file(move.path);
            if (f && !m_lib.find_file(move.newPath))
                m_lib.relink(f, move.newPath);
        }
        m_sortDirty = true;
        FinishChange();
        if (!IsWindow(m_hWnd))
            return;

        // What the database kept: a failed save reloaded the library with the old paths, which stay missing.
        size_t relinked = 0;
        for (const MovedSearch::Found &move : moves)
        {
            if (m_lib.find_file(move.newPath))
            {
                m_missingPaths.erase(move.path);
                MoveSize(move.path, move.newPath);
                relinked++;
            }
        }
        LibraryChanged();
        if (relinked > 0)
            ShowNotice(std::wstring(strings::kRelinked) + L" " + FileCount(relinked));
    });
}

// A file now at another path keeps the size the last check of the disk read, for sorting by size.
void Window::MoveSize(const std::string &from, const std::string &to)
{
    auto size = m_sizes.find(from);
    if (size == m_sizes.end())
        return;
    std::uint64_t bytes = size->second;
    m_sizes.erase(size);
    m_sizes[to] = bytes;
}

// While a sheet, the viewer, the settings, the menu or a drag is up, a moved-file search that ends waits to ask its
// question: under the viewer a video would cover it, and the others take the mouse and keys it needs.
bool Window::HoldMovedResult()
{
    return OpenSheet() || m_viewer.IsOpen() || m_settings.IsOpen() || m_contextMenu.IsOpen() || Dragging();
}

// A moved-file search held by HoldMovedResult asks its question once nothing holds it.
void Window::ResumeHeldResults()
{
    if (!m_movedPending || HoldMovedResult())
        return;
    m_movedPending = false;
    PostMessage(m_hWnd, kMovedFound, 0, 0);
}
