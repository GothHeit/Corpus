#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <set>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <unordered_map>

#include "../include/database.hpp"
#include "../include/file.hpp"
#include "../include/file_identity.hpp"
#include "../include/folder_import.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"
#include "../include/text.hpp"

/// Splits search text into folded words, dropping repeats.
static void AddWords(const std::string &text, std::vector<std::string> &words)
{
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t start = text.find_first_not_of(' ', pos);
        if (start == std::string::npos)
            break;
        size_t end = text.find(' ', start);
        if (end == std::string::npos)
            end = text.size();
        std::string word = fold_search(text.substr(start, end - start));
        if (!word.empty() && std::find(words.begin(), words.end(), word) == words.end())
            words.push_back(std::move(word));
        pos = end;
    }
}

/// Upper-cases the drive letter on Windows, so "c:\a.jpg" and "C:\a.jpg" are one file.
static std::string stored_path(const std::string &path)
{
#ifdef _WIN32
    if(path.size() >= 2 && path[1] == ':' && path[0] >= 'a' && path[0] <= 'z')
    {
        std::string out = path;
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
        return out;
    }
#endif
    return path;
}

/// Attaches a database; later changes are written through. Loads nothing. nullptr keeps the library in memory.
void library::attach(database* d)
{
    this->db = d;
}

database* library::attached() const
{
    return db;
}

/// Adds a file, recording its file system identity (for find_moved) when a database is attached. The row is
/// written first. Returns the existing file for a known path, nullptr if the database failed.
file* library::add_file(const std::string &given)
{
    std::string path = stored_path(given);
    file* &slot = files_by_path[path];
    if(slot)
    {
        return slot;
    }
    file* f = new file(path);
    bool was_ignored = ignored.count(path) > 0;
    if(db)
    {
        f->set_identity(file_identity(path));
        f->set_db_id(db->insert_file(path, f->get_identity()));
        // Added by hand: a watched folder may have it again.
        if(!f->get_db_id() || (was_ignored && !db->unignore_file(path)))
        {
            files_by_path.erase(path);
            delete f;
            return nullptr;
        }
    }
    if(was_ignored)
    {
        ignored.erase(path);
    }
    slot = f;
    lib_files.push_back(f);
    return f;
}

/// Adds scanned files not yet in the library, keeping the scan's identities. Runs its own transaction, so don't
/// call it inside one. Returns the count, or -1 on database failure (rolled back).
int library::add_files(const std::vector<scanned_file> &files)
{
    std::vector<std::pair<std::string, const scanned_file*>> fresh;
    std::unordered_set<std::string> batch;
    for(const scanned_file &s : files)
    {
        std::string path = stored_path(s.path);
        if(!files_by_path.count(path) && batch.insert(path).second)
        {
            fresh.emplace_back(std::move(path), &s);
        }
    }
    if(fresh.empty())
    {
        return 0;
    }

    // Rows first: memory only changes once they are saved.
    std::vector<std::int64_t> ids(fresh.size(), 0);
    if(db)
    {
        if(!db->begin())
        {
            return -1;
        }
        for(std::size_t i = 0; i < fresh.size(); i++)
        {
            ids[i] = db->insert_file(fresh[i].first, fresh[i].second->identity);
            if(!ids[i] || (ignored.count(fresh[i].first) && !db->unignore_file(fresh[i].first)))
            {
                db->rollback();
                return -1;
            }
        }
        if(!db->commit())
        {
            db->rollback();
            return -1;
        }
    }

    lib_files.reserve(lib_files.size() + fresh.size());
    for(std::size_t i = 0; i < fresh.size(); i++)
    {
        file* f = new file(fresh[i].first);
        f->set_identity(fresh[i].second->identity);
        f->set_db_id(ids[i]);
        files_by_path[f->get_path()] = f;
        lib_files.push_back(f);
        ignored.erase(f->get_path());
    }
    return static_cast<int>(fresh.size());
}

/// Looks a file up by path; nullptr if absent.
file* library::find_file(const std::string &path) const
{
    auto it = files_by_path.find(stored_path(path));
    return it != files_by_path.end() ? it->second : nullptr;
}

/// What the running operation has changed so far, as a net effect (see tag_change).
struct library::change_log
{
    std::set<tag_change::link> added, removed;
    std::set<std::string> created, deleted;
};

/// Starts a record for an operation if the caller wants one and no outer operation is already keeping it. An inner
/// operation (rename_branch inside delete_tag_keep_children) writes into the outer record. done() hands the record
/// over on success; without it the caller's tag_change stays empty.
class library::recording
{
    library &lib;
    tag_change *out;
    bool owner;

    public:

    recording(library &l, tag_change *change) : lib(l), out(change), owner(change && !l.log)
    {
        if(owner)
        {
            *out = tag_change();
            lib.log = new change_log;
        }
    }

    template<class T> T done(T result)
    {
        if(owner && lib.log)
        {
            out->added.assign(lib.log->added.begin(), lib.log->added.end());
            out->removed.assign(lib.log->removed.begin(), lib.log->removed.end());
            out->created.assign(lib.log->created.begin(), lib.log->created.end());
            out->deleted.assign(lib.log->deleted.begin(), lib.log->deleted.end());
        }
        return result;
    }

    ~recording()
    {
        if(owner)
        {
            delete lib.log;
            lib.log = nullptr;
        }
    }

    recording(const recording&) = delete;
    recording& operator=(const recording&) = delete;
};

void library::note_link(const file* f, const tag* t, bool add)
{
    if(!log)
    {
        return;
    }
    tag_change::link key(t->get_id(), f->get_path());
    // A link made and then taken off (or the other way round) within one operation is no change.
    if(add ? log->removed.erase(key) == 0 : log->added.erase(key) == 0)
    {
        (add ? log->added : log->removed).insert(std::move(key));
    }
}

void library::note_tag(const tag* t, bool created)
{
    if(!log)
    {
        return;
    }
    if(created ? log->deleted.erase(t->get_id()) == 0 : log->created.erase(t->get_id()) == 0)
    {
        (created ? log->created : log->deleted).insert(t->get_id());
    }
}

/// Adds or removes one link, database first, and records it. False if the database refused (nothing changed).
bool library::link_now(file* f, tag* t, bool add)
{
    if(add == f->tagged(t))
    {
        return true;
    }
    if(db && !(add ? db->link(f->get_db_id(), t->get_db_id()) : db->unlink(f->get_db_id(), t->get_db_id())))
    {
        return false;
    }
    if(add)
    {
        f->add_tag(t);
    }
    else
    {
        f->remove_tag(t);
    }
    note_link(f, t, add);
    return true;
}

