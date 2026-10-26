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

/// @brief Splits the plain text of a search into its words, folded for searching (see fold_search). Repeats go.
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

/// @brief Writes a path the way the library keeps it. On Windows the drive letter is upper case, as Explorer
/// and file pickers write it, so "c:\photos\a.jpg" typed by hand is the same file as "C:\photos\a.jpg".
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

/// @brief Attaches a database: from now on every change to the library is also written to it right away.
/// Attaching doesn't load anything (see database::load) or write what is already in memory.
/// @param d The database, or nullptr to keep the library in memory only.
void library::attach(database* d)
{
    this->db = d;
}

database* library::attached() const
{
    return db;
}

/// @brief Adds a file to the library. With a database attached, its file system identity is recorded too,
/// so it can be found again if it is moved (see find_moved), and the row is written first: nothing changes in
/// memory if that fails.
/// @param given Path for the file to be added.
/// @return The file (the one already there if the library has that path), or nullptr if the database failed
/// (its last_error says why).
file* library::add_file(const std::string &given)
{
    std::string path = stored_path(given);
    file* &slot = files_by_path[path];
    if(slot)
    {
        return slot;
    }
    file* f = new file(path);
    if(db)
    {
        f->set_identity(file_identity(path));
        f->set_db_id(db->insert_file(path, f->get_identity()));
        if(!f->get_db_id())
        {
            files_by_path.erase(path);
            delete f;
            return nullptr;
        }
    }
    slot = f;
    lib_files.push_back(f);
    return f;
}

/// @brief Adds the files a scan found (see scan_folder) that aren't in the library yet, keeping the identities
/// read by the scan. With a database attached, they are saved in one transaction of their own, so it must not
/// be called inside another one: if any of it fails, the transaction is rolled back and the library is left as
/// it was.
/// @return How many files were added, or -1 if the database failed (its last_error says why).
int library::add_files(const std::vector<scanned_file> &files)
{
    // Each new file's path, and the scan that found it.
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
            if(!ids[i])
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
    }
    return static_cast<int>(fresh.size());
}

/// @brief Looks a file up by path without adding it.
/// @return The file, or nullptr if there is none with that path.
file* library::find_file(const std::string &path) const
{
    auto it = files_by_path.find(stored_path(path));
    return it != files_by_path.end() ? it->second : nullptr;
}

/// @brief Adds a tag to a file or takes it off. With a database attached, the link is written first, and nothing
/// changes in memory if that fails.
/// @return A boolean describing whether the file now is as asked (true too if it already was).
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

/// @brief Adds or removes tags on many files at once: the database writes are one atomic group (inside the
/// caller's begin/commit if there is one). Pairs that already are as asked are
/// skipped. Pointers must belong to this library; repeats are ignored.
/// @param files Files to edit.
/// @param tags Tags to add to or remove from each of them.
/// @param add Whether to add (true) or remove (false).
/// @return How many file-tag links changed, or -1 if the database failed (nothing changed then).
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

/// @brief Tells whether a file is this library's own (not one of another library, or deleted).
bool library::owns(const file* f) const
{
    auto it = f ? files_by_path.find(f->get_path()) : files_by_path.end();
    return it != files_by_path.end() && it->second == f;
}

/// @brief Removes a file from the library and deletes it (the file on disk is untouched).
/// The pointer is invalid afterwards. With a database attached, the row goes first, and nothing
/// changes in memory if that fails. To remove many files, remove_files is much faster.
/// @param f File to be removed.
/// @return A boolean describing whether the file was removed.
bool library::remove_file(file* f)
{
    if(!owns(f))
    {
        return 0;
    }
    if(db && !db->delete_file(f->get_db_id()))
    {
        return 0;
    }
    files_by_path.erase(f->get_path());
    lib_files.erase(std::find(lib_files.begin(), lib_files.end(), f));
    delete f; // unlinks it from its tags
    return 1;
}

/// @brief Removes files from the library and deletes them (the files on disk are untouched), in time linear in
/// the size of the library however many there are. The pointers are invalid afterwards. With a database
/// attached, the rows go first as one atomic group (inside the caller's begin/commit if there is one), and
/// nothing changes in memory if that fails. Tags left on no file stay (see delete_tag).
/// @param files Files to remove. Repeats, nullptr and files that aren't this library's are skipped.
/// @return How many files were removed, or -1 if the database failed.
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
        if(!db->release_savepoint())
        {
            return -1;
        }
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
        f->tags.clear(); // already off its tags
        delete f;
    }
    return static_cast<int>(list.size());
}

/// @brief Lists the files that are no longer at their path (see file_missing). Checks the disk once per file.
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

