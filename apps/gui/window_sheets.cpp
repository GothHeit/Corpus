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
#include "../../include/text.hpp"
#include "filter_query.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <string>

std::vector<file*> Window::TagSheetFiles() const
{
    if (!m_viewer.IsOpen())
        return m_fileGrid.SelectedFiles();
    file *f = m_lib.find_file(m_viewer.CurrentPath());
    return f ? std::vector<file*>{ f } : std::vector<file*>();
}

// Ctrl+T, "Add tag", the info pane's button and the strip's: the tag sheet asks for the tags to add to the selection.
// In the viewer, Ctrl+T and its add button ask for tags for the file shown.
void Window::OpenTagSheet()
{
    std::vector<file*> selected = TagSheetFiles();
    if (selected.empty())
    {
        if (!m_viewer.IsOpen())
            ShowNotice(strings::kTagNoSelection);
        return;
    }
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_tagSheet.Open(m_hWnd, m_renderer, client, selected);
    m_tagSheet.OnHover(CursorInWindow());
    m_viewer.SetCovered(true);
}

// The sheet that is open, if any. Only one is at a time, and it takes the mouse and the keyboard.
Sheet *Window::OpenSheet()
{
    Sheet *sheets[] = { &m_rename, &m_tagSheet, &m_folderTags, &m_confirm };
    for (Sheet *sheet : sheets)
    {
        if (sheet->IsOpen())
            return sheet;
    }
    return nullptr;
}

void Window::CloseSheets()
{
    m_rename.Close();
    m_tagSheet.Close();
    m_folderTags.Close();
    m_confirm.Close();
    m_confirmed = nullptr;
    m_viewer.SetCovered(false);
}

void Window::HandleSheetAction(Sheet *sheet, Sheet::Action action)
{
    if (sheet == &m_rename)
        HandleRenameAction(action);
    else if (sheet == &m_tagSheet)
        HandleTagSheetAction(action);
    else if (sheet == &m_folderTags)
        HandleFolderTagsAction(action);
    else if (sheet == &m_confirm)
        HandleConfirmAction(action);
    if (!IsWindow(m_hWnd))
        return;
    ResumeHeldResults();
    m_viewer.SetCovered(OpenSheet() != nullptr);
    OfferFolderTags();
}

// Asks before a change, in a sheet over the window: 'then' runs if the user confirms. The window keeps running while
// it is up, so 'then' finds what it changes again (by path or name) rather than holding pointers into the library.
void Window::Ask(ConfirmSheet::Question question, std::function<void()> then)
{
    m_contextMenu.Close();
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_confirmed = std::move(then);
    m_confirm.Open(m_hWnd, m_renderer, client, std::move(question));
    m_confirm.OnHover(CursorInWindow());
}

void Window::HandleConfirmAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    std::function<void()> then = std::move(m_confirmed);
    m_confirmed = nullptr;
    m_confirm.Close();
    if (action == Sheet::Action::kConfirm && then)
        then();
    if (!IsWindow(m_hWnd))
        return;
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// F2 and "Rename": asks for a new name for the focused file, or the first selected one. The sheet takes the keyboard
// until it is answered.
void Window::StartRename()
{
    std::vector<file*> selected = m_fileGrid.SelectedFiles();
    if (selected.empty())
        return;
    file *f = m_fileGrid.FocusedFile();
    if (!f || std::find(selected.begin(), selected.end(), f) == selected.end())
        f = selected[0];

    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renamePath = f->get_path();
    m_renameTag.clear();
    std::wstring wide = Utf8ToWide(m_renamePath);
    size_t slash = wide.find_last_of(L"\\/");
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_rename.Open(m_hWnd, m_renderer, client, slash == std::wstring::npos ? wide : wide.substr(slash + 1));
    m_rename.OnHover(CursorInWindow());
}