/// Adds a tag to a file (with every branch above it, see edit_files) or removes it. True if the file ends up as asked.
bool library::edit_file(file* f, tag* t, const bool add)
{
    if(!f || !t)
    {
        return 0;
    }
    if(add)
    {
        return edit_files({f}, {t}, true) >= 0 && f->tagged(t);
    }
    return link_now(f, t, false);
}

/// Bulk add_tag/remove_tag as one atomic database group. Skips pairs already as asked and repeats. Adding also adds
/// every branch above each tag ("Music/Rock" puts "Music" on the file too, creating it if need be), so a file in a
/// level is always tagged with the levels above it; removing takes off exactly the tags given. Returns the links
/// changed (branches included), or -1 if the database failed (nothing changed, apart from branch tags made on the way).
/// change, if given, gets what was done (see tag_change).
int library::edit_files(const std::vector<file*> &files, const std::vector<tag*> &tags, const bool add, tag_change *change)
{
    recording rec(*this, change);
    std::unordered_set<const file*> seen_f;
    std::unordered_set<const tag*> seen_t;
    std::vector<tag*> ts;
    std::vector<tag*> fresh;  // branch tags made here, deleted again if the edit fails
    auto want = [&](tag* t)
    {
        if(t && seen_t.insert(t).second)
        {
            ts.push_back(t);
        }
    };
    bool any_file = false;
    for(file* f : files)
    {
        any_file = any_file || f;
    }
    for(tag* t : tags)
    {
        if(!t)
        {
            continue;
        }
        want(t);
        if(add && any_file)
        {
            for(const std::string &level : tag_with_ancestors(t->get_id()))
            {
                const bool known = find_tag(level) != nullptr;
                tag* branch = create_tag(level);
                if(!branch)
                {
                    for(tag* made : fresh)
                    {
                        delete_tag(made);
                    }
                    return -1;
                }
                if(!known)
                {
                    fresh.push_back(branch);
                }
                want(branch);
            }
        }
    }
    std::vector<std::pair<file*, tag*>> changes;
    for(file* f : files)
    {
        if(!f || !seen_f.insert(f).second)
        {
            continue;
        }
        for(tag* t : ts)
        {
            if(f->tagged(t) != add)
            {
                changes.emplace_back(f, t);
            }
        }
    }
    auto tidy = [&]()
    {
        // Branch tags made for files that did not need them (every link was there already) are not kept.
        for(tag* made : fresh)
        {
            if(made->get_files().empty())
            {
                delete_tag(made);
            }
        }
    };
    if(changes.empty())
    {
        tidy();
        return rec.done(0);
    }
    if(db)
    {
        if(!db->savepoint())
        {
            return -1;
        }
        for(const auto &c : changes)
        {
            bool ok = add ? db->link(c.first->get_db_id(), c.second->get_db_id())
                          : db->unlink(c.first->get_db_id(), c.second->get_db_id());
            if(!ok)
            {
                db->undo_savepoint();
                for(tag* made : fresh)
                {
                    delete_tag(made);
                }
                return -1;
            }
        }
        if(!db->release_savepoint())
        {
            for(tag* made : fresh)
            {
                delete_tag(made);
            }
            return -1;
        }
    }
    for(const auto &c : changes)
    {
        if(add)
        {
            c.first->add_tag(c.second);
        }
        else
        {
            c.first->remove_tag(c.second);
        }
        note_link(c.first, c.second, add);
    }
    return rec.done(static_cast<int>(changes.size()));
}

/// Tags the files with the named tags and every branch above them, making the tags that don't exist. Names must be
/// valid (see is_valid_tag_name). Same result and failure as edit_files; with no file, makes nothing.
int library::tag_files(const std::vector<file*> &files, const std::vector<std::string> &names, tag_change *change)
{
    recording rec(*this, change);
    if(std::none_of(files.begin(), files.end(), [](const file* f) { return f != nullptr; }))
    {
        return rec.done(0);
    }
    for(const std::string &name : names)
    {
        if(!is_valid_tag_name(name))
        {
            return -1;
        }
    }
    std::vector<tag*> ts;
    std::vector<tag*> fresh;
    for(const std::string &name : names)
    {
        const bool known = find_tag(name) != nullptr;
        tag* t = create_tag(name);
        if(!t)
        {
            for(tag* made : fresh)
            {
                delete_tag(made);
            }
            return -1;
        }
        if(!known)
        {
            fresh.push_back(t);
        }
        ts.push_back(t);
    }
    int n = edit_files(files, ts, true);
    for(tag* made : fresh)
    {
        // A name that gave nothing (every file had it) or a failed edit does not leave an empty tag behind.
        if(made->get_files().empty())
        {
            delete_tag(made);
        }
    }
    return n < 0 ? n : rec.done(n);
}

