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
#include "../../include/saving.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <algorithm>
#include <string>
#include <unordered_set>

// How many of 'files' lack at least one of the tags named: the files that adding them changes.
size_t FilesLacking(const library &lib, const std::vector<file*> &files, const std::vector<std::string> &names)
{
    std::vector<const tag*> tags;
    for (const std::string &name : names)
        tags.push_back(lib.find_tag(name));
    return static_cast<size_t>(std::count_if(files.begin(), files.end(), [&](const file *f)
    {
        return std::any_of(tags.begin(), tags.end(), [f](const tag *t) { return !t || !f->tagged(t); });
    }));
}

void Window::ApplyTagEdit(const TagEdit &edit)
{
    ApplyTagEdit(edit, m_fileGrid.SelectedFiles());
}

// Says what an addition by the sheet or a drop did: "Tagged 12 files · anime", counting the files that lacked a tag,
// or that every file had the tags.
void Window::NoticeTagged(int changed, size_t files, const std::vector<std::string> &tags)
{
    if (changed < 0)
        return;
    std::wstring names;
    for (const std::string &name : tags)
        names += (names.empty() ? L"" : L" ") + Utf8ToWide(name);
    ShowNotice(changed > 0 ? std::wstring(strings::kTaggedFiles) + L" " + FileCount(files) + L" · " + names
                           : std::wstring(strings::kAlreadyTagged));
}

// Adds or removes the tags on every file of 'files' as one change, saved all or none. A tag a removal leaves on no
// file at all is deleted. Returns how many file-tag links changed, or -1 if nothing was.
int Window::ApplyTagEdit(const TagEdit &edit, const std::vector<file*> &files)
{
    if (edit.kind == TagEdit::kNone || edit.tags.empty() || files.empty())
        return -1;

    if (!BeginChange())
        return -1;
    bool add = edit.kind == TagEdit::kAdd;
    std::vector<tag*> tags;
    for (const std::string &name : edit.tags)
    {
        if (tag *t = add ? m_lib.retrieve_tag(name) : m_lib.find_tag(name))
            tags.push_back(t);
    }

    // What changes, for Ctrl+Z: per tag, the files that didn't have it (or did).
    UndoStep step;
    std::unordered_set<const file*> touched;
    std::wstring names;
    for (const tag *t : tags)
    {
        UndoStep::Links links{ t->get_id(), {}, add };
        for (const file *f : files)
        {
            if (f->tagged(t) != add)
            {
                links.paths.push_back(f->get_path());
                touched.insert(f);
            }
        }
        if (!links.paths.empty())
        {
            names += (names.empty() ? L"" : L" ") + Utf8ToWide(t->get_id());
            step.links.push_back(std::move(links));
        }
    }

    int changed = m_lib.edit_files(files, tags, add);
    if (changed >= 0 && !add)
    {
        for (tag *t : tags)
        {
            if (t->get_files().empty())
                m_lib.delete_tag(t);
        }
    }
    bool saved = FinishChange();
    if (saved && changed > 0 && IsWindow(m_hWnd))
    {
        step.label = add ? std::wstring(strings::kTaggedFiles) + L" " + FileCount(touched.size()) + L" \u00B7 " + names
                         : std::wstring(strings::kUntaggedBefore) + L" " + names + L" " + strings::kUntaggedFrom + L" " + FileCount(touched.size());
        Remember(std::move(step));
    }
    return saved ? changed : -1;
}