// The tag sheet's answer: Enter adds the typed tags to the files it was opened for (still the selection: nothing can
// change it while the sheet is up).
void Window::HandleTagSheetAction(Sheet::Action action)
{
    if (action == Sheet::Action::kNone)
        return;
    TagEdit edit;
    if (action == Sheet::Action::kConfirm)
    {
        edit.kind = TagEdit::kAdd;
        edit.tags = m_tagSheet.Tags();
    }
    m_tagSheet.Close();

    std::vector<file*> files = TagSheetFiles();
    size_t lacking = edit.kind == TagEdit::kAdd ? FilesLacking(m_lib, files, edit.tags) : 0;
    int changed = ApplyTagEdit(edit, files);
    if (edit.kind == TagEdit::kAdd)
        NoticeTagged(changed, lacking, edit.tags);
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Sidebar tag menu and F2 over a tag: the rename sheet asks for the tag's new name.
void Window::StartTagRename(const std::string &name)
{
    if (!m_lib.find_tag(name))
        return;
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renameTag = name;
    m_renamePath.clear();
    RECT client;
    GetClientRect(m_hWnd, &client);
    m_rename.Open(m_hWnd, m_renderer, client, Utf8ToWide(name), true, strings::kTagRenameTitle);
    m_rename.OnHover(CursorInWindow());
}

// Renames the tag m_renameTag to what the sheet holds. Another tag with that name (case aside) takes this one's files
// and keeps its own spelling: the sheet closes and a question asks first. If the search names the old tag, it follows
// the tag. On failure 'error' says why, to show in the sheet.
bool Window::RenameTag(std::wstring &error)
{
    tag *t = m_lib.find_tag(m_renameTag);
    if (!t)
        return true;  // gone meanwhile: nothing to rename

    // Like the folder names of an import, a name with spaces becomes one word.
    std::string name = WideToUtf8(m_rename.Name());
    if (!is_valid_tag_name(name))
        name = normalize_tag_name(name);
    if (name.empty())
    {
        error = strings::kTagRenameInvalid;
        return false;
    }

    if (t->get_id() == name)
        return true;
    tag *other = m_lib.find_tag(name);
    if (!other || other == t)
    {
        RenameTagNow(t, name);
        return true;
    }

    std::string from = t->get_id(), into = other->get_id();
    ConfirmSheet::Question question;
    question.title = strings::kMergeBefore + Utf8ToWide(from) + strings::kMergeInto + Utf8ToWide(into) + L"?";
    question.body = Utf8ToWide(into) + strings::kMergeExists + Utf8ToWide(from) + strings::kMergeGetIt;
    question.confirm = strings::kMergeConfirm;
    m_rename.Close();
    Ask(std::move(question), [this, from, into]()
    {
        if (tag *source = m_lib.find_tag(from))
            RenameTagNow(source, into);
    });
    return true;
}

// Renames 't' to 'name', or merges it into the tag that has that name (case aside), as one change Ctrl+Z takes back.
// A merge is kept as the tags it adds and takes off, so undoing it puts each file's tags back as they were.
void Window::RenameTagNow(tag *t, const std::string &name)
{
    std::string oldName = t->get_id();
    UndoStep step;
    step.tagFrom = oldName;
    tag *other = m_lib.find_tag(name);
    if (other && other != t)
    {
        step.merge = true;
        UndoStep::Links gain{ other->get_id(), {}, true }, loss{ oldName, {}, false };
        for (const file *f : t->get_files())
        {
            loss.paths.push_back(f->get_path());
            if (!f->tagged(other))
                gain.paths.push_back(f->get_path());
        }
        step.links = { std::move(gain), std::move(loss) };
    }

    tag *result = nullptr;
    if (!BeginChange())
        return;
    rename_result outcome = m_lib.rename_tag(t, name, &result);
    bool saved = FinishChange();
    if (!IsWindow(m_hWnd))
        return;
    // A failed commit makes FinishChange reload the library: 't' and 'result' are gone, so look the tag up again.
    result = m_lib.find_tag(name);
    if (!saved || !result || (outcome != rename_result::Renamed && outcome != rename_result::Merged))
    {
        if (saved && (outcome == rename_result::Invalid || outcome == rename_result::Failed))
            ShowNotice(outcome == rename_result::Invalid ? strings::kTagRenameInvalid : strings::kTagRenameFailed, true);
        return;
    }

    step.followed = FollowTagInQuery(oldName, result->get_id());
    std::wstring shown = Utf8ToWide(result->get_id());
    step.tagTo = result->get_id();
    if (outcome == rename_result::Merged)
        step.label = std::wstring(strings::kTagMerged) + L" " + shown + L" \u00B7 " + FileCount(result->get_files().size());
    else
        step.label = std::wstring(strings::kTagRenamed) + L" " + shown;
    ShowNotice(step.label);
    Remember(std::move(step));
}

// Sidebar tag menu "Delete tag": a tag on files asks first, saying how many lose it; an empty one just goes.
void Window::DeleteTag(const std::string &name)
{
    tag *t = m_lib.find_tag(name);
    if (!t)
        return;
    size_t count = t->get_files().size();
    if (count == 0)
    {
        DeleteTagNow(name);
        return;
    }
    ConfirmSheet::Question question;
    question.title = strings::kTagDeleteBefore + Utf8ToWide(t->get_id()) + L"?";
    question.body = FileCount(count) + strings::kTagDeleteLose;
    question.confirm = strings::kTagDeleteConfirm;
    std::string id = t->get_id();
    Ask(std::move(question), [this, id]() { DeleteTagNow(id); });
}

// Deletes the tag, taking it off its files, as one change Ctrl+Z takes back. A search naming it drops it.
void Window::DeleteTagNow(const std::string &name)
{
    tag *t = m_lib.find_tag(name);
    if (!t)
        return;
    UndoStep step;
    step.tagDeleted = t->get_id();
    UndoStep::Links loss{ step.tagDeleted, {}, false };
    for (const file *f : t->get_files())
        loss.paths.push_back(f->get_path());
    step.links.push_back(std::move(loss));

    if (!BeginChange())
        return;
    bool done = m_lib.delete_tag(t);
    bool saved = FinishChange();
    if (!IsWindow(m_hWnd))
        return;
    if (!saved || !done)
    {
        if (saved)
            ShowNotice(strings::kTagDeleteFailed, true);
        return;
    }
    std::string query = WideToUtf8(m_searchBar.Query());
    if (TagStates(query).count(fold_case(name)))
        SetQuery(Utf8ToWide(WithTagState(query, name, TagState::kOff)));
    step.label = std::wstring(strings::kTagDeleted) + L" " + Utf8ToWide(step.tagDeleted);
    ShowNotice(step.label);
    Remember(std::move(step));
}

// The search followed the sidebar: a tag it asked for under the old name is asked for under the new one. Returns
// whether the search named it.
bool Window::FollowTagInQuery(const std::string &from, const std::string &to)
{
    std::string query = WideToUtf8(m_searchBar.Query());
    auto states = TagStates(query);
    auto state = states.find(fold_case(from));
    if (state == states.end())
        return false;
    query = WithTagState(query, from, TagState::kOff);
    if (CanFilterBy(to))
        query = WithTagState(query, to, state->second);
    SetQuery(Utf8ToWide(query));
    return true;
}

void Window::HandleRenameAction(Sheet::Action action)
{
    if (action == Sheet::Action::kCancel)
        m_rename.Close();
    else if (action == Sheet::Action::kConfirm)
    {
        std::wstring error;
        bool done = m_renameTag.empty() ? RenameFile(m_renamePath, m_rename.Name(), error) : RenameTag(error);
        if (done)
            m_rename.Close();
        else
            m_rename.SetError(error);
    }
    else
        return;
    OnHover(CursorInWindow());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

// Why Windows can't have 'name' as a file name, or null if it can: characters it forbids, a trailing space or dot
// (which it silently drops, so the file would not get the name typed) and the names of devices.
static const wchar_t *BadFileName(const std::wstring &name)
{
    if (name.empty())
        return strings::kRenameEmpty;
    for (wchar_t ch : name)
    {
        if (ch < L' ' || wcschr(L"\\/:*?\"<>|", ch))
            return strings::kRenameBadChars;
    }
    if (name.back() == L' ' || name.back() == L'.')
        return strings::kRenameBadEnd;

    // CON, NUL, COM1... are devices with or without an extension ("nul.txt").
    std::wstring stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ')
        stem.pop_back();
    static const wchar_t *const kDevices[] = { L"CON", L"PRN", L"AUX", L"NUL" };
    for (const wchar_t *device : kDevices)
    {
        if (_wcsicmp(stem.c_str(), device) == 0)
            return strings::kRenameReserved;
    }
    if (stem.size() == 4 && (_wcsnicmp(stem.c_str(), L"COM", 3) == 0 || _wcsnicmp(stem.c_str(), L"LPT", 3) == 0) &&
        stem[3] >= L'1' && stem[3] <= L'9')
        return strings::kRenameReserved;
    return nullptr;
}

// Renames the file on disk and points the library to the new path, keeping its tags. Nothing is replaced: a name
// taken in the folder is refused. If the library can't be saved, the file gets its old name back.
// On failure 'error' says why, to show in the sheet.
bool Window::RenameFile(const std::string &path, const std::wstring &name, std::wstring &error)
{
    if (const wchar_t *bad = BadFileName(name))
    {
        error = bad;
        return false;
    }

    std::wstring from = Utf8ToWide(path);
    size_t slash = from.find_last_of(L"\\/");
    std::wstring to = (slash == std::wstring::npos ? L"" : from.substr(0, slash + 1)) + name;
    if (to == from)
        return true;

    file *f = m_lib.find_file(path);
    if (!f || GetFileAttributesW(from.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        error = strings::kRenameMissing;
        return false;
    }
    // A change of case only is the same file to Windows, and fine; anything else already there is refused.
    std::wstring oldName = slash == std::wstring::npos ? from : from.substr(slash + 1);
    bool caseOnly = _wcsicmp(oldName.c_str(), name.c_str()) == 0;
    if (!caseOnly && GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        error = strings::kRenameExists;
        return false;
    }
    if (!MoveFileExW(from.c_str(), to.c_str(), 0))
    {
        error = GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS
            ? strings::kRenameExists : strings::kRenameFailed;
        return false;
    }

    std::string newPath = WideToUtf8(to);
    if (!m_lib.relink(f, newPath))
    {
        MoveFileExW(to.c_str(), from.c_str(), 0);
        ReportSaveError();
        error = strings::kRenameFailed;
        return false;
    }
    m_fileGrid.Clear();
    MoveSize(path, newPath);
    m_sortDirty = true;
    LibraryChanged();
    m_fileGrid.SelectPath(newPath);
    SelectionChanged();
    return true;
}