/// Takes a level out of the files' tags: every tag of a file with a level called level (case aside; whole levels, not
/// parts of a name) is taken off the file, and the file gets the same path without that level, with the branches above
/// it, joining the tag if it exists and making it if not ("Queen/Live" minus Queen is "Live"; "A/B/C" minus B is "A/C").
/// Nothing else is renamed: other files keep their tags. Tags left with no file are deleted. A path with nothing
/// left only goes. joined, if given, gets the existing tags (their spelling) a file joined, once each. Returns how
/// many files changed, or -1 if level isn't one valid level name or the database refused (then reload, as after
/// rename_branch). change, if given, gets what was done (see tag_change).
int library::remove_level(const std::vector<file*> &files, const std::string &level, std::vector<std::string> *joined,
    tag_change *change)
{
    recording rec(*this, change);
    if(joined)
    {
        joined->clear();
    }
    if(!is_valid_tag_name(level) || level.find(tag_path_separator) != std::string::npos)
    {
        return -1;
    }
    const std::string folded = fold_case(level);
    struct plan
    {
        file* f;
        std::vector<tag*> drop;
        std::vector<std::string> gain;
    };
    std::vector<plan> plans;
    std::unordered_set<const file*> seen;
    for(file* f : files)
    {
        if(!owns(f) || !seen.insert(f).second)
        {
            continue;
        }
        plan p{f, {}, {}};
        std::set<std::string> gained;
        for(tag* t : f->get_tags())
        {
            const std::string &id = t->get_id();
            std::string rest;
            bool hit = false;
            for(std::size_t from = 0; from <= id.size();)
            {
                std::size_t end = id.find(tag_path_separator, from);
                std::string part = id.substr(from, end == std::string::npos ? end : end - from);
                if(fold_case(part) == folded)
                {
                    hit = true;
                }
                else
                {
                    rest += (rest.empty() ? "" : std::string(1, tag_path_separator)) + part;
                }
                if(end == std::string::npos)
                {
                    break;
                }
                from = end + 1;
            }
            if(!hit)
            {
                continue;
            }
            p.drop.push_back(t);
            for(std::string &name : tag_with_ancestors(rest))
            {
                if(gained.insert(fold_case(name)).second)
                {
                    p.gain.push_back(std::move(name));
                }
            }
        }
        if(!p.drop.empty())
        {
            plans.push_back(std::move(p));
        }
    }
    if(plans.empty())
    {
        return rec.done(0);
    }
    if(db && !db->savepoint())
    {
        return -1;
    }
    std::set<std::string> made, told;
    std::vector<tag*> touched;
    bool ok = true;
    for(const plan &p : plans)
    {
        for(const std::string &name : p.gain)
        {
            tag* t = find_tag(name);
            const bool existed = t != nullptr;
            if(!t && !(t = create_tag(name)))
            {
                ok = false;
                break;
            }
            if(!existed)
            {
                made.insert(t->get_folded_id());
            }
            const bool had = p.f->tagged(t);
            if(!link_now(p.f, t, true))
            {
                ok = false;
                break;
            }
            if(joined && existed && !had && !made.count(t->get_folded_id()) && told.insert(t->get_folded_id()).second)
            {
                joined->push_back(t->get_id());
            }
        }
        for(tag* t : p.drop)
        {
            if(!ok)
            {
                break;
            }
            ok = link_now(p.f, t, false);
            touched.push_back(t);
        }
        if(!ok)
        {
            break;
        }
    }
    std::set<tag*> emptied;
    for(tag* t : touched)
    {
        if(ok && emptied.insert(t).second && t->get_files().empty())
        {
            ok = delete_tag(t);
        }
    }
    if(!ok)
    {
        if(db)
        {
            db->undo_savepoint();
        }
        return -1;
    }
    if(db && !db->release_savepoint())
    {
        return -1;
    }
    return rec.done(static_cast<int>(plans.size()));
}

/// Takes a tag_change back (undo) or makes it again (redo), by names and paths: files and tags gone since are
/// skipped. Redo takes the links off, deletes the tags, makes the tags, then adds the links; undo does the same
/// with the two lists swapped. One atomic group. Returns the links changed, or -1 if the database refused (reload).
int library::apply_tag_change(const tag_change &c, bool undo)
{
    const std::vector<tag_change::link> &off = undo ? c.added : c.removed, &on = undo ? c.removed : c.added;
    const std::vector<std::string> &drop = undo ? c.created : c.deleted, &make = undo ? c.deleted : c.created;
    if(db && !db->savepoint())
    {
        return -1;
    }
    int changed = 0;
    bool ok = true;
    for(const auto &l : off)
    {
        file* f = find_file(l.second);
        tag* t = find_tag(l.first);
        if(f && t && f->tagged(t))
        {
            ok = link_now(f, t, false);
            changed++;
        }
        if(!ok)
        {
            break;
        }
    }
    for(const std::string &name : drop)
    {
        if(ok)
        {
            if(tag* t = find_tag(name))
            {
                ok = delete_tag(t);
            }
        }
    }
    for(const std::string &name : make)
    {
        ok = ok && create_tag(name);
    }
    for(const auto &l : on)
    {
        if(!ok)
        {
            break;
        }
        file* f = find_file(l.second);
        if(!f)
        {
            continue;
        }
        tag* t = create_tag(l.first);
        if(!t)
        {
            ok = false;
            break;
        }
        if(!f->tagged(t))
        {
            ok = link_now(f, t, true);
            changed++;
        }
    }
    if(!ok)
    {
        if(db)
        {
            db->undo_savepoint();
        }
        return -1;
    }
    if(db && !db->release_savepoint())
    {
        return -1;
    }
    return changed;
}

/// True if the file belongs to this library.
bool library::owns(const file* f) const
{
    auto it = f ? files_by_path.find(f->get_path()) : files_by_path.end();
    return it != files_by_path.end() && it->second == f;
}

/// Removes a file from the library (not from disk); the pointer is dead afterwards. See remove_files for many.
bool library::remove_file(file* f)
{
    return remove_files({f}) == 1;
}

/// Linear-time bulk removal; pointers are dead afterwards. Rows go first as one atomic group. Files in a watched
/// folder are remembered (ignore_file) so the folder doesn't re-add them. Skips repeats, nullptr and foreign
/// files. Returns the count, or -1 on database failure.
int library::remove_files(const std::vector<file*> &files)
{
    std::unordered_set<const file*> gone;
    std::vector<file*> list;
    for(file* f : files)
    {
        if(owns(f) && gone.insert(f).second)
        {
            list.push_back(f);
        }
    }
    if(list.empty())
    {
        return 0;
    }
    std::vector<const file*> watched_list;
    for(const file* f : list)
    {
        if(is_watched(f->get_path()))
        {
            watched_list.push_back(f);
        }
    }
    if(db)
    {
        if(!db->savepoint())
        {
            return -1;
        }
        for(const file* f : list)
        {
            if(!db->delete_file(f->get_db_id()))
            {
                db->undo_savepoint();
                return -1;
            }
        }
        for(const file* f : watched_list)
        {
            if(!db->ignore_file(f->get_path(), f->get_identity()))
            {
                db->undo_savepoint();
                return -1;
            }
        }
        if(!db->release_savepoint())
        {
            return -1;
        }
    }

    for(const file* f : watched_list)
    {
        ignored[f->get_path()] = f->get_identity();
    }
    // Each list is compacted once, rather than searched once per file.
    std::unordered_set<tag*> touched;
    for(file* f : list)
    {
        files_by_path.erase(f->get_path());
        touched.insert(f->tags.begin(), f->tags.end());
    }
    for(tag* t : touched)
    {
        t->has_tag.erase(std::remove_if(t->has_tag.begin(), t->has_tag.end(),
            [&gone](const file* f) { return gone.count(f) > 0; }), t->has_tag.end());
    }
    lib_files.erase(std::remove_if(lib_files.begin(), lib_files.end(),
        [&gone](const file* f) { return gone.count(f) > 0; }), lib_files.end());
    for(file* f : list)
    {
        f->tags.clear();
        delete f;
    }
    return static_cast<int>(list.size());
}