/// @brief Records the file system identity of every file that is at its path and has none yet or a
/// different one: files added without a database, stored before the database kept identities, or replaced
/// by a program that saves to a temporary file and renames it over the original (which gives it a new identity).
/// Identities recorded on FAT or exFAT, which can never match (see file_identity), are forgotten once that
/// volume is back at the file's path, so those files show as not findable. Only files with an up to date identity
/// can be found again after they move.
/// Checks the disk once per file. Wrap it in a transaction to save it all at once.
/// @return How many identities were recorded or forgotten.
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
            // A missing file was on FAT if the folder it was in is, and on that very volume: a FAT stick
            // given the letter of an unplugged NTFS drive says nothing about the files that were on the drive.
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

/// @brief Tells whether two names in a path are the same. Windows paths ignore case.
static bool same_name(const std::filesystem::path &a, const std::filesystem::path &b)
{
#ifdef _WIN32
    return fold_case(a.u8string()) == fold_case(b.u8string());
#else
    return a == b;
#endif
}

/// @brief Tells whether a path is a folder or something under it, comparing their names only.
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

/// @brief Looks for missing files in a folder and everything under it, by their file system identity.
/// Changes nothing: pass what it finds to relink. A path already in the library isn't offered.
/// An identity only survives a move within the same volume, so look on the drive the files were on.
/// A folder on a volume none of the missing files were on is only searched where such a volume is mounted
/// inside it (/home on its own partition when searching /, say), and not at all if there is none.
/// @param folder Folder to search, in UTF-8.
/// @param error If given, set to why the search stopped early (the folder can't be read, say), or cleared.
/// The files found before that are still returned. A folder that wasn't searched because none of the
/// missing files can be on its volume (the wrong drive, or a FAT or exFAT one) sets std::errc::cross_device_link.
/// @return Each missing file found, with its new path.
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
    // An identity is "volume:file", so the folder's own tells which volume it is on. If it can't be read,
    // search anyway and let the scan report why. Nothing on FAT or exFAT has an identity to match.
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
        // weakly_canonical doesn't resolve on Windows), and only mounts under it are opened: an unrelated DVD
        // drive, card reader or stale network mount is never touched.
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

/// @brief Points a file of the library to a new path (the file on disk is untouched), keeping its tags.
/// With a database attached, the row changes first, and nothing changes in memory if that fails.
/// @param f File to be relinked.
/// @param given Where the file is now. No other file of the library may have it.
/// @return A boolean describing whether the file was relinked.
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

/// @brief Records how far a file was read, replacing what was recorded before. It stays with the file when the
/// file is relinked, and goes when it is removed. With a database attached, the row is written first, and
/// nothing changes in memory if that fails.
/// @param progress Where reading stopped. A last_read of 0 means now.
/// @return A boolean describing whether it was recorded. It isn't if the position is negative or, with a known
/// length, not below it (a position is an index from 0), or the length is negative.
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

/// @brief Forgets how far a file was read (it then opens at the start). With a database attached, the row goes
/// first, and nothing changes in memory if that fails.
/// @return A boolean describing whether the file is this library's and its progress is gone (true too if it had none).
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

/// @brief Lists the files with reading progress, the most recently read first (by path when read at the same time).
/// @param limit How many to list at most, or 0 for all of them.
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

/// @brief Renames a tag. Names follow is_valid_tag_name. If another tag already has the name, case aside (the
/// same rule searches use), the two are merged: that tag keeps its own spelling and gains the files of `t`, and
/// `t` is deleted (its pointer is invalid afterwards). Renaming a tag to a different spelling of its own name
/// just changes the spelling. The change is atomic with a database attached.
/// @param t Tag to rename.
/// @param name The new name.
/// @param result If given, set to the tag that now carries the files (t itself, or the one merged into), or
/// nullptr if nothing was done.
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

/// @brief Creates a tag, or finds the one the library already has under that name with case ignored (it keeps
/// its own spelling: "Beach" and "beach" are one tag). With a database attached, the row is written first, and
/// nothing changes in memory if that fails.
/// @return The tag, or nullptr if the database failed (its last_error says why).
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

/// @brief Deletes a tag, taking it off every file that has it. The pointer is invalid afterwards.
/// With a database attached, the row goes first, and nothing changes in memory if that fails.
/// @param t Tag to be deleted.
/// @return A boolean describing whether the tag was deleted.
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

/// @brief The same as create_tag: the tag with that name (case aside), created if there is none.
tag* library::retrieve_tag(const std::string &name)
{
    return create_tag(name);
}

/// @brief Looks a tag up by name without creating it. Case is ignored, as in searches.
/// @return The tag, or nullptr if there is none with that name.
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

/// @brief Searches the library by name and tag; see the README for the query syntax. Each word of the plain
/// text must be found, in any order, in the file's path or in one of its tag names, with case and accents
/// ignored (see fold_search). Each {...} group must match the file's tags (names compared with case ignored).
/// @param view Narrows the search to a Library view. Missing checks the disk for each file that matches the rest.
SearchResult library::search(const std::string &query, LibraryView view) const
{
    // Split the query into its {...} groups and the words around them.
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

    // Every word and every group must match, as well as the view.
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

/// @brief Deletes every file and tag in memory. Doesn't touch the attached database.
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
}

library::~library()
{
    clear();
}