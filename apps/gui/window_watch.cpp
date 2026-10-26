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
#include "strings.hpp"
#include "ui_util.hpp"
#include <string>
#include <unordered_set>

// Starts keeping the watched folders in step with the disk, once the window shows: a rescan of each for files saved
// while Corpus was closed, then the files saved in them while it runs (see FolderWatch). The thread is given every
// path of the library, so it only reads the identity of files that are new.
void Window::StartWatching()
{
    std::unordered_set<std::string> known;
    if (!m_lib.watched_folders().empty())
    {
        known.reserve(m_lib.get_files().size());
        for (const file *f : m_lib.get_files())
            known.insert(f->get_path());
    }
    m_folderWatch.Start(m_hWnd, m_lib.watched_folders(), std::move(known), m_libraryFolder);
    WatchedFoldersChanged({});
}

// The library's watched folders changed (an import, Stop watching in the settings): the thread and the settings
// follow. 'known' holds the files an import just added, which the thread doesn't have to report.
void Window::WatchedFoldersChanged(std::vector<std::string> known)
{
    m_folderWatch.SetFolders(m_lib.watched_folders(), std::move(known));
    std::vector<std::wstring> folders;
    for (const std::string &folder : m_lib.watched_folders())
        folders.push_back(Utf8ToWide(folder));
    m_settings.SetWatchedFolders(std::move(folders));
}

// "Stop watching" in the settings. The folder's files stay in the library.
void Window::StopWatching(size_t index)
{
    const std::vector<std::string> &folders = m_lib.watched_folders();
    if (index >= folders.size())
        return;
    std::string folder = folders[index];
    m_lib.unwatch_folder(folder);
    ReportSaveError();
    if (!IsWindow(m_hWnd))
        return;
    WatchedFoldersChanged({});
    OnHover(CursorInWindow());
}

// A rescan found files: the new ones join the library untagged, and missing files found at a new path (moved within
// the drive) are relinked, keeping their tags. A quiet notice says so: "12 new files in Pictures".
void Window::OnWatchFound()
{
    std::vector<FolderWatch::Found> found;
    if (!m_folderWatch.Take(found))
        return;

    size_t added = 0, relinked = 0;
    std::string folder;
    bool several = false;
    for (const FolderWatch::Found &batch : found)
    {
        // Stopped watching since the rescan.
        if (!m_lib.is_watched(batch.folder))
            continue;
        watch_sync sync = m_lib.sync_watched(batch.files);
        if (sync.failed)
        {
            ReportSaveError();
            return;
        }
        if (sync.added.empty() && sync.relinked.empty())
            continue;
        if (!folder.empty() && folder != batch.folder)
            several = true;
        folder = batch.folder;
        added += sync.added.size();
        relinked += sync.relinked.size();
        // The rescan just saw them, so they aren't missing; a check under way must not bring them back.
        for (const file *f : sync.added)
        {
            m_missingPaths.erase(f->get_path());
            m_foundSinceCheck.insert(f->get_path());
        }
        for (size_t i = 0; i < sync.relinked.size(); i++)
        {
            const std::string &path = sync.relinked[i]->get_path();
            m_missingPaths.erase(sync.relinked_from[i]);
            m_missingPaths.erase(path);
            m_foundSinceCheck.insert(path);
            MoveSize(sync.relinked_from[i], path);
        }
    }
    if (added == 0 && relinked == 0)
        return;

    m_sortDirty = true;
    LibraryChanged();
    // The new files' sizes, for sorting by size, come with a check of the disk.
    if (added > 0 && m_sortKey == SortKey::kSize)
        CheckMissing();

    std::wstring text;
    if (added > 0)
        text = FormatCount(added) + L" " + (added == 1 ? strings::kNewFileIn : strings::kNewFilesIn) + L" " +
            (several ? std::wstring(strings::kWatchedFolders) : FolderName(folder));
    if (relinked > 0)
        text += (text.empty() ? L"" : L" · ") + FormatCount(relinked) + L" " +
            (relinked == 1 ? strings::kMovedFileFound : strings::kMovedFilesFound);
    ShowNotice(text);
}