/// Captures what the library knows of files so restore_files can bring them back after removal.
std::vector<file_snapshot> library::snapshot_files(const std::vector<file*> &files) const
{
    std::vector<file_snapshot> out;
    for(const file* f : files)
    {
        if(!owns(f))
        {
            continue;
        }
        file_snapshot s;
        s.path = f->get_path();
        s.identity = f->get_identity();
        s.db_id = f->get_db_id();
        for(const tag* t : f->get_tags())
        {
            s.tags.push_back(t->get_id());
        }
        if(const reading_progress* p = f->get_progress())
        {
            s.has_progress = true;
            s.progress = *p;
        }
        out.push_back(std::move(s));
    }
    return out;
}

/// Puts removed files back (see snapshot_files) with identity, tags (recreated if gone), progress and, if still
/// free, their old row id. Skips paths already present. One atomic group. Returns the count, or -1 on database
/// failure.
int library::restore_files(const std::vector<file_snapshot> &files)
{
    std::vector<const file_snapshot*> fresh;
    std::vector<std::string> fresh_paths;
    std::unordered_set<std::string> batch;
    for(const file_snapshot &s : files)
    {
        std::string path = stored_path(s.path);
        if(!files_by_path.count(path) && batch.insert(path).second)
        {
            fresh.push_back(&s);
            fresh_paths.push_back(std::move(path));
        }
    }
    if(fresh.empty())
    {
        return 0;
    }

    std::vector<std::string> new_tags;
    std::unordered_map<std::string, std::size_t> new_index;
    for(const file_snapshot* s : fresh)
    {
        for(const std::string &name : s->tags)
        {
            std::string folded = fold_case(name);
            if(!tags_by_name.count(folded) && new_index.emplace(folded, new_tags.size()).second)
            {
                new_tags.push_back(name);
            }
        }
    }

    // Rows first: memory only changes once they are saved.
    std::vector<std::int64_t> file_ids(fresh.size(), 0), tag_ids(new_tags.size(), 0);
    if(db)
    {
        if(!db->savepoint())
        {
            return -1;
        }
        bool ok = true;
        for(std::size_t i = 0; ok && i < fresh.size(); i++)
        {
            file_ids[i] = db->restore_file(fresh[i]->db_id, fresh_paths[i], fresh[i]->identity);
            ok = file_ids[i] != 0 && (!ignored.count(fresh_paths[i]) || db->unignore_file(fresh_paths[i]));
        }
        for(std::size_t i = 0; ok && i < new_tags.size(); i++)
        {
            tag_ids[i] = db->insert_tag(new_tags[i]);
            ok = tag_ids[i] != 0;
        }
        for(std::size_t i = 0; ok && i < fresh.size(); i++)
        {
            for(const std::string &name : fresh[i]->tags)
            {
                std::string folded = fold_case(name);
                auto known = tags_by_name.find(folded);
                std::int64_t tag_id = known != tags_by_name.end() ? known->second->get_db_id() : tag_ids[new_index[folded]];
                if(!(ok = db->link(file_ids[i], tag_id)))
                {
                    break;
                }
            }
            if(ok && fresh[i]->has_progress)
            {
                ok = db->set_progress(file_ids[i], fresh[i]->progress);
            }
        }
        if(!ok)
        {
            db->undo_savepoint();
            return -1;
        }
        if(!db->release_savepoint())
        {
            return -1;
        }
    }

    for(std::size_t i = 0; i < new_tags.size(); i++)
    {
        tag* t = new tag(new_tags[i]);
        t->set_db_id(tag_ids[i]);
        tags_by_name[t->get_folded_id()] = t;
        tags.push_back(t);
    }
    lib_files.reserve(lib_files.size() + fresh.size());
    for(std::size_t i = 0; i < fresh.size(); i++)
    {
        file* f = new file(fresh_paths[i]);
        f->set_identity(fresh[i]->identity);
        f->set_db_id(file_ids[i]);
        files_by_path[f->get_path()] = f;
        lib_files.push_back(f);
        ignored.erase(f->get_path());
        for(const std::string &name : fresh[i]->tags)
        {
            tag* t = tags_by_name[fold_case(name)];
            if(!f->tagged(t))
            {
                f->add_tag(t);
            }
        }
        if(fresh[i]->has_progress)
        {
            f->set_progress(fresh[i]->progress);
        }
    }
    return static_cast<int>(fresh.size());
}

/// Lists files no longer at their path. Hits the disk once per file.
std::vector<file*> library::missing_files() const
{
    std::vector<file*> out;
    for(file* f : lib_files)
    {
        if(file_missing(f->get_path()))
        {
            out.push_back(f);
        }
    }
    return out;
}

static std::string volume_of(const std::string &identity)
{
    return identity.substr(0, identity.find(':'));
}

/// Records the identity of files at their path that have none or a stale one (added without a database, saved
/// by temp-file-and-rename, or stored before identities existed). Identities recorded on FAT/exFAT can never
/// match, so they're forgotten once the volume is back. Hits the disk once per file; wrap in a transaction.
/// Returns how many were recorded or forgotten.
int library::record_identities()
{
    int count = 0;
    for(file* f : lib_files)
    {
        const std::string &path = f->get_path();
        std::string id = file_identity(path);
        if(id == f->get_identity())
        {
            continue;
        }
        if(id.empty())
        {
            // A missing file was on FAT if its folder is, on that same volume: a FAT stick that took an unplugged
            // NTFS drive's letter says nothing about that drive's files.
            std::string fat = fat_volume(path);
            if(fat.empty())
            {
                fat = fat_volume(std::filesystem::u8path(path).parent_path().u8string());
            }
            if(fat.empty() || fat != volume_of(f->get_identity()))
            {
                continue;
            }
        }
        if(db && !db->set_identity(f->get_db_id(), id))
        {
            continue;
        }
        f->set_identity(id);
        count++;
    }
    return count;
}

/// Name equality; Windows ignores case.
static bool same_name(const std::filesystem::path &a, const std::filesystem::path &b)
{
#ifdef _WIN32
    return fold_case(a.u8string()) == fold_case(b.u8string());
#else
    return a == b;
#endif
}

