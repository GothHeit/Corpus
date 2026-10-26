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
#include "shell_files.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <string>
#include <unordered_set>

// Opening more files than this at once asks first, like Explorer does.
static const size_t kOpenPromptThreshold = 15;

std::vector<std::string> PathsOf(const std::vector<file*> &files)
{
    std::vector<std::string> paths;
    paths.reserve(files.size());
    for (const file *f : files)
        paths.push_back(f->get_path());
    return paths;
}

// Ctrl+C and "Copy": the selected files go on the clipboard, to paste in Explorer or a chat.
void Window::CopySelected()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;
    if (shell_files::CopyToClipboard(m_hWnd, PathsOf(files)))
        ShowNotice(std::wstring(strings::kCopied) + L" " + FileCount(files.size()));
    else
        ShowNotice(strings::kCopyFailed, true);
}

// "Show in Explorer": a window on each folder the selected files are in, with them selected. Many folders ask first,
// like opening many files.
void Window::ShowSelectedInExplorer()
{
    std::vector<std::string> paths = PathsOf(m_fileGrid.SelectedFiles());
    size_t folders = shell_files::FolderCount(paths);
    if (folders == 0)
        return;
    if (folders > kOpenPromptThreshold)
    {
        ConfirmSheet::Question question;
        question.title = strings::kOpenBefore + FormatCount(folders) + strings::kShowFoldersAfter;
        question.confirm = strings::kOpenConfirm;
        Ask(std::move(question), [paths]() { shell_files::ShowInExplorer(paths); });
        return;
    }
    shell_files::ShowInExplorer(paths);
}

void Window::SelectionChanged()
{
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    m_tagPanel.SetSelection(selected);
    if (m_infoPane.IsOpen())
        m_infoPane.SetSelection(selected);
}

