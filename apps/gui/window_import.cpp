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
#include "../../include/file_identity.hpp"
#include "../../include/text.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <wrl/client.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

// While an import runs, the import bar is redrawn this often: smooth for the moving bar, or just enough to keep
// the count current when Windows animations are off. The result then stays up for a few seconds.
static const UINT kAnimationMs = 33;
static const UINT kCountMs = 250;
static const UINT kResultMs = 4000;

// The last part of a path: "Pictures" for C:\Users\me\Pictures.
std::wstring FolderName(const std::string &path)
{
    std::wstring wide = Utf8ToWide(path);
    while (wide.size() > 1 && (wide.back() == L'\\' || wide.back() == L'/'))
        wide.pop_back();
    size_t slash = wide.find_last_of(L"\\/");
    return slash == std::wstring::npos || slash + 1 == wide.size() ? wide : wide.substr(slash + 1);
}

// Asks for folders with the system's folder picker: several, or just one. Empty if none was picked.
std::vector<std::string> PickFolders(HWND hWnd, bool several)
{
    std::vector<std::string> paths;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))))
        return paths;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | (several ? static_cast<DWORD>(FOS_ALLOWMULTISELECT) : 0));
    ComPtr<IShellItemArray> items;
    if (dialog->Show(hWnd) != S_OK || FAILED(dialog->GetResults(items.GetAddressOf())))
        return paths;

    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; i++)
    {
        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, item.GetAddressOf())) &&
            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        {
            paths.push_back(WideToUtf8(path));
            CoTaskMemFree(path);
        }
    }
    return paths;
}

// ---- Inbox and drops ----

// Starts the Inbox (main window only): its folder (inbox::Folder), watched like any other the first time (so Stop
// watching in the settings is kept), and the drop target that takes files from Explorer and pictures from a browser.
// A secondary window takes drops of files only, as before, and has no Inbox.
void Window::SetUpInbox()
{
    if (!m_secondary)
    {
        m_inboxFolder = inbox::Folder(m_libraryFolder);
        m_settings.SetFolders(Utf8ToWide(m_inboxFolder), Utf8ToWide(m_libraryFolder));
        // The flag is the real catalog's: a test library just watches its own Inbox, and never sets it.
        DWORD done = 0, size = sizeof(done);
        if (!inbox::Movable())
        {
            if (!m_lib.is_watched(m_inboxFolder))
                m_lib.watch_folder(m_inboxFolder);
        }
        else if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"InboxWatched", RRF_RT_REG_DWORD, nullptr, &done, &size) != ERROR_SUCCESS ||
            !done)
        {
            if (m_lib.is_watched(m_inboxFolder) || m_lib.watch_folder(m_inboxFolder))
            {
                DWORD one = 1;
                RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"InboxWatched", REG_DWORD, &one, sizeof(one));
            }
        }
    }
    else
        m_settings.SetFolders(L"", Utf8ToWide(m_libraryFolder));
    m_dropTarget = new inbox::DropTarget(
        [this](IDataObject *data, POINT screen) { return WantsDrop(data, screen); },
        [this](IDataObject *data, POINT screen) { OnOleDrop(data, screen); },
        [this]() { m_sidebar.SetDropInbox(false); });
    RegisterDragDrop(m_hWnd, m_dropTarget);
}

// Whether a drag over the window is taken: files, or (outside a secondary window) anything Capture can save.
// Files of the library dragged out and dropped back in are not imported again.
bool Window::WantsDrop(IDataObject *data, POINT screen)
{
    if (m_draggingOut)
        return false;
    // The Inbox row glows while files that would be copied into it are over it.
    POINT pt = screen;
    ScreenToClient(m_hWnd, &pt);
    m_sidebar.SetDropInbox(!m_secondary && inbox::HasFiles(data) && m_sidebar.OnInboxRow(pt));
    return inbox::HasFiles(data) || (!m_secondary && inbox::CanCapture(data));
}