/// True if path is the folder or under it, comparing names only.
static bool lexically_under(const std::filesystem::path &path, const std::filesystem::path &folder)
{
    auto p = path.begin();
    for(auto it = folder.begin(); it != folder.end(); ++it, ++p)
    {
        // A trailing separator ("C:\") ends in an empty name.
        if(it->empty() && std::next(it) == folder.end())
        {
            break;
        }
        if(p == path.end() || !same_name(*p, *it))
        {
            return 0;
        }
    }
    return 1;
}

/// Finds missing files under a folder by file system identity, without changing anything (pass the result to
/// relink). Identities survive moves only within a volume, so search the drive the files were on. A folder on a
/// volume no missing file was on is searched only through mounts inside it. Paths already in the library aren't
/// offered. On early stop *error says why and the files found so far are returned; a volume that can't match
/// (wrong drive, FAT/exFAT) sets cross_device_link.
std::vector<moved_file> library::find_moved(const std::string &folder, std::error_code *error) const
{
    if(error)
    {
        error->clear();
    }
    std::unordered_map<std::string, file*> missing;
    std::unordered_set<std::string> volumes;
    for(file* f : missing_files())
    {
        const std::string &id = f->get_identity();
        if(!id.empty())
        {
            // Two files with the same identity are hard links to one file: either path will do.
            missing.emplace(id, f);
            volumes.insert(volume_of(id));
        }
    }

    std::vector<moved_file> out;
    if(missing.empty())
    {
        return out;
    }

    namespace fs = std::filesystem;
    // An identity is "volume:file", so the folder's own names its volume. If unreadable, search anyway and let
    // the scan report why. FAT and exFAT have no identity to match.
    std::vector<fs::path> roots;
    std::string folder_id = file_identity(folder);
    if(folder_id.empty() ? fat_volume(folder).empty() : volumes.count(volume_of(folder_id)) > 0)
    {
        roots.push_back(fs::u8path(folder));
    }
    else
    {
        std::vector<std::string> mounts;
        if(!mount_points(mounts))
        {
            // Mounts can't be listed, so any folder below may be on the right volume.
            roots.push_back(fs::u8path(folder));
        }
        // Mount points are written in full with links resolved, so the folder is too (junctions included, which
        // weakly_canonical doesn't resolve on Windows). Only mounts under it are opened, never an unrelated DVD
        // drive, card reader or stale network mount.
        std::error_code ec;
        std::string real = real_path(folder);
        fs::path base = real.empty() ? fs::weakly_canonical(fs::u8path(folder), ec) : fs::u8path(real);
        if(ec)
        {
            base = fs::absolute(fs::u8path(folder), ec);
        }
        for(const std::string &m : mounts)
        {
            if(!lexically_under(fs::u8path(m), base))
            {
                continue;
            }
            std::string id = file_identity(m);
            if(!id.empty() && volumes.count(volume_of(id)))
            {
                roots.push_back(fs::u8path(m));
            }
        }
        // A mount inside another one is searched with it. Sorted, a folder comes right before what is in it.
        std::sort(roots.begin(), roots.end());
        std::vector<fs::path> outer;
        for(const fs::path &root : roots)
        {
            if(outer.empty() || !lexically_under(root, outer.back()))
            {
                outer.push_back(root);
            }
        }
        roots.swap(outer);
        if(roots.empty())
        {
            if(error)
            {
                *error = std::make_error_code(std::errc::cross_device_link);
            }
            return out;
        }
    }

    std::error_code first_error;
    for(const fs::path &root : roots)
    {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        // Folders are checked too: one dropped into the library is a file of it.
        for(; !ec && !missing.empty() && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::string path = it->path().u8string();
            if(find_file(path))
            {
                continue;
            }
            auto found = missing.find(file_identity(path));
            if(found != missing.end())
            {
                out.push_back({found->second, path});
                missing.erase(found);
            }
        }
        if(ec && !first_error)
        {
            first_error = ec;
        }
    }
    if(error)
    {
        *error = first_error;
    }
    return out;
}

/// Points a file to a new path, keeping its tags (disk untouched). Database first. Fails if the path is taken.
bool library::relink(file* f, const std::string &given)
{
    std::string new_path = stored_path(given);
    auto it = f ? files_by_path.find(f->get_path()) : files_by_path.end();
    if(it == files_by_path.end() || it->second != f || files_by_path.count(new_path))
    {
        return 0;
    }
    std::string id = file_identity(new_path);
    if(db && !db->move_file(f->get_db_id(), new_path, id))
    {
        return 0;
    }
    files_by_path.erase(it);
    files_by_path[new_path] = f;
    f->set_path(new_path);
    f->set_identity(id);
    return 1;
}

/// Absolute, no "." or ".." or trailing separator (drive roots keep theirs), upper-case drive letter.
static std::string folder_key(const std::string &folder)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::absolute(fs::u8path(folder), ec);
    if(ec)
    {
        p = fs::u8path(folder);
    }
    p = p.lexically_normal();
    if(!p.has_filename() && p != p.root_path())
    {
        p = p.parent_path();
    }
    return stored_path(p.u8string());
}

/// Watches a folder: sync_watched adds files saved in it later. A folder under a watched one changes nothing;
/// one above watched folders replaces them. Database first.
bool library::watch_folder(const std::string &folder)
{
    namespace fs = std::filesystem;
    std::string key = folder_key(folder);
    fs::path path = fs::u8path(key);
    std::vector<std::string> inner;
    for(const std::string &w : watched)
    {
        if(lexically_under(path, fs::u8path(w)))
        {
            return 1;
        }
        if(lexically_under(fs::u8path(w), path))
        {
            inner.push_back(w);
        }
    }
    if(db)
    {
        if(!db->savepoint())
        {
            return 0;
        }
        bool ok = true;
        for(const std::string &w : inner)
        {
            ok = ok && db->unwatch_folder(w);
        }
        if(!ok || !db->watch_folder(key))
        {
            db->undo_savepoint();
            return 0;
        }
        if(!db->release_savepoint())
        {
            return 0;
        }
    }
    for(const std::string &w : inner)
    {
        watched.erase(std::find(watched.begin(), watched.end(), w));
    }
    watched.insert(std::upper_bound(watched.begin(), watched.end(), key), key);
    return 1;
}

/// Stops watching a folder; its files stay. Database first. True if it was watched.
bool library::unwatch_folder(const std::string &folder)
{
    namespace fs = std::filesystem;
    fs::path path = fs::u8path(folder_key(folder));
    for(auto it = watched.begin(); it != watched.end(); ++it)
    {
        fs::path w = fs::u8path(*it);
        if(lexically_under(path, w) && lexically_under(w, path))
        {
            if(db && !db->unwatch_folder(*it))
            {
                return 0;
            }
            watched.erase(it);
            return 1;
        }
    }
    return 0;
}