// Shows or hides the info pane. It reads the selection from the disk when it opens, not while it is closed.
void Window::ToggleInfo()
{
    m_infoPane.SetOpen(!m_infoPane.IsOpen());
    LayoutComponents();
    SelectionChanged();
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Enter and "Open": like a double-click, pictures, videos and readable documents go to the viewer and the rest
// to their default program.
void Window::OpenSelected()
{
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    std::vector<file*> viewable, others;
    for (file *f : selected)
        (Viewer::CanShow(f->get_path()) ? viewable : others).push_back(f);
    if (!others.empty())
        OpenFiles(others);
    if (viewable.size() == 1)
        ViewFile(viewable[0], selected.size() == 1 ? m_fileGrid.Files() : viewable, selected.size() > 1);
    else if (!viewable.empty())
        ViewFile(viewable[0], viewable, true);
}

// A right click on a file: it joins the selection if it isn't in it (alone, as in the Finder), and the menu then
// works on the selection. One on the grid's empty room offers to add a folder.
void Window::OpenContextMenu(POINT pt)
{
    file *f = m_fileGrid.FileAt(pt);
    if (!f)
    {
        if (!m_fileGrid.Contains(pt))
            return;
        m_searchBar.Blur();
        m_sidebar.Blur();
        OnHover(kNoPointer);
        m_contextMenu.Open(m_hWnd, pt, ContextMenu::Kind::kGrid);
        m_contextMenu.OnHover(pt);
        return;
    }
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (std::find(selected.begin(), selected.end(), f) == selected.end())
    {
        m_fileGrid.SelectPath(f->get_path());
        SelectionChanged();
    }
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_contextMenu.Open(m_hWnd, pt);
    m_contextMenu.OnHover(pt);
}

// The files of the library at these paths, skipping the ones it no longer has.
std::vector<file*> Window::FilesAt(const std::vector<std::string> &paths) const
{
    std::vector<file*> files;
    files.reserve(paths.size());
    for (const std::string &path : paths)
    {
        if (file *f = m_lib.find_file(path))
            files.push_back(f);
    }
    return files;
}

void Window::RunMenuItem(ContextMenu::Item item)
{
    RunMenuItemNow(item);
    ResumeHeldResults();
    OfferFolderTags();
}

void Window::RunMenuItemNow(ContextMenu::Item item)
{
    if (item == ContextMenu::Item::kOpen)
        OpenSelected();
    else if (item == ContextMenu::Item::kShowInExplorer)
        ShowSelectedInExplorer();
    else if (item == ContextMenu::Item::kCopy)
        CopySelected();
    else if (item == ContextMenu::Item::kAddFolder)
        ChooseFolders();
    else if (item == ContextMenu::Item::kSortAdded)
        SetSort(SortKey::kAdded, true);
    else if (item == ContextMenu::Item::kSortName)
        SetSort(SortKey::kName, false);
    else if (item == ContextMenu::Item::kSortSize)
        SetSort(SortKey::kSize, true);
    else if (item == ContextMenu::Item::kSortType)
        SetSort(SortKey::kType, false);
    else if (item == ContextMenu::Item::kSortAscending || item == ContextMenu::Item::kSortDescending)
        SetSort(m_sortKey, item == ContextMenu::Item::kSortDescending);
    else if (item == ContextMenu::Item::kAddTag)
        OpenTagSheet();
    else if (item == ContextMenu::Item::kRenameTag)
        StartTagRename(m_menuTag);
    else if (item == ContextMenu::Item::kInfo)
    {
        // Info always shows the pane; Ctrl+I is what hides it.
        if (!m_infoPane.IsOpen())
            ToggleInfo();
    }
    else if (item == ContextMenu::Item::kRename)
        StartRename();
    else if (item == ContextMenu::Item::kRemove)
        RemoveSelectedFiles();
    else if (item == ContextMenu::Item::kDelete)
        DeleteSelectedFiles();
}

// Opens the file with the program Windows associates with it (an .html in the browser, a .pdf in the PDF reader,
// ...). Windows itself reports missing files and asks which program to use when there is none.
void OpenWithDefaultProgram(HWND hWnd, const std::string &path)
{
    std::wstring wide = Utf8ToWide(path);

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = hWnd;     // owner of any dialog Windows shows
    info.lpFile = wide.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

// Opens each file in its default program.
void Window::OpenFiles(const std::vector<file*> &files)
{
    std::vector<std::string> paths = PathsOf(files);
    if (paths.size() > kOpenPromptThreshold)
    {
        ConfirmSheet::Question question;
        question.title = strings::kOpenBefore + FormatCount(paths.size()) + strings::kOpenAfter;
        question.body = strings::kOpenBody;
        question.confirm = strings::kOpenConfirm;
        Ask(std::move(question), [this, paths]()
        {
            for (const std::string &path : paths)
                OpenWithDefaultProgram(m_hWnd, path);
        });
        return;
    }

    for (const std::string &path : paths)
        OpenWithDefaultProgram(m_hWnd, path);
}

// Takes the selected files out of the library, after asking. The files on disk are not touched. Like a tag
// removal, it deletes the tags that only these files had.
void Window::RemoveSelectedFiles()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;

    std::vector<std::string> paths = PathsOf(files);
    ConfirmSheet::Question question;
    question.title = strings::kRemoveBefore + (paths.size() == 1 ? FolderName(paths[0]) : FileCount(paths.size())) + strings::kRemoveAfter;
    question.body = strings::kRemoveBody;
    question.confirm = strings::kRemoveConfirm;
    Ask(std::move(question), [this, paths]()
    {
        std::vector<file*> files = FilesAt(paths);
        if (!files.empty())
            RemoveFromLibrary(files, true);
    });
}

// Takes 'files' out of the library, and the tags only they had, as one change. The files on disk are not touched.
// 'undoable': Ctrl+Z puts them back with their tags (not after they went to the Recycle Bin).
void Window::RemoveFromLibrary(const std::vector<file*> &files, bool undoable)
{
    UndoStep step;
    if (undoable)
        step.removed = m_lib.snapshot_files(files);
    if (!BeginChange())
        return;
    TakeOutOfLibrary(files);
    bool saved = FinishChange();
    if (!undoable || !saved || !IsWindow(m_hWnd) || step.removed.empty())
        return;
    step.label = std::wstring(strings::kRemovedFiles) + L" " + FileCount(step.removed.size()) + L" " + strings::kFromLibrary;
    ShowNotice(step.label);
    Remember(std::move(step));
}

// Takes 'files' out of the library, and the tags only they had. Part of a change: the caller begins and finishes it.
void Window::TakeOutOfLibrary(const std::vector<file*> &files)
{
    std::unordered_set<tag*> tags;
    for (const file *f : files)
    {
        for (tag *t : f->get_tags())
            tags.insert(t);
    }
    // The grid points to the files about to be deleted; it lets go of them first, and so does the ranking.
    m_fileGrid.Clear();
    m_sortRank.clear();
    m_sortDirty = true;
    if (m_lib.remove_files(files) < 0)
        return;
    for (tag *t : tags)
    {
        if (t->get_files().empty())
            m_lib.delete_tag(t);
    }
}

// Sends files to the Recycle Bin, never deleting them for good: Windows asks first when it can't recycle one (a
// network share, say). Returns the paths that are still on disk afterwards, because the user said no or Windows
// failed.
static std::vector<std::wstring> RecycleFiles(HWND hWnd, const std::vector<std::wstring> &paths, std::wstring &error)
{
    // Not in every MinGW header: adds the deletion to the Explorer's undo list (Ctrl+Z there).
    const DWORD addUndoRecord = 0x20000000;

    IFileOperation *operation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr))
    {
        operation->SetOwnerWindow(hWnd);
        operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_WANTNUKEWARNING | addUndoRecord);
        for (const std::wstring &path : paths)
        {
            IShellItem *item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))))
            {
                operation->DeleteItem(item, nullptr);
                item->Release();
            }
        }
        hr = operation->PerformOperations();
        operation->Release();
    }
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        wchar_t* text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        error = text ? text : L"";
        LocalFree(text);
    }

    std::vector<std::wstring> left;
    for (const std::wstring &path : paths)
    {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            left.push_back(path);
    }
    return left;
}