// Keeps a change for Ctrl+Z. A new change can't be redone over: the steps undone before it are dropped.
void Window::Remember(UndoStep step)
{
    const size_t kUndoLimit = 100;
    m_undo.push_back(std::move(step));
    if (m_undo.size() > kUndoLimit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

// Ctrl+Z takes back the last change, Ctrl+Y makes the last change undone again, and the notice says which.
void Window::Undo(bool redo)
{
    std::vector<UndoStep> &from = redo ? m_redo : m_undo;
    if (from.empty())
    {
        ShowNotice(redo ? strings::kNothingToRedo : strings::kNothingToUndo);
        return;
    }
    UndoStep step = std::move(from.back());
    from.pop_back();
    if (!RunStep(step, redo))
    {
        if (IsWindow(m_hWnd))
            ShowNotice(strings::kUndoFailed, true);
        return;
    }
    if (!IsWindow(m_hWnd))
        return;
    ShowNotice(std::wstring(redo ? strings::kRedone : strings::kUndone) + L" \u00B7 " + step.label);
    (redo ? m_undo : m_redo).push_back(std::move(step));
}

// Makes a change again ('forward') or takes it back, as one change saved all or none. Files and tags gone since are
// skipped. Returns false if the change could not be saved, or a tag rename would now merge with another tag.
bool Window::RunStep(const UndoStep &step, bool forward)
{
    if (!step.tagFrom.empty() && !step.merge)
    {
        const std::string &from = forward ? step.tagFrom : step.tagTo;
        const std::string &to = forward ? step.tagTo : step.tagFrom;
        tag *t = m_lib.find_tag(from);
        tag *other = m_lib.find_tag(to);
        if (!t || (other && other != t) || !BeginChange())
            return false;
        rename_result outcome = m_lib.rename_tag(t, to);
        bool saved = FinishChange();
        if (!IsWindow(m_hWnd) || !saved || (outcome != rename_result::Renamed && outcome != rename_result::Unchanged))
            return false;
        FollowTagInQuery(from, to);
        return true;
    }

    if (!BeginChange())
        return false;
    if (forward)
    {
        for (const UndoStep::Links &links : step.links)
            ApplyLinks(links, links.add);
        for (const std::string &name : step.tagsDeleted)
        {
            if (tag *gone = m_lib.find_tag(name))
                m_lib.delete_tag(gone);
        }
        if (!step.removed.empty())
        {
            std::vector<std::string> paths;
            for (const file_snapshot &f : step.removed)
                paths.push_back(f.path);
            TakeOutOfLibrary(FilesAt(paths));
        }
    }
    else
    {
        for (const std::string &name : step.tagsDeleted)
            m_lib.create_tag(name);  // before the links: an empty tag has none to bring it back
        if (!step.removed.empty())
        {
            m_lib.restore_files(step.removed);
            m_sortDirty = true;
        }
        for (auto links = step.links.rbegin(); links != step.links.rend(); ++links)
            ApplyLinks(*links, !links->add);
    }
    bool saved = FinishChange();
    // A merge moves the search's tag back only if doing it moved it: the target may have been in the search before.
    if (saved && step.merge && step.followed && IsWindow(m_hWnd))
        FollowTagInQuery(forward ? step.tagFrom : step.tagTo, forward ? step.tagTo : step.tagFrom, step.branch);
    return saved;
}

// Adds the tag to the files at the paths, or takes it off them (and deletes it if no file has it any more, as the
// chips do). Part of a change: the caller begins and finishes it.
void Window::ApplyLinks(const UndoStep::Links &links, bool add)
{
    std::vector<file*> files = FilesAt(links.paths);
    tag *t = add ? m_lib.retrieve_tag(links.tag) : m_lib.find_tag(links.tag);
    if (!t || files.empty())
        return;
    if (m_lib.edit_files(files, { t }, add) >= 0 && !add && t->get_files().empty())
        m_lib.delete_tag(t);
}

// Starts a change made of several writes as one transaction, so it is saved all or none: one wait for the disk
// instead of one per write, and no half-saved change if a write fails. If the transaction can't start (the
// database is locked, say), the change must not be made: says so and returns false.
bool Window::BeginChange()
{
    database *db = m_lib.attached();
    if (db && !db->begin())
    {
        ReportSaveError();
        return false;
    }
    return true;
}

// Saves the change started by BeginChange, or undoes it in the database if any write failed (ReportSaveError
// then reloads the library to match), and shows the result. Returns whether it was saved.
bool Window::FinishChange()
{
    database *db = m_lib.attached();
    bool saved = true;
    if (db && (!db->last_error().empty() || !db->commit()))
    {
        db->rollback();
        saved = false;
    }

    ReportSaveError();
    if (!IsWindow(m_hWnd))
        return false;
    LibraryChanged();
    return saved;
}

// Throws the library away and reads it again from the database, then shows it. False if it could not be read.
bool Window::ReloadLibrary()
{
    database *db = m_lib.attached();
    // The grid points into the library; it lets go before the library deletes its files, and so does the ranking.
    m_fileGrid.Clear();
    m_sortRank.clear();
    m_sortDirty = true;
    m_lib.clear();
    if (!db || !open_library(m_lib, *db, ""))
        return false;
    WatchedFoldersChanged({});
    LibraryChanged();
    return true;
}

// Every change is written to the database as it happens. If a write failed, the library in memory no
// longer matches the database: a file that wasn't saved has no row of its own, and the id it was given
// may later belong to another file. So the library is reloaded from the database, the screen shows what
// is really saved, and the user is told the change didn't stick.
void Window::ReportSaveError()
{
    database *db = m_lib.attached();
    if (!db || db->last_error().empty())
        return;

    std::wstring message = strings::kSaveFailed + Utf8ToWide(db->last_error());
    db->clear_error();

    if (!ReloadLibrary())
    {
        // Nothing on screen can be trusted, and new changes would no longer be saved.
        message += strings::kReloadFailed + Utf8ToWide(db->last_error()) + strings::kWillClose;
        MessageBoxW(m_hWnd, message.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
        DestroyWindow(m_hWnd);
        return;
    }

    message += strings::kReloaded;
    MessageBoxW(m_hWnd, message.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
}