/// Watched folders, sorted.
const std::vector<std::string>& library::watched_folders() const
{
    return watched;
}

/// True if path is in a watched folder or under one.
bool library::is_watched(const std::string &path) const
{
    if(watched.empty())
    {
        return 0;
    }
    std::filesystem::path p = std::filesystem::u8path(path);
    for(const std::string &w : watched)
    {
        if(lexically_under(p, std::filesystem::u8path(w)))
        {
            return 1;
        }
    }
    return 0;
}

/// Keeps a file out of sync_watched while it has its current identity (remove_files uses it so the folder doesn't
/// re-add it). A new file at that path later is added. Adding by hand forgets it. Database first.
bool library::ignore_file(const std::string &path, const std::string &identity)
{
    std::string key = stored_path(path);
    if(db && !db->ignore_file(key, identity))
    {
        return 0;
    }
    ignored[key] = identity;
    return 1;
}

/// Takes in a rescan of a watched folder. Skips known files, temporary files (temporary_file) and removed ones
/// (ignore_file). A file with a missing library file's identity is that file moved: relinked with its tags. The
/// rest are added untagged. One atomic group, database first.
watch_sync library::sync_watched(const std::vector<scanned_file> &found)
{
    watch_sync out;
    std::unordered_map<std::string, file*> by_identity;
    bool indexed = false;
    std::unordered_set<std::string> batch;
    std::unordered_set<const file*> moving;
    std::vector<std::pair<std::string, const scanned_file*>> fresh;
    std::vector<std::pair<file*, const scanned_file*>> moves;
    std::vector<std::string> moved_paths, stale;
    for(const scanned_file &s : found)
    {
        std::string path = stored_path(s.path);
        if(files_by_path.count(path) || temporary_file(path) || !batch.insert(path).second)
        {
            continue;
        }
        auto skipped = ignored.find(path);
        if(skipped != ignored.end())
        {
            if(skipped->second == s.identity)
            {
                continue;
            }
            stale.push_back(path); // another file took its place
        }
        if(!s.identity.empty())
        {
            if(!indexed)
            {
                for(file* f : lib_files)
                {
                    if(!f->get_identity().empty())
                    {
                        by_identity.emplace(f->get_identity(), f);
                    }
                }
                indexed = true;
            }
            auto same = by_identity.find(s.identity);
            if(same != by_identity.end() && !moving.count(same->second) && file_missing(same->second->get_path()))
            {
                moving.insert(same->second);
                moves.emplace_back(same->second, &s);
                moved_paths.push_back(std::move(path));
                continue;
            }
        }
        fresh.emplace_back(std::move(path), &s);
    }
    if(fresh.empty() && moves.empty() && stale.empty())
    {
        return out;
    }

    // Rows first: memory only changes once they are saved.
    std::vector<std::int64_t> ids(fresh.size(), 0);
    if(db)
    {
        if(!db->savepoint())
        {
            out.failed = true;
            return out;
        }
        bool ok = true;
        for(std::size_t i = 0; ok && i < moves.size(); i++)
        {
            ok = db->move_file(moves[i].first->get_db_id(), moved_paths[i], moves[i].second->identity);
        }
        for(std::size_t i = 0; ok && i < fresh.size(); i++)
        {
            ids[i] = db->insert_file(fresh[i].first, fresh[i].second->identity);
            ok = ids[i] != 0;
        }
        for(std::size_t i = 0; ok && i < stale.size(); i++)
        {
            ok = db->unignore_file(stale[i]);
        }
        if(!ok)
        {
            db->undo_savepoint();
            out.failed = true;
            return out;
        }
        if(!db->release_savepoint())
        {
            out.failed = true;
            return out;
        }
    }

    for(const std::string &path : stale)
    {
        ignored.erase(path);
    }
    for(std::size_t i = 0; i < moves.size(); i++)
    {
        file* f = moves[i].first;
        out.relinked_from.push_back(f->get_path());
        files_by_path.erase(f->get_path());
        files_by_path[moved_paths[i]] = f;
        f->set_path(moved_paths[i]);
        f->set_identity(moves[i].second->identity);
        out.relinked.push_back(f);
    }
    lib_files.reserve(lib_files.size() + fresh.size());
    for(std::size_t i = 0; i < fresh.size(); i++)
    {
        file* f = new file(fresh[i].first);
        f->set_identity(fresh[i].second->identity);
        f->set_db_id(ids[i]);
        files_by_path[f->get_path()] = f;
        lib_files.push_back(f);
        out.added.push_back(f);
    }
    return out;
}

