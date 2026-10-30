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
#include <unordered_set>

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
    m_retagRoot.clear();
    m_retagFiles.clear();
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

// Sidebar tag menu and F2 over a tag: the rename sheet asks for the tag's new name. A parent that is no tag of its own
// can be renamed too: it renames the tags under it.
void Window::StartTagRename(const std::string &name)
{
    if (BranchTags(name).empty())
        return;
    m_searchBar.Blur();
    m_sidebar.Blur();
    OnHover(kNoPointer);
    m_renameTag = name;
    m_renamePath.clear();
    RECT client;
    GetClientRect(m_hWnd, &client);
    // Only the last part of the path: a name without '/' keeps the branch the tag is in (see RenameTag).
    size_t slash = name.rfind(tag_path_separator);
    m_rename.Open(m_hWnd, m_renderer, client, Utf8ToWide(slash == std::string::npos ? name : name.substr(slash + 1)), true,
        strings::kTagRenameTitle);
    m_rename.OnHover(CursorInWindow());
}

// The tag with this name and every tag nested under it (Music, Music/Album, Music/Album/Disc_1...), whether or not the
// name is a tag of its own.
std::vector<tag*> Window::BranchTags(const std::string &branch) const
{
    std::string from = fold_case(branch);
    std::vector<tag*> found;
    for (tag *t : m_lib.get_tags())
    {
        if (tag_under(t->get_folded_id(), from))
            found.push_back(t);
    }
    return found;
}

// What a tag under 'branch' is called once the branch is 'name': the part of its name below the branch, found by
// levels (folding can change a name's length), after the new name. The way library::rename_branch does it.
static std::string BranchTarget(const std::string &id, const std::string &branch, const std::string &name)
{
    size_t levels = static_cast<size_t>(std::count(branch.begin(), branch.end(), tag_path_separator)) + 1;
    size_t below = 0;
    for (size_t level = 0; level < levels && below != std::string::npos; level++)
        below = id.find(tag_path_separator, level == 0 ? 0 : below + 1);
    return name + (below == std::string::npos ? std::string() : id.substr(below));
}

// Renames the tag m_renameTag to what the sheet holds. Another tag with that name (case aside) takes this one's files
// and keeps its own spelling: the sheet closes and a question asks first. If the search names the old tag, it follows
// the tag. A tag with others under it, or a parent that is none, renames the whole branch. On failure 'error' says
// why, to show in the sheet.
bool Window::RenameTag(std::wstring &error)
{
    std::vector<tag*> branch = BranchTags(m_renameTag);
    if (branch.empty())
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

    // A name without '/' renames the last part and stays in the branch; one with '/' is the whole new path.
    std::string parent = tag_parent(m_renameTag);
    if (!parent.empty() && name.find(tag_path_separator) == std::string::npos)
        name = parent + tag_path_separator + name;

    std::string from = fold_case(m_renameTag);
    if (branch.size() > 1 || branch[0]->get_folded_id() != from)
    {
        if (name == m_renameTag)
            return true;
        if (fold_case(name) != from && tag_under(fold_case(name), from))
        {
            error = strings::kTagRenameInside;
            return false;
        }
        bool merges = false;
        for (const tag *t : branch)
        {
            const tag *other = m_lib.find_tag(BranchTarget(t->get_id(), m_renameTag, name));
            merges = merges || (other && other != t);
        }
        std::string oldName = m_renameTag;
        if (!merges)
        {
            RenameBranchNow(oldName, name);
            return true;
        }
        ConfirmSheet::Question question;
        question.title = strings::kBranchMergeTitle;
        question.body = strings::kBranchMergeBody;
        question.confirm = strings::kMergeConfirm;
        m_rename.Close();
        Ask(std::move(question), [this, oldName, name]() { RenameBranchNow(oldName, name); });
        return true;
    }

    tag *t = branch[0];
    if (t->get_id() == name)
        return true;
    tag *other = m_lib.find_tag(name);
    if (!other || other == t)
    {
        RenameTagNow(t, name);
        return true;
    }

    std::string oldName = t->get_id(), into = other->get_id();
    ConfirmSheet::Question question;
    question.title = strings::kMergeBefore + Utf8ToWide(oldName) + strings::kMergeInto + Utf8ToWide(into) + L"?";
    question.body = Utf8ToWide(into) + strings::kMergeExists + Utf8ToWide(oldName) + strings::kMergeGetIt;
    question.confirm = strings::kMergeConfirm;
    m_rename.Close();
    Ask(std::move(question), [this, oldName, into]()
    {
        if (tag *source = m_lib.find_tag(oldName))
            RenameTagNow(source, into);
    });
    return true;
}