// Files dropped on the Inbox row are copied into the Inbox; files anywhere else are added to the library where they
// are (nothing copied or moved), with everything inside folders. Pictures from a browser go to the Inbox.
void Window::OnOleDrop(IDataObject *data, POINT screen)
{
    POINT pt = screen;
    ScreenToClient(m_hWnd, &pt);
    if (inbox::HasFiles(data) && (m_secondary || !m_sidebar.OnInboxRow(pt)))
    {
        // The import bar and its outcome are under the viewer: it closes first, so the drop doesn't go unseen.
        CloseViewer();
        CloseSettings();
        AddPaths(inbox::Files(data));
        return;
    }
    CloseViewer();
    CloseSettings();
    CaptureIntoInbox(data);
}

// Whether 'path' is 'folder' or inside it (UTF-8, case aside, as Windows sees paths).
static bool PathWithin(const std::string &path, const std::string &folder)
{
    std::wstring p = Utf8ToWide(path), f = Utf8ToWide(folder);
    while (!f.empty() && (f.back() == L'\\' || f.back() == L'/'))
        f.pop_back();
    if (p.size() < f.size() || _wcsnicmp(p.c_str(), f.c_str(), f.size()) != 0)
        return false;
    return p.size() == f.size() || p[f.size()] == L'\\' || p[f.size()] == L'/';
}

void Window::MoveInbox()
{
    if (m_secondary)
        return;
    if (!inbox::Movable())
    {
        ShowNotice(strings::kInboxTestLibrary, true);
        return;
    }
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))))
        return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(strings::kInboxFolderTitle);
    ComPtr<IShellItem> start;
    if (SUCCEEDED(SHCreateItemFromParsingName(Utf8ToWide(m_inboxFolder).c_str(), nullptr, IID_PPV_ARGS(start.GetAddressOf()))))
        dialog->SetFolder(start.Get());
    ComPtr<IShellItem> item;
    PWSTR picked = nullptr;
    if (dialog->Show(m_hWnd) != S_OK || FAILED(dialog->GetResult(item.GetAddressOf())) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked)))
        return;
    std::string to = WideToUtf8(picked);
    CoTaskMemFree(picked);
    std::string from = m_inboxFolder;
    if (PathWithin(to, from) && PathWithin(from, to))
        return;
    // Deleting the library (the uninstaller asks) must never take the Inbox with it; and a folder that holds the
    // library (the profile, AppData) would be watched whole, catalog and caches with it.
    if (PathWithin(to, m_libraryFolder))
    {
        ShowNotice(strings::kInboxInLibrary, true);
        return;
    }
    if (PathWithin(m_libraryFolder, to))
    {
        ShowNotice(strings::kInboxAboveLibrary, true);
        return;
    }
    // The files move on the Inbox worker (instant on one drive, copies across drives); the catalog follows when they
    // are there (FinishInboxMove). A capture still being saved goes first, so nothing lands behind the move.
    if (m_inboxWorker.Busy())
    {
        ShowNotice(m_inboxWorker.Moving() ? strings::kInboxBusy : strings::kInboxSaving, true);
        return;
    }
    m_inboxMoveWatched = m_lib.is_watched(from);
    m_inboxWorker.Move(m_hWnd, from, to);
    ShowNotice(strings::kInboxMoving);
}

void Window::ShowInboxMoveProgress()
{
    size_t done = 0, total = 0;
    m_inboxWorker.Progress(done, total);
    if (m_inboxWorker.Moving() && total > 0)
        ShowNotice(std::wstring(strings::kInboxMoving) + L" \u00B7 " + FormatCount(done) + L" " + strings::kInboxMovingOf +
            L" " + FileCount(total));
}

// What the Inbox worker finished: captures get their notice and join the library, a move gets its catalog change.
void Window::OnInboxWork()
{
    std::vector<inbox::Outcome> done;
    if (!m_inboxWorker.Take(done))
        return;
    for (inbox::Outcome &outcome : done)
    {
        if (!IsWindow(m_hWnd))
            return;
        if (!outcome.move)
            CaptureDone(outcome);
        else if (!outcome.back)
            FinishInboxMove(outcome);
    }
    // Rescans held back while the Inbox moved.
    if (m_watchPending && !m_inboxWorker.Moving() && IsWindow(m_hWnd))
    {
        m_watchPending = false;
        PostMessage(m_hWnd, kWatchFound, 0, 0);
    }
}