/// Records how far a file was read, replacing the old record; it follows relinks and goes on removal. Fails for
/// a negative position or length, or a position not below a known length. last_read 0 means now.
bool library::set_progress(file* f, reading_progress progress)
{
    if(!owns(f) || progress.position < 0 || progress.length < 0 ||
        (progress.length > 0 && progress.position >= progress.length))
    {
        return 0;
    }
    if(progress.last_read == 0)
    {
        progress.last_read = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    if(db && !db->set_progress(f->get_db_id(), progress))
    {
        return 0;
    }
    f->set_progress(progress);
    return 1;
}

/// Forgets a file's reading progress. True if the file is ours and has none afterwards.
bool library::clear_progress(file* f)
{
    if(!owns(f))
    {
        return 0;
    }
    if(!f->get_progress())
    {
        return 1;
    }
    if(db && !db->clear_progress(f->get_db_id()))
    {
        return 0;
    }
    f->clear_progress();
    return 1;
}

/// Files with reading progress, most recently read first (ties by path). limit 0 means all.
std::vector<file*> library::recently_read(std::size_t limit) const
{
    std::vector<file*> out;
    for(file* f : lib_files)
    {
        if(f->get_progress())
        {
            out.push_back(f);
        }
    }
    auto newer = [](const file* a, const file* b)
    {
        std::int64_t ta = a->get_progress()->last_read;
        std::int64_t tb = b->get_progress()->last_read;
        return ta != tb ? ta > tb : a->get_path() < b->get_path();
    };
    if(limit > 0 && limit < out.size())
    {
        std::partial_sort(out.begin(), out.begin() + limit, out.end(), newer);
        out.resize(limit);
    }
    else
    {
        std::sort(out.begin(), out.end(), newer);
    }
    return out;
}

/// Renames a tag (see is_valid_tag_name). If another tag has the name, case aside, they merge: that tag keeps its
/// spelling and gains the files, and t is deleted. A different spelling of the same name just renames. Atomic.
/// *result gets the tag now carrying the files, or nullptr if nothing was done.
rename_result library::rename_tag(tag* t, const std::string &name, tag** result, tag_change *change)
{
    recording rec(*this, change);
    if(result)
    {
        *result = nullptr;
    }
    auto self = t ? tags_by_name.find(t->get_folded_id()) : tags_by_name.end();
    if(self == tags_by_name.end() || self->second != t || !is_valid_tag_name(name))
    {
        return rename_result::Invalid;
    }
    if(t->get_id() == name)
    {
        if(result)
        {
            *result = t;
        }
        return rec.done(rename_result::Unchanged);
    }
    const std::string folded = fold_case(name);
    auto other = tags_by_name.find(folded);
    tag* target = other != tags_by_name.end() && other->second != t ? other->second : nullptr;
    if(!target)
    {
        if(db && !db->rename_tag(t->get_db_id(), name))
        {
            return rename_result::Failed;
        }
        // As a record, a rename is the old name going and the new one coming, with the files moving.
        if(log)
        {
            for(const file* f : t->get_files())
            {
                note_link(f, t, false);
            }
            note_tag(t, false);
        }
        tags_by_name.erase(self);
        tags_by_name[folded] = t;
        t->set_id(name);
        if(log)
        {
            note_tag(t, true);
            for(const file* f : t->get_files())
            {
                note_link(f, t, true);
            }
        }
        if(result)
        {
            *result = t;
        }
        return rec.done(rename_result::Renamed);
    }
    // Merge: the files first, then the old tag, as one atomic group. Memory changes only once it is written.
    const std::vector<file*> moving = t->get_files();
    if(db)
    {
        bool ok = db->savepoint();
        for(std::size_t i = 0; ok && i < moving.size(); ++i)
        {
            ok = db->link(moving[i]->get_db_id(), target->get_db_id());
        }
        ok = ok && db->delete_tag(t->get_db_id());
        if(!ok)
        {
            db->undo_savepoint();
            return rename_result::Failed;
        }
        if(!db->release_savepoint())
        {
            return rename_result::Failed;
        }
    }
    for(file* f : moving)
    {
        if(log)
        {
            if(!f->tagged(target))
            {
                note_link(f, target, true);
            }
            note_link(f, t, false);
        }
        f->add_tag(target);
    }
    note_tag(t, false);
    tags_by_name.erase(self);
    tags.erase(std::find(tags.begin(), tags.end(), t));
    delete t;
    if(result)
    {
        *result = target;
    }
    return rec.done(rename_result::Merged);
}

/// Renames a branch of nested tags: the tag called branch, if there is one, and every tag under it get name in
/// place of branch ("Music/Old" -> "Music/New" also turns "Music/Old/Disc_1" into "Music/New/Disc_1"). Case aside,
/// like tag names. Each one merges into a tag that already has its new name, as rename_tag does. One savepoint.
/// Returns how many tags were renamed or merged (0 if none is under branch or nothing changes), or -1 if name is
/// invalid or under branch itself, or the database refused; then the error stays in last_error() for the caller's
/// rollback, since tags renamed before the failure have their new names in memory.
int library::rename_branch(const std::string &branch, const std::string &name, tag_change *change)
{
    recording rec(*this, change);
    const std::string from = fold_case(branch);
    if(from.empty() || !is_valid_tag_name(name) || (fold_case(name) != from && tag_under(fold_case(name), from)))
    {
        return -1;
    }
    // Outermost first, so a parent merging into an existing tag happens before its children follow it.
    std::vector<tag*> moving;
    for(tag* t : tags)
    {
        if(tag_under(t->get_folded_id(), from))
        {
            moving.push_back(t);
        }
    }
    std::sort(moving.begin(), moving.end(),
        [](const tag* a, const tag* b) { return a->get_folded_id().size() < b->get_folded_id().size(); });
    if(moving.empty())
    {
        return rec.done(0);
    }
    if(db && !db->savepoint())
    {
        return -1;
    }
    // The part of each name below the branch, found by levels: folding can change a name's length in bytes.
    const std::size_t levels = std::count(branch.begin(), branch.end(), tag_path_separator) + 1;
    int changed = 0;
    for(tag* t : moving)
    {
        const std::string &id = t->get_id();
        std::size_t below = 0;
        for(std::size_t level = 0; level < levels && below != std::string::npos; ++level)
        {
            below = id.find(tag_path_separator, level == 0 ? 0 : below + 1);
        }
        std::string renamed = name + (below == std::string::npos ? std::string() : id.substr(below));
        rename_result r = rename_tag(t, renamed);
        if(r == rename_result::Failed || r == rename_result::Invalid)
        {
            if(db)
            {
                db->undo_savepoint();
            }
            return -1;
        }
        if(r != rename_result::Unchanged)
        {
            changed++;
        }
    }
    if(db && !db->release_savepoint())
    {
        return -1;
    }
    return rec.done(changed);
}

/// Deletes the tag named branch (if it is a tag of its own) and moves everything nested under it up one level, to
/// branch's parent: "Music/Rock/Live" becomes "Rock/Live" when "Music" goes. A moved name that already exists merges
/// into it. Files keep their other tags. One savepoint. Returns how many tags were renamed, or -1 (nothing changed,
/// unless last_error says the database and memory differ). moves, if given, lists each direct child's old and new name
/// (the spelling it had, whether or not a tag of its own), for an undo.
int library::delete_tag_keep_children(const std::string &branch, std::vector<tag_move> *moves, tag_change *change)
{
    recording rec(*this, change);
    const std::string from = fold_case(branch);
    if(from.empty())
    {
        return -1;
    }
    // The direct children, as the first level below the branch in the spelling of the first tag found.
    std::vector<std::string> children;
    std::set<std::string> seen;
    const std::size_t levels = std::count(branch.begin(), branch.end(), tag_path_separator) + 1;
    for(const tag* t : tags)
    {
        if(t->get_folded_id() == from || !tag_under(t->get_folded_id(), from))
        {
            continue;
        }
        const std::string &id = t->get_id();
        std::size_t start = 0;
        for(std::size_t level = 0; level < levels; ++level)
        {
            start = id.find(tag_path_separator, level == 0 ? 0 : start) + 1;
        }
        std::size_t end = id.find(tag_path_separator, start);
        std::string child = id.substr(start, end == std::string::npos ? end : end - start);
        if(seen.insert(fold_case(child)).second)
        {
            children.push_back(std::move(child));
        }
    }
    tag* self = find_tag(branch);
    if(!self && children.empty())
    {
        return rec.done(0);
    }
    // A child named like the tag itself ("Rock/Rock" under "Rock") moves to where the tag was, so its own children
    // land on the other children's old names ("Rock/Rock/Live" on "Rock/Live"). It goes last, once those have moved.
    const std::string last = fold_case(branch.substr(branch.rfind(tag_path_separator) + 1));
    std::stable_partition(children.begin(), children.end(), [&](const std::string &c) { return fold_case(c) != last; });
    if(db && !db->savepoint())
    {
        return -1;
    }
    const std::string parent = tag_parent(branch);
    const std::string prefix = parent.empty() ? std::string() : parent + tag_path_separator;
    const std::string below = (self ? self->get_id() : branch) + tag_path_separator;
    int changed = 0;
    std::vector<tag_move> done;
    bool ok = true;
    // The tag goes first, so a child with its own name ("Rock/Rock" under "Rock") is renamed to a fresh tag instead of
    // merging into the one being deleted.
    if(self && !delete_tag(self))
    {
        ok = false;
    }
    for(const std::string &child : children)
    {
        if(!ok)
        {
            break;
        }
        int n = rename_branch(below + child, prefix + child);
        if(n < 0)
        {
            ok = false;
            break;
        }
        changed += n;
        done.emplace_back(below + child, prefix + child);
    }
    if(!ok)
    {
        if(db)
        {
            db->undo_savepoint();
        }
        return -1;
    }
    if(db && !db->release_savepoint())
    {
        return -1;
    }
    if(moves)
    {
        *moves = std::move(done);
    }
    return rec.done(changed);
}

/// Creates a tag, or returns the existing one with that name, case ignored ("Beach" and "beach" are one tag).
/// Database first. nullptr if the database failed.
tag* library::create_tag(const std::string &name)
{
    std::string folded = fold_case(name);
    tag* &slot = tags_by_name[folded];
    if(slot)
    {
        return slot;
    }
    tag* t = new tag(name);
    if(db)
    {
        t->set_db_id(db->insert_tag(name));
        if(!t->get_db_id())
        {
            tags_by_name.erase(folded);
            delete t;
            return nullptr;
        }
    }
    slot = t;
    tags.push_back(t);
    note_tag(t, true);
    return t;
}

/// Deletes a tag, taking it off every file; the pointer is dead afterwards. Database first.
bool library::delete_tag(tag* t, tag_change *change)
{
    recording rec(*this, change);
    auto it = t ? tags_by_name.find(t->get_folded_id()) : tags_by_name.end();
    if(it == tags_by_name.end() || it->second != t)
    {
        return 0;
    }
    if(db && !db->delete_tag(t->get_db_id()))
    {
        return 0;
    }
    if(log)
    {
        for(const file* f : t->get_files())
        {
            note_link(f, t, false);
        }
        note_tag(t, false);
    }
    tags_by_name.erase(it);
    tags.erase(std::find(tags.begin(), tags.end(), t));
    delete t; // unlinks it from its files
    return rec.done(1);
}

/// Same as create_tag.
tag* library::retrieve_tag(const std::string &name)
{
    return create_tag(name);
}

/// Looks a tag up by name, case ignored; nullptr if absent.
tag* library::find_tag(const std::string &name) const
{
    auto it = tags_by_name.find(fold_case(name));
    return it != tags_by_name.end() ? it->second : nullptr;
}

const std::vector<file*>& library::get_files() const
{
    return lib_files;
}

const std::vector<tag*>& library::get_tags() const
{
    return tags;
}

/// Searches by name and tag (query syntax in the README). Each plain word must appear in the path or a tag name,
/// ignoring case and accents; each {...} group must match the file's tags, case ignored. The Missing view hits
/// the disk for each file matching the rest.
SearchResult library::search(const std::string &query, LibraryView view) const
{
    std::vector<std::string> words;
    std::vector<std::unique_ptr<QueryNode>> groups;
    size_t pos = 0;
    while (true)
    {
        size_t open = query.find('{', pos);
        AddWords(query.substr(pos, open == std::string::npos ? std::string::npos : open - pos), words);
        if (open == std::string::npos)
            break;

        size_t close = query.find('}', open);
        if (close == std::string::npos || query.find('{', open + 1) < close)
            return { {}, SearchError::UnclosedBrace };

        std::unique_ptr<QueryNode> ast = ParseQuery(query.substr(open + 1, close - open - 1));
        if (!ast)
            return { {}, SearchError::BadExpression };
        groups.push_back(std::move(ast));
        pos = close + 1;
    }

    if (words.empty() && groups.empty() && view == LibraryView::All)
        return { lib_files };

    // The tags whose names hold each word: there are few tags, so each file then only compares pointers.
    std::vector<std::unordered_set<const tag*>> wordTags(words.size());
    for (size_t i = 0; i < words.size(); i++)
    {
        for (const tag* t : tags)
        {
            if (t->get_search_name().find(words[i]) != std::string::npos)
                wordTags[i].insert(t);
        }
    }

    std::vector<file*> results;
    for (file* f : lib_files)
    {
        const std::vector<tag*> &fileTags = f->get_tags();
        if (view == LibraryView::Untagged && !fileTags.empty())
            continue;
        bool matches = true;
        for (size_t i = 0; matches && i < words.size(); i++)
        {
            matches = f->get_search_path().find(words[i]) != std::string::npos ||
                std::any_of(fileTags.begin(), fileTags.end(), [&](const tag* t) { return wordTags[i].count(t) > 0; });
        }
        for (size_t i = 0; matches && i < groups.size(); i++)
            matches = groups[i]->Evaluate(fileTags);
        // The disk last, and only for files that match the rest.
        if (matches && (view != LibraryView::Missing || file_missing(f->get_path())))
            results.push_back(f);
    }
    return { results };
}

/// Clears all files, tags and watched folders in memory; the database is untouched.
void library::clear()
{
    // Tags first: each one unlinks its files in one pass, so the files are untagged when deleted.
    for (tag* t : this->tags)
    {
        delete t;
    }
    for (file* f : this->lib_files)
    {
        delete f;
    }
    lib_files.clear();
    tags.clear();
    files_by_path.clear();
    tags_by_name.clear();
    watched.clear();
    ignored.clear();
}

library::~library()
{
    clear();
}