// Renames a branch (the tag and every tag under it) as one change Ctrl+Z takes back. The undo step keeps, for every tag
// under it, the files that moved to the new name and the files that left the old one, so a merge splits again; it is
// written before the rename and kept only if the rename is saved. The tags a rename leaves empty don't come back.
void Window::RenameBranchNow(const std::string &branch, const std::string &name)
{
    struct Moved
    {
        std::string from, to;
        std::vector<std::string> left;    // the files the old name had
        std::vector<std::string> gained;  // those the new name did not have yet
    };
    std::vector<Moved> moved;
    std::vector<tag*> tags = BranchTags(branch);
    for (const tag *t : tags)
    {
        Moved m{ t->get_id(), BranchTarget(t->get_id(), branch, name), {}, {} };
        const tag *other = m_lib.find_tag(m.to);
        if (other == t)
            continue;  // only the case of its name changes
        for (const file *f : t->get_files())
        {
            m.left.push_back(f->get_path());
            if (!other || !f->tagged(other))
                m.gained.push_back(f->get_path());
        }
        moved.push_back(std::move(m));
    }

    if (!BeginChange())
        return;
    int renamed = m_lib.rename_branch(branch, name);
    bool saved = FinishChange();
    if (!IsWindow(m_hWnd))
        return;
    if (!saved || renamed < 0)
    {
        // A failed commit already told the user and reloaded; -1 with a good save is a name the core refused.
        if (saved)
            ShowNotice(strings::kTagRenameFailed, true);
        return;
    }
    if (renamed == 0)
        return;

    UndoStep step;
    for (Moved &m : moved)
    {
        if (!m.gained.empty())
            step.links.push_back({ m.to, std::move(m.gained), true });
    }
    for (Moved &m : moved)
    {
        if (!m.left.empty())
            step.links.push_back({ m.from, std::move(m.left), false });
    }
    step.tagFrom = branch;
    step.tagTo = name;
    step.merge = true;
    step.branch = true;
    step.followed = FollowTagInQuery(branch, name, true);
    step.label = std::wstring(strings::kTagRenamed) + L" " + Utf8ToWide(name) + L" \u00B7 " + FormatCount(tags.size()) + L" " + strings::kTagsCount;
    ShowNotice(step.label);
    Remember(std::move(step));
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

// Sidebar tag menu "Delete tag": a tag on files asks first, saying how many lose it; an empty one just goes. A tag with
// others under it, or a parent that is none, deletes the whole branch.
void Window::DeleteTag(const std::string &name)
{
    std::vector<tag*> branch = BranchTags(name);
    if (branch.empty())
        return;
    std::unordered_set<const file*> files;
    for (const tag *t : branch)
        files.insert(t->get_files().begin(), t->get_files().end());
    if (files.empty())
    {
        DeleteTagNow(name);
        return;
    }
    bool many = branch.size() > 1;
    ConfirmSheet::Question question;
    question.title = strings::kTagDeleteBefore + Utf8ToWide(many ? name : branch[0]->get_id()) + (many ? strings::kTagDeleteBranch : L"?");
    question.body = FileCount(files.size()) + (many ? strings::kTagDeleteLoseMany : strings::kTagDeleteLose);
    question.confirm = many ? strings::kTagDeleteConfirmMany : strings::kTagDeleteConfirm;
    std::string id = many ? name : branch[0]->get_id();
    Ask(std::move(question), [this, id]() { DeleteTagNow(id); });
}

// Deletes the tag and the tags under it, taking them off their files, as one change Ctrl+Z takes back. A search naming
// one drops it.
void Window::DeleteTagNow(const std::string &name)
{
    std::vector<tag*> tags = BranchTags(name);
    if (tags.empty())
        return;
    UndoStep step;
    for (const tag *t : tags)
    {
        step.tagsDeleted.push_back(t->get_id());
        UndoStep::Links loss{ t->get_id(), {}, false };
        for (const file *f : t->get_files())
            loss.paths.push_back(f->get_path());
        if (!loss.paths.empty())
            step.links.push_back(std::move(loss));
    }

    if (!BeginChange())
        return;
    bool done = true;
    for (tag *t : tags)
        done = m_lib.delete_tag(t) && done;
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
    auto states = TagStates(query);
    bool named = false;
    for (const std::string &id : step.tagsDeleted)
    {
        if (states.count(fold_case(id)))
        {
            query = WithTagState(query, id, TagState::kOff);
            named = true;
        }
    }
    if (named)
        SetQuery(Utf8ToWide(query));
    step.label = std::wstring(strings::kTagDeleted) + L" " + Utf8ToWide(name);
    if (tags.size() > 1)
        step.label += L" \u00B7 " + FormatCount(tags.size()) + L" " + strings::kTagsCount;
    ShowNotice(step.label);
    Remember(std::move(step));
}

// The search followed the sidebar: a tag it asked for under the old name is asked for under the new one. With 'branch',
// so are the tags under it. Returns whether the search named any.
bool Window::FollowTagInQuery(const std::string &from, const std::string &to, bool branch)
{
    std::string query = WideToUtf8(m_searchBar.Query());
    std::string folded = fold_case(from);
    bool followed = false;
    for (const auto &entry : TagStates(query))
    {
        const std::string &key = entry.first;
        if (key != folded && !(branch && tag_under(key, folded)))
            continue;
        followed = true;
        query = WithTagState(query, key, TagState::kOff);
        std::string target = key == folded ? to : to + key.substr(folded.size());
        if (CanFilterBy(target))
            query = WithTagState(query, target, entry.second);
    }
    if (followed)
        SetQuery(Utf8ToWide(query));
    return followed;
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