// The files of the Inbox are in 'done.to': the catalog follows them and the setting changes, as one change. If the
// catalog or the setting can't take it, the files go back.
void Window::FinishInboxMove(inbox::Outcome &done)
{
    const std::string from = done.from, to = done.to;
    bool watched = m_inboxMoveWatched;
    if (!BeginChange())
    {
        m_inboxWorker.MoveBack(m_hWnd, done.moved);
        ShowNotice(strings::kInboxMoveFailed, true);
        return;
    }
    bool empty = inbox::IsEmpty(from);
    // The new folder is watched only if the old one was: Stop watching on the Inbox is kept.
    bool watchedTo = watched && !m_lib.is_watched(to) && m_lib.watch_folder(to);
    for (const auto &move : done.moved)
    {
        file *f = m_lib.find_file(move.first);
        if (f && !m_lib.find_file(move.second))
            m_lib.relink(f, move.second);
    }
    // An old Inbox emptied out goes, and stops being watched; one with something left (a subfolder, a file in use)
    // stays watched, so nothing in it drops out of sight.
    if (empty && watched)
        m_lib.unwatch_folder(from);
    m_sortDirty = true;
    bool saved = FinishChange();
    if (!saved || !inbox::SetFolder(to))
    {
        // The catalog still has the old paths (or the setting the old folder): the files go back where it says.
        if (saved && BeginChange())
        {
            // Saved but the setting refused: the catalog's half is taken back too, watched folders with it.
            for (const auto &move : done.moved)
            {
                file *f = m_lib.find_file(move.second);
                if (f && !m_lib.find_file(move.first))
                    m_lib.relink(f, move.first);
            }
            if (watchedTo)
                m_lib.unwatch_folder(to);
            if (watched && !m_lib.is_watched(from))
                m_lib.watch_folder(from);
            if (FinishChange() && IsWindow(m_hWnd))
                WatchedFoldersChanged({});
        }
        if (!IsWindow(m_hWnd))
            return;
        m_inboxWorker.MoveBack(m_hWnd, done.moved);
        ShowNotice(strings::kInboxMoveFailed, true);
        return;
    }
    if (!IsWindow(m_hWnd))
        return;
    m_inboxFolder = to;
    m_settings.SetFolders(Utf8ToWide(m_inboxFolder), Utf8ToWide(m_libraryFolder));
    if (empty)
        RemoveDirectoryW(FileWide(from).c_str());
    std::vector<std::string> known;
    for (const auto &move : done.moved)
    {
        if (m_lib.find_file(move.second))
        {
            m_missingPaths.erase(move.first);
            MoveSize(move.first, move.second);
            known.push_back(move.second);
        }
    }
    m_fileGrid.Clear();
    m_board.Clear();
    WatchedFoldersChanged(std::move(known));
    LibraryChanged();
    std::wstring notice = std::wstring(strings::kInboxMovedTo) + L" " + FolderName(to);
    if (!empty)
    {
        // What is left in the old folder: its files that didn't move (subfolders aside).
        size_t left = 0;
        WIN32_FIND_DATAW found = {};
        HANDLE find = FindFirstFileW(FileWide(Utf8ToWide(from) + L"\\*").c_str(), &found);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    left++;
            } while (FindNextFileW(find, &found));
            FindClose(find);
        }
        if (left > 0)
            notice += L" \u00B7 " + FileCount(left) + L" " + strings::kInboxLeftBehind;
    }
    ShowNotice(notice);
}

