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

static std::string TrimSpaces(const std::string &s)
{
    size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

/// @param foldedQuery The text searched for, already folded (see fold_case).
static bool MatchesSubstring(file *f, const std::string &foldedQuery)
{
    if (fold_case(f->get_path()).find(foldedQuery) != std::string::npos)
        return true;

    for (tag* t : f->get_tags())
    {
        if (t->get_folded_id().find(foldedQuery) != std::string::npos)
            return true;
    }
    return false;
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
/// so it can be found again if it is moved (see find_moved).
/// @param given Path for the file to be added.
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
    }
    slot = f;
    lib_files.push_back(f);
    if(missing_checked && file_missing(path))
    {
        missing.insert(f);
    }
    // A new file has no tags, so it is only visible when no filter is applied.
    if(seen_tags.empty())
    {
        seen_files.push_back(f);
    }
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
        // The scan just found them, so they aren't missing: the Missing view stays as it is.
        // New files have no tags, so they are only visible when no filter is applied.
        if(seen_tags.empty())
        {
            seen_files.push_back(f);
        }
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

void library::edit_file(file* f, tag* t, const bool add)
{
    if(add == f->tagged(t))
    {
        return;
    }
    if(add)
    {
        f->add_tag(t);
        if(db)
        {
            db->link(f->get_db_id(), t->get_db_id());
        }
    }
    else // !add
    {
        f->remove_tag(t);
        if(db)
        {
            db->unlink(f->get_db_id(), t->get_db_id());
        }
    }
    // Only a tag that is part of the filter can change which files are visible.
    if(std::find(seen_tags.begin(), seen_tags.end(), t) != seen_tags.end())
    {
        update_seen_files();
    }
}

/// @brief Removes a file from the library and deletes it (the file on disk is untouched).
/// The pointer is invalid afterwards. With a database attached, the row goes first, and nothing
/// changes in memory if that fails.
/// @param f File to be removed.
/// @return A boolean describing whether the file was removed.
bool library::remove_file(file* f)
{
    auto it = f ? files_by_path.find(f->get_path()) : files_by_path.end();
    if(it == files_by_path.end() || it->second != f)
    {
        return 0;
    }
    if(db && !db->delete_file(f->get_db_id()))
    {
        return 0;
    }
    files_by_path.erase(it);
    lib_files.erase(std::find(lib_files.begin(), lib_files.end(), f));
    // The other visible files stay visible, so the view only loses this one.
    auto seen = std::find(seen_files.begin(), seen_files.end(), f);
    if(seen != seen_files.end())
    {
        seen_files.erase(seen);
    }
    missing.erase(f); // a later file could get the same address
    delete f; // unlinks it from its tags
    return 1;
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

/// @brief Checks the disk for every file, as missing_files does, and keeps the result for the Missing view.
/// Searches in Missing don't touch the disk: they show the files missing at the last refresh, kept up to date
/// as files are added, removed or relinked. Call it when the Missing view is chosen, or the disk may have changed.
/// @return The missing files.
std::vector<file*> library::refresh_missing()
{
    std::vector<file*> out = missing_files();
    missing.clear();
    missing.insert(out.begin(), out.end());
    missing_checked = true;
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
    if(missing_checked)
    {
        if(file_missing(new_path))
            missing.insert(f);
        else
            missing.erase(f);
    }
    return 1;
}

/// @brief Tells whether a file is this library's own (not one of another library, or deleted).
static bool owned(const std::unordered_map<std::string, file*> &files_by_path, const file* f)
{
    auto it = f ? files_by_path.find(f->get_path()) : files_by_path.end();
    return it != files_by_path.end() && it->second == f;
}

/// @brief Records how far a file was read, replacing what was recorded before. It stays with the file when the
/// file is relinked, and goes when it is removed. With a database attached, the row is written first, and
/// nothing changes in memory if that fails.
/// @param progress Where reading stopped. A last_read of 0 means now.
/// @return A boolean describing whether it was recorded. It isn't if the position is negative or, with a known
/// length, not below it (a position is an index from 0), or the length is negative.
bool library::set_progress(file* f, reading_progress progress)
{
    if(!owned(files_by_path, f) || progress.position < 0 || progress.length < 0 ||
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
    if(!owned(files_by_path, f))
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

/// @brief Changes the view filter applied to the files in the library.
/// @param tofind Tag to be added or removed from the filter;
/// @param add Boolean describing whether the tag should be added(1) or deleted(0) from the filter.
void library::filter_files(tag* tofind, const bool add)
{
    // 1 -> add, 0 -> remove
    if(add)
    {
        if(std::find(this->seen_tags.begin(), this->seen_tags.end(), tofind) == this->seen_tags.end())
            this->seen_tags.push_back(tofind);
        else
            return;
    }
    else
    {
        if(std::find(this->seen_tags.begin(), this->seen_tags.end(), tofind) == this->seen_tags.end())
            return;
        else
            this->seen_tags.erase(std::find(this->seen_tags.begin(), this->seen_tags.end(), tofind));
    }
    update_seen_files();
}


void library::update_seen_files()
{
    std::unordered_set<file*> out;

    if(seen_tags.size() == 0)
    {
        seen_files = lib_files;
        return;
    }

    for(const tag* ta : this->seen_tags)
    {
        for(file* fi : ta->get_files())
        {
            out.insert(fi);
        }
    }
    
    for(const tag* ta : this->seen_tags)
    {
        std::unordered_set<file*> temp;
        for(file* f : out)
        {
            if(f->tagged(ta))
            {
                temp.insert(f);
            }
        }
        out = temp;
    }

    this->seen_files.assign(out.begin(), out.end());
}

/// @brief edits a tag identifier.
/// @param t tag to be edited.
/// @param a the new identifier intended for the tag.
/// @return a boolean describing a tag with that identifier already exists. 
bool library::edit_tag(tag* t, const std::string &a)
{
    if(tags_by_name.count(a))
    {
        return 0;
    }
    tags_by_name.erase(t->get_id());
    tags_by_name[a] = t;
    t->set_id(a);
    if(db)
    {
        db->rename_tag(t->get_db_id(), a);
    }
    return 1;
}

tag* library::create_tag(const std::string &name)
{
    tag* &slot = tags_by_name[name];
    if(slot)
    {
        return slot;
    }
    tag* t = new tag(name);
    if(db)
    {
        t->set_db_id(db->insert_tag(name));
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
    auto it = t ? tags_by_name.find(t->get_id()) : tags_by_name.end();
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
    auto filtered = std::find(seen_tags.begin(), seen_tags.end(), t);
    bool was_filtered = filtered != seen_tags.end();
    if(was_filtered)
    {
        seen_tags.erase(filtered);
    }
    delete t; // unlinks it from its files
    // A tag in the filter narrowed the view: without it, more files can be visible.
    if(was_filtered)
    {
        update_seen_files();
    }
    return 1;
}

tag* library::retrieve_tag(const std::string &name)
{
    tag* t = find_tag(name);
    if(t)
    {
        return t;
    }
    return this->create_tag(name);
}

/// @brief Looks a tag up by name without creating it.
/// @return The tag, or nullptr if there is none with that name.
tag* library::find_tag(const std::string &name) const
{
    auto it = tags_by_name.find(name);
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

const std::vector<tag*>& library::current_filter() const
{
    return seen_tags;
}

const std::vector<file*>& library::show() const
{
    return seen_files;
}

/// @brief Searches the files on screen by name and tag; see the README for the query syntax.
/// @param view Narrows the search to a Library view. Missing uses the files found by the last refresh_missing,
/// and only checks the disk (once per file) if there was none yet.
/// Building that list changes the library, so search must not be called from two threads at once.
SearchResult library::search(const std::string &query, LibraryView view) const
{
    // Split the query into its {...} groups and the plain text around them.
    // The spaces around a group only separate it from the text: "foo {x} bar" searches for "foo bar".
    std::string plainText;
    std::vector<std::unique_ptr<QueryNode>> groups;
    size_t pos = 0;
    while (true)
    {
        size_t open = query.find('{', pos);
        std::string piece = TrimSpaces(query.substr(pos, open == std::string::npos ? std::string::npos : open - pos));
        if (!piece.empty())
            plainText += (plainText.empty() ? "" : " ") + piece;
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

    if (plainText.empty() && groups.empty() && view == LibraryView::All)
        return { seen_files };

    if (view == LibraryView::Missing && !missing_checked)
    {
        std::vector<file*> found = missing_files();
        missing.insert(found.begin(), found.end());
        missing_checked = true;
    }

    // Every group must match, as well as the text and the view.
    std::string foldedText = fold_case(plainText);
    std::vector<file*> results;
    for (file* f : seen_files)
    {
        if (!plainText.empty() && !MatchesSubstring(f, foldedText))
            continue;
        std::vector<tag*> fileTags = f->get_tags();
        if (view == LibraryView::Untagged && !fileTags.empty())
            continue;
        bool matchesGroups = true;
        for (const std::unique_ptr<QueryNode> &ast : groups)
        {
            if (!ast->Evaluate(fileTags))
            {
                matchesGroups = false;
                break;
            }
        }
        if (matchesGroups && (view != LibraryView::Missing || missing.count(f)))
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
    seen_files.clear();
    seen_tags.clear();
    missing.clear();
    missing_checked = false;
}

library::~library()
{
    clear();
}