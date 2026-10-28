#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <unordered_set>
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

/// Adds or removes a tag on a file, database first. True if the file ends up as asked.
bool library::edit_file(file* f, tag* t, const bool add)
{
    if(!f || !t)
    {
        return 0;
    }
    if(add == f->tagged(t))
    {
        return 1;
    }
    if(db && !(add ? db->link(f->get_db_id(), t->get_db_id()) : db->unlink(f->get_db_id(), t->get_db_id())))
    {
        return 0;
    }
    if(add)
    {
        f->add_tag(t);
    }
    else
    {
        f->remove_tag(t);
    }
    return 1;
}

/// Bulk add_tag/remove_tag as one atomic database group. Skips pairs already as asked and repeats.
/// Returns the links changed, or -1 if the database failed (nothing changed).
int library::edit_files(const std::vector<file*> &files, const std::vector<tag*> &tags, const bool add)
{
    std::unordered_set<const file*> seen_f;
    std::unordered_set<const tag*> seen_t;
    std::vector<tag*> ts;
    for(tag* t : tags)
    {
        if(t && seen_t.insert(t).second)
        {
            ts.push_back(t);
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
    if(changes.empty())
    {
        return 0;
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
                return -1;
            }
        }
        if(!db->release_savepoint())
        {
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
    }
    return static_cast<int>(changes.size());
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
rename_result library::rename_tag(tag* t, const std::string &name, tag** result)
{
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
        return rename_result::Unchanged;
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
        tags_by_name.erase(self);
        tags_by_name[folded] = t;
        t->set_id(name);
        if(result)
        {
            *result = t;
        }
        return rename_result::Renamed;
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
        f->add_tag(target);
    }
    tags_by_name.erase(self);
    tags.erase(std::find(tags.begin(), tags.end(), t));
    delete t;
    if(result)
    {
        *result = target;
    }
    return rename_result::Merged;
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
    return t;
}

/// Deletes a tag, taking it off every file; the pointer is dead afterwards. Database first.
bool library::delete_tag(tag* t)
{
    auto it = t ? tags_by_name.find(t->get_folded_id()) : tags_by_name.end();
    if(it == tags_by_name.end() || it->second != t)
    {
        return 0;
    }
    if(db && !db->delete_tag(t->get_db_id()))
    {
        return 0;
    }
    tags_by_name.erase(it);
    tags.erase(std::find(tags.begin(), tags.end(), t));
    delete t; // unlinks it from its files
    return 1;
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