// Shift+Del and "Delete from disk": after asking, moves the selected files to the Recycle Bin and takes the ones
// that went out of the library. A file that is still there (the user declined, or Windows could not) stays.
void Window::DeleteSelectedFiles()
{
    std::vector<file*> files = m_fileGrid.SelectedFiles();
    if (files.empty())
        return;

    std::vector<std::string> selected = PathsOf(files);
    ConfirmSheet::Question question;
    question.title = strings::kDeleteBefore + (selected.size() == 1 ? FolderName(selected[0]) : FileCount(selected.size())) +
        strings::kDeleteAfter;
    question.body = strings::kDeleteBody;
    question.confirm = strings::kDeleteConfirm;
    question.danger = true;
    Ask(std::move(question), [this, selected]()
    {
        std::vector<std::wstring> paths;
        for (const std::string &path : selected)
            paths.push_back(Utf8ToWide(path));
        std::wstring error;
        std::unordered_set<std::wstring> left;
        for (std::wstring &path : RecycleFiles(m_hWnd, paths, error))
            left.insert(std::move(path));

        // Looked up again: the library may have changed while the Recycle Bin was busy.
        std::vector<std::string> gone;
        for (size_t i = 0; i < paths.size(); i++)
        {
            if (!left.count(paths[i]))
                gone.push_back(selected[i]);
        }
        std::vector<file*> goneFiles = FilesAt(gone);
        if (!goneFiles.empty())
            RemoveFromLibrary(goneFiles, false);
        if (!error.empty() && IsWindow(m_hWnd))
        {
            while (!error.empty() && (error.back() == L'\n' || error.back() == L'\r' || error.back() == L' '))
                error.pop_back();
            ShowNotice(std::wstring(strings::kDeleteFailed) + L" " + error, true);
        }
    });
}
