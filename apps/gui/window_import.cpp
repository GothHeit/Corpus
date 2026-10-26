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
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

// Adds the files and folders dragged in from Explorer to the library, with everything inside the folders. Only
// their paths are stored: nothing is copied or moved.
void Window::OnDropFiles(HDROP drop)
{
    std::vector<std::string> paths;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; i++)
    {
        UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(drop, i, &path[0], length + 1);
        paths.push_back(WideToUtf8(path));
    }
    DragFinish(drop);
    AddPaths(std::move(paths));
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
        DWORD attributes = GetFileAttributesW(Utf8ToWide(path).c_str());
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

// Tags the files of every ticked folder name, as one change.
void Window::HandleFolderTagsAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    std::vector<FolderTag> tags;
    if (action == Sheet::Action::kConfirm)
        tags = m_folderTags.Tags();
    m_folderTags.Close();

    std::unordered_set<file*> tagged;
    if (std::any_of(tags.begin(), tags.end(), [](const FolderTag &t) { return t.on; }) && BeginChange())
    {
        bool failed = false;
        UndoStep step;
        for (const FolderTag &entry : tags)
        {
            if (!entry.on)
                continue;
            std::vector<file*> files = FilesAt(entry.paths);
            tag *t = m_lib.find_tag(entry.name);
            if (!t)
                t = m_lib.retrieve_tag(entry.name);
            UndoStep::Links links{ t ? t->get_id() : entry.name, {}, true };
            for (const file *f : files)
            {
                if (t && !f->tagged(t))
                    links.paths.push_back(f->get_path());
            }
            if (!t || m_lib.edit_files(files, { t }, true) < 0)
            {
                failed = true;
                break;
            }
            tagged.insert(files.begin(), files.end());
            if (!links.paths.empty())
                step.links.push_back(std::move(links));
        }
        bool saved = FinishChange();
        if (!IsWindow(m_hWnd))
            return;
        if (!failed && saved && !tagged.empty())
        {
            step.label = std::wstring(strings::kTaggedFiles) + L" " + FileCount(tagged.size());
            ShowNotice(step.label);
            if (!step.links.empty())
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