void Window::OpenCatalogFolder()
{
    ShellExecuteW(m_hWnd, L"open", Utf8ToWide(m_libraryFolder).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Ctrl+V: a picture on the clipboard (or files copied in Explorer) is saved into the Inbox.
void Window::PasteIntoInbox()
{
    if (m_secondary)
        return;
    ComPtr<IDataObject> data;
    if (FAILED(OleGetClipboard(&data)) || !inbox::CanCapture(data.Get()))
    {
        ShowNotice(strings::kNothingToPaste);
        return;
    }
    CaptureIntoInbox(data.Get());
}

// Saves what the data holds into the Inbox, on the Inbox worker (CaptureDone then adds it to the library; the watcher
// would find it too, this just doesn't wait for it). Files already in the library aren't copied again.
void Window::CaptureIntoInbox(IDataObject *data)
{
    if (m_inboxWorker.Moving())
    {
        ShowNotice(strings::kInboxBusy, true);
        return;
    }
    m_inboxFolder = inbox::Folder(m_libraryFolder);
    bool started = false;
    inbox::Outcome left = m_inboxWorker.Capture(m_hWnd, data, m_inboxFolder,
        [this](const std::string &path) { return m_lib.find_file(path) != nullptr; }, started);
    if (started)
        ShowNotice(strings::kSavingToInbox);
    else
        CaptureDone(left);
}

void Window::CaptureDone(inbox::Outcome &done)
{
    size_t left = done.known + done.tooBig + done.refused + done.failed;
    if (done.saved.empty())
    {
        if (done.tooBig)
            ShowNotice(strings::kTooBigForInbox, true);
        else if (done.refused)
            ShowNotice(strings::kShortcutsRefused, true);
        else if (done.failed)
            ShowNotice(strings::kSaveToInboxFailed, true);
        else if (done.known)
            ShowNotice(strings::kAlreadyInLibrary);
        else
            ShowNotice(strings::kNothingToPaste, true);
        return;
    }
    std::wstring text = done.saved.size() == 1 ? std::wstring(strings::kSavedToInbox)
                                               : L"Saved " + FormatCount(done.saved.size()) + L" " + strings::kFilesToInbox;
    if (left > 0)
        text += L" \u00B7 " + FormatCount(left) + L" " + strings::kInboxNotSaved;
    ShowNotice(text);
    AddPaths(std::move(done.saved));
}

// "Add folder..." (under an empty library, the + of the search row, the grid's menu, Ctrl+O): the folders picked are
// added as if they had been dropped.
void Window::ChooseFolders()
{
    AddPaths(PickFolders(m_hWnd, true));
}

// Imports dropped or chosen files and folders, or queues them behind the import that is running.
void Window::AddPaths(std::vector<std::string> paths)
{
    if (paths.empty())
        return;
    if (m_importThread.joinable())
        m_queuedDrops.insert(m_queuedDrops.end(), paths.begin(), paths.end());
    else
        StartImport(std::move(paths));
}

// Reading a big folder can take minutes (every file is opened for its identity), so it happens on another thread
// while the window stays responsive. The import bar shows how far it got and can cancel it. When the thread is
// done, FinishImport adds what it found to the library in one transaction.
void Window::StartImport(std::vector<std::string> paths)
{
    KillTimer(m_hWnd, kResultTimer);
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        m_importFound = 0;
        m_importFolder.clear();
        m_importCurrent.clear();
    }
    m_cancelImport = false;

    bool wasVisible = m_importBar.Visible();
    m_importBar.ShowProgress(strings::kImporting, L"");
    if (!wasVisible)
    {
        LayoutComponents();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
    else
        PlaceSelectionBar();  // an import lifts the bar so Cancel stays in reach

    SetTimer(m_hWnd, kImportTimer, AnimationsEnabled() ? kAnimationMs : kCountMs, nullptr);

    m_importThread = std::thread(&Window::ScanDropped, this, std::move(paths));
}

// Runs on the import thread: lists the dropped folders and reads the identity of every file found. Never touches
// the library or the window, only the members shared under m_importMutex.
void Window::ScanDropped(std::vector<std::string> paths)
{
    folder_scan found;
    std::vector<std::string> unopened, roots;
    for (const std::string &path : paths)
    {
        if (m_cancelImport)
        {
            found.cancelled = true;
            break;
        }

        size_t before = found.files.size();
        DWORD attributes = GetFileAttributesW(FileWide(path).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            {
                std::lock_guard<std::mutex> lock(m_importMutex);
                m_importFolder = path;
            }
            folder_scan scan = scan_folder(path, &m_cancelImport, [&](size_t count, const std::string &current)
            {
                std::lock_guard<std::mutex> lock(m_importMutex);
                m_importFound = before + count;
                m_importCurrent = current;
            });
            if (scan.error)
                unopened.push_back(path);
            else
                roots.push_back(path);
            found.files.insert(found.files.end(), std::make_move_iterator(scan.files.begin()),
                std::make_move_iterator(scan.files.end()));
            found.unreadable += scan.unreadable;
            found.skipped += scan.skipped;
            if (scan.cancelled)
            {
                found.cancelled = true;
                break;
            }
        }
        else
        {
            found.files.push_back({ path, file_identity(path) });
            std::lock_guard<std::mutex> lock(m_importMutex);
            m_importFound = found.files.size();
            m_importFolder.clear();
            m_importCurrent = path;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        m_importResult = std::move(found);
        m_importUnopened = std::move(unopened);
        m_importRoots = std::move(roots);
    }
    PostMessage(m_hWnd, kImportDone, 0, 0);
}

// Shows how many files the import thread found so far, and in which folder: the path from the dropped folder
// down, like Pictures\2023.
void Window::ShowImportProgress()
{
    if (m_cancelImport)
    {
        m_importBar.ShowProgress(strings::kCancelling, L"");
        m_importBar.Animate();
        return;
    }

    size_t found;
    std::string folder, current;
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        found = m_importFound;
        folder = m_importFolder;
        current = m_importCurrent;
    }

    std::wstring text = strings::kImporting;
    if (found > 0)
        text += L" \u00B7 " + FormatCount(found) + L" " + strings::kFound;

    std::wstring detail;
    if (!folder.empty() && !current.empty())
    {
        std::wstring root = Utf8ToWide(folder);
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
            root.pop_back();
        std::wstring path = Utf8ToWide(current);
        size_t name = path.find_last_of(L"\\/");
        if (name != std::wstring::npos)
            path.erase(name);
        // The scan returns the paths starting from the dropped folder, made absolute.
        size_t parent = root.find_last_of(L"\\/");
        if (parent != std::wstring::npos && path.compare(0, parent + 1, root, 0, parent + 1) == 0)
            path.erase(0, parent + 1);
        detail = path;
    }

    m_importBar.ShowProgress(text, detail);
    m_importBar.Animate();
}

// Called on the window's thread once the import thread is done: adds the files it found, unless the import was
// cancelled, and shows the outcome for a few seconds.
void Window::FinishImport()
{
    if (!m_importThread.joinable())
        return;
    m_importThread.join();
    KillTimer(m_hWnd, kImportTimer);

    folder_scan found;
    std::vector<std::string> unopened, roots;
    {
        std::lock_guard<std::mutex> lock(m_importMutex);
        found = std::move(m_importResult);
        unopened = std::move(m_importUnopened);
        roots = std::move(m_importRoots);
        m_importResult = folder_scan();
        m_importRoots.clear();
    }

    std::wstring outcome;
    bool error = false;
    if (found.cancelled)
        outcome = strings::kImportCancelled;
    else
    {
        // Counted before adding, which makes them part of the library. A file dropped twice counts once.
        std::unordered_set<std::string> already;
        for (const scanned_file &s : found.files)
        {
            if (m_lib.find_file(s.path))
                already.insert(s.path);
        }

        // add_files saves them in a transaction of its own, so this isn't a BeginChange/FinishChange change.
        int added = m_lib.add_files(found.files);
        if (added < 0)
        {
            outcome = strings::kNotSaved;
            error = true;
            ReportSaveError();
            if (!IsWindow(m_hWnd))
                return;
        }
        else
        {
            std::wstring separator = L" \u00B7 ";
            if (added > 0)
                outcome = std::wstring(strings::kAdded) + L" " + FileCount(static_cast<size_t>(added));
            else if (!found.files.empty() || unopened.empty())
                outcome = strings::kNothingNew;
            if (!already.empty())
                outcome += separator + FormatCount(already.size()) + L" " + strings::kAlreadyThere;
            if (found.skipped > 0)
                outcome += separator + FormatCount(static_cast<size_t>(found.skipped)) + L" " + strings::kSkipped;
            if (found.unreadable == 1)
                outcome += separator + strings::kOneUnreadable;
            else if (found.unreadable > 1)
                outcome += separator + FormatCount(static_cast<size_t>(found.unreadable)) + L" " + strings::kUnreadableFolders;
            error = found.unreadable > 0 || !unopened.empty();
            for (const std::string &folder : unopened)
                outcome += (outcome.empty() ? L"" : separator) + strings::kCouldNotOpen + L" " + FolderName(folder);
            // The scan just found them, so they aren't missing, even those already in the library.
            // A check under way may have read the disk before then, so its result must not bring them back.
            for (const scanned_file &s : found.files)
            {
                m_missingPaths.erase(s.path);
                m_foundSinceCheck.insert(s.path);
            }
            m_sortDirty = true;
            LibraryChanged();
            // The new files' sizes, for sorting by size, come with a check of the disk.
            if (added > 0 && m_sortKey == SortKey::kSize)
                CheckMissing();
            if (added > 0)
            {
                ScanAudioTags();
                IndexContent();
            }

            // The folder names of the files that are new, for the user to tick (see OfferFolderTags).
            std::vector<scanned_file> fresh;
            for (const scanned_file &s : found.files)
            {
                if (!already.count(s.path))
                    fresh.push_back(s);
            }
            std::unordered_map<std::string, size_t> offered;  // by folded name
            for (size_t i = 0; i < m_folderTagOffer.size(); i++)
                offered[fold_case(m_folderTagOffer[i].name)] = i;
            for (const std::string &root : roots)
            {
                for (const folder_tag_suggestion &suggestion : suggest_folder_tags(root, fresh))
                {
                    auto slot = offered.emplace(fold_case(suggestion.name), m_folderTagOffer.size());
                    if (slot.second)
                    {
                        // A tag that exists keeps its spelling: Beach goes on beach.
                        tag *existing = m_lib.find_tag(suggestion.name);
                        m_folderTagOffer.push_back({ existing ? existing->get_id() : suggestion.name, {}, true });
                    }
                    std::vector<std::string> &paths = m_folderTagOffer[slot.first->second].paths;
                    for (size_t index : suggestion.files)
                        paths.push_back(fresh[index].path);
                }
            }
            for (FolderTag &offer : m_folderTagOffer)
            {
                std::sort(offer.paths.begin(), offer.paths.end());
                offer.paths.erase(std::unique(offer.paths.begin(), offer.paths.end()), offer.paths.end());
            }

            // The folders read are watched from now on: files saved in them later come in on their own.
            if (!roots.empty())
            {
                for (const std::string &root : roots)
                {
                    if (!m_lib.watch_folder(root))
                        break;
                }
                ReportSaveError();
                if (!IsWindow(m_hWnd))
                    return;
                std::vector<std::string> known;
                known.reserve(found.files.size());
                for (const scanned_file &s : found.files)
                    known.push_back(s.path);
                WatchedFoldersChanged(std::move(known));
            }
        }
    }

    if (!m_queuedDrops.empty())
    {
        std::vector<std::string> next;
        next.swap(m_queuedDrops);
        StartImport(std::move(next));
        return;
    }

    m_importBar.ShowResult(outcome, error);
    PlaceSelectionBar();
    SetTimer(m_hWnd, kResultTimer, kResultMs, nullptr);
    OfferFolderTags();
}

// After the last of the imports under way: asks which folder names to use as tags on the files they added. Nothing is
// tagged unless the user ticks it (and no rule picks tags by file type). A cancelled import offers nothing.
void Window::OfferFolderTags()
{
    if (m_folderTagOffer.empty())
        return;
    // Busy: the offer waits, and is made again when the sheet, viewer, settings, menu or drag closes.
    if (OpenSheet() || m_viewer.IsOpen() || m_settings.IsOpen() || m_contextMenu.IsOpen() || Dragging())
        return;
    std::vector<FolderTag> offer = std::move(m_folderTagOffer);
    m_folderTagOffer.clear();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_folderTags.Open(m_hWnd, m_renderer, client, std::move(offer));
    m_folderTags.OnHover(CursorInWindow());
}

// Tags the files with the tag of the folder each is in (or the nearest ticked one above) and of every ticked folder
// above that, as one change the undo takes back whole (one Links entry per tag). From Settings'
// "Tag by folder…" it can also take the flat tags of the older import off the same files (m_retagRoot).
void Window::HandleFolderTagsAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    std::vector<FolderTag> tags;
    bool removeOld = false;
    if (action == Sheet::Action::kConfirm)
    {
        tags = m_folderTags.Tags();
        removeOld = m_folderTags.RemoveOld();
    }
    m_folderTags.Close();
    std::string retagRoot = std::move(m_retagRoot);
    std::vector<std::string> retagFiles = std::move(m_retagFiles);
    m_retagRoot.clear();
    m_retagFiles.clear();

    std::unordered_set<file*> tagged;
    UndoStep step;
    bool failed = false;
    bool any = std::any_of(tags.begin(), tags.end(), [](const FolderTag &t) { return t.on; }) || removeOld;
    if (any && BeginChange())
    {
        // The older flat tags first: a file's old folder names, unless the file is getting that very tag now.
        if (removeOld)
        {
            std::unordered_map<std::string, std::unordered_set<std::string>> newTags;  // file path -> folded names it gets
            for (const FolderTag &entry : tags)
            {
                for (const std::string &path : entry.paths)
                    newTags[path].insert(fold_case(entry.name));
            }
            std::unordered_map<tag*, std::vector<std::string>> old;
            for (const std::string &path : retagFiles)
            {
                const file *f = m_lib.find_file(path);
                if (!f)
                    continue;
                std::vector<std::string> names = folder_tags(retagRoot, path);
                for (std::string &name : names)
                    name = fold_case(name);
                auto added = newTags.find(path);
                for (tag *t : f->get_tags())
                {
                    if (std::find(names.begin(), names.end(), t->get_folded_id()) != names.end() &&
                        (added == newTags.end() || !added->second.count(t->get_folded_id())))
                        old[t].push_back(path);
                }
            }
            std::vector<std::pair<tag*, std::vector<std::string>>> removals(old.begin(), old.end());
            std::sort(removals.begin(), removals.end(), [](const auto &a, const auto &b) { return a.first->get_id() < b.first->get_id(); });
            for (auto &removal : removals)
            {
                std::vector<file*> files = FilesAt(removal.second);
                tag_change change;
                if (m_lib.edit_files(files, { removal.first }, false, &change) < 0)
                {
                    failed = true;
                    break;
                }
                if (!change.empty())
                    step.changes.push_back(std::move(change));
                if (removal.first->get_files().empty())
                {
                    tag_change dropped;
                    m_lib.delete_tag(removal.first, &dropped);
                    if (!dropped.empty())
                        step.changes.push_back(std::move(dropped));
                }
                tagged.insert(files.begin(), files.end());
            }
        }
        for (size_t n = 0; n < tags.size() && !failed; n++)
        {
            const FolderTag &entry = tags[n];
            if (!entry.on)
                continue;
            std::vector<file*> files = FilesAt(entry.paths);
            tag_change change;
            if (m_lib.tag_files(files, { entry.name }, &change) < 0)
            {
                failed = true;
                break;
            }
            tagged.insert(files.begin(), files.end());
            if (!change.empty())
                step.changes.push_back(std::move(change));
        }
        bool saved = FinishChange();
        if (!IsWindow(m_hWnd))
            return;
        if (!failed && saved && !tagged.empty())
        {
            step.label = std::wstring(retagRoot.empty() ? strings::kTaggedFiles : strings::kFolderTagsRetag) + L" " + FileCount(tagged.size());
            ShowNotice(step.label);
            if (!step.changes.empty())
                Remember(std::move(step));
        }
    }

    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
    // An import that ended while the sheet was up waits for it.
    if (m_importDonePending)
    {
        m_importDonePending = false;
        PostMessage(m_hWnd, kImportDone, 0, 0);
    }
    if (m_inboxWorkPending)
    {
        m_inboxWorkPending = false;
        PostMessage(m_hWnd, inbox::kInboxWork, 1, 0);
    }
}

// Settings' "Tag by folder…" on a watched folder: the files already in the library under it get the same sheet an
// import does. When some carry the flat tags an older import made (their folder names), it offers to remove them.
void Window::TagByFolder(const std::string &folder)
{
    if (OpenSheet() || m_viewer.IsOpen())
        return;
    std::wstring prefix = Utf8ToWide(folder);
    while (!prefix.empty() && (prefix.back() == L'\\' || prefix.back() == L'/'))
        prefix.pop_back();
    std::vector<scanned_file> inside;
    for (const file *f : m_lib.get_files())
    {
        std::wstring path = Utf8ToWide(f->get_path());
        if (path.size() > prefix.size() && (path[prefix.size()] == L'\\' || path[prefix.size()] == L'/') &&
            _wcsnicmp(path.c_str(), prefix.c_str(), prefix.size()) == 0)
            inside.push_back({ f->get_path(), f->get_identity() });
    }
    if (inside.empty())
    {
        ShowNotice(strings::kFolderTagsNoFiles);
        return;
    }

    std::vector<FolderTag> offer;
    for (const folder_tag_suggestion &suggestion : suggest_folder_tags(folder, inside))
    {
        FolderTag entry{ suggestion.name, {}, true };
        for (size_t index : suggestion.files)
            entry.paths.push_back(inside[index].path);
        offer.push_back(std::move(entry));
    }
    if (offer.empty())
    {
        ShowNotice(strings::kFolderTagsNoFiles);
        return;
    }

    // Old flat tags: a file's tag named like one of the folders it is in.
    bool hasOld = false;
    for (const scanned_file &s : inside)
    {
        const file *f = m_lib.find_file(s.path);
        if (!f)
            continue;
        for (const std::string &name : folder_tags(folder, s.path))
        {
            const tag *t = m_lib.find_tag(name);
            hasOld = hasOld || (t && f->tagged(t));
        }
    }
    m_retagRoot = folder;
    m_retagFiles.clear();
    for (const scanned_file &s : inside)
        m_retagFiles.push_back(s.path);

    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_folderTags.Open(m_hWnd, m_renderer, client, std::move(offer), hasOld);
    m_folderTags.OnHover(CursorInWindow());
}

// A line in the import bar for a few seconds, the way an import reports. Not while one is running.
void Window::ShowNotice(const std::wstring &text, bool error)
{
    if (m_importThread.joinable())
        return;
    bool wasVisible = m_importBar.Visible();
    m_importBar.ShowResult(text, error);
    if (!wasVisible)
    {
        LayoutComponents();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
    else
        PlaceSelectionBar();  // the new text may not fit beside the bar
    SetTimer(m_hWnd, kResultTimer, kResultMs, nullptr);
}

// The thread stops at the next file it reads; FinishImport then adds nothing.
void Window::CancelImport()
{
    m_cancelImport = true;
    m_queuedDrops.clear();
    ShowImportProgress();
}

// Waits for the import thread to stop, without adding what it found: the window is closing.
void Window::StopImport()
{
    m_queuedDrops.clear();
    if (!m_importThread.joinable())
        return;
    m_cancelImport = true;
    m_importThread.join();
    if (m_hWnd)
        KillTimer(m_hWnd, kImportTimer);
}

void Window::HideImportBar()
{
    KillTimer(m_hWnd, kResultTimer);
    m_importBar.Hide();
    LayoutComponents();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}
