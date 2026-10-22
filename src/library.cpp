#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <unordered_map>

#include "../include/database.hpp"
#include "../include/file.hpp"
#include "../include/file_identity.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"

static std::string ToLower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string TrimSpaces(const std::string &s)
{
    size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

static bool ContainsCaseInsensitive(const std::string &haystack, const std::string &needle)
{
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

static bool MatchesSubstring(file *f, const std::string &query)
{
    if (ContainsCaseInsensitive(f->get_path(), query))
        return true;

    for (tag* t : f->get_tags())
    {
        if (ContainsCaseInsensitive(t->get_id(), query))
            return true;
    }
    return false;
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
/// @param path Path for the file to be added.
file* library::add_file(const std::string &path)
{
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
    // A new file has no tags, so it is only visible when no filter is applied.
    if(seen_tags.empty())
    {
        seen_files.push_back(f);
    }
    return f;
}

/// @brief Looks a file up by path without adding it.
/// @return The file, or nullptr if there is none with that path.
file* library::find_file(const std::string &path) const
{
    auto it = files_by_path.find(path);
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

/// @brief Records the file system identity of every file that is at its path and has none yet or a
/// different one: files added without a database, stored before the database kept identities, or replaced
/// by a program that saves to a temporary file and renames it over the original (which gives it a new identity).
/// Only files with an up to date identity can be found again after they move. Checks the disk once per file.
/// Wrap it in a transaction to save it all at once.
/// @return How many identities were recorded.
int library::record_identities()
{
    int count = 0;
    for(file* f : lib_files)
    {
        std::string id = file_identity(f->get_path());
        if(id.empty() || id == f->get_identity())
        {
            continue;
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

/// @brief Looks for missing files in a folder and everything under it, by their file system identity.
/// Changes nothing: pass what it finds to relink. A path already in the library isn't offered.
/// An identity only survives a move within the same volume, so look on the drive the files were on:
/// a folder on a volume none of the missing files were on isn't searched at all.
/// @param folder Folder to search, in UTF-8.
/// @param error If given, set to why the search stopped early (the folder can't be read, say), or cleared.
/// The files found before that are still returned.
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
            volumes.insert(id.substr(0, id.find(':')));
        }
    }

    std::vector<moved_file> out;
    if(missing.empty())
    {
        return out;
    }
    // An identity is "volume:file", so the folder's own tells which volume it is on. If it can't be read,
    // search anyway and let the scan report why.
    std::string folder_id = file_identity(folder);
    if(!folder_id.empty() && !volumes.count(folder_id.substr(0, folder_id.find(':'))))
    {
        return out;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::recursive_directory_iterator it(fs::u8path(folder), fs::directory_options::skip_permission_denied, ec);
    // Folders are checked too: one dropped into the library is a file of it.
    for(; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
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
            if(missing.empty())
            {
                break;
            }
        }
    }
    if(error)
    {
        *error = ec;
    }
    return out;
}

/// @brief Points a file of the library to a new path (the file on disk is untouched), keeping its tags.
/// With a database attached, the row changes first, and nothing changes in memory if that fails.
/// @param f File to be relinked.
/// @param new_path Where the file is now. No other file of the library may have it.
/// @return A boolean describing whether the file was relinked.
bool library::relink(file* f, const std::string &new_path)
{
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

SearchResult library::search(const std::string &query) const
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
            return { {}, false };   // '{' without matching '}'

        std::unique_ptr<QueryNode> ast = ParseQuery(query.substr(open + 1, close - open - 1));
        if (!ast)
            return { {}, false };   // malformed expression inside {}
        groups.push_back(std::move(ast));
        pos = close + 1;
    }

    if (plainText.empty() && groups.empty())
        return { seen_files, true };

    // Every group must match, as well as the text.
    std::vector<file*> results;
    for (file* f : seen_files)
    {
        if (!plainText.empty() && !MatchesSubstring(f, plainText))
            continue;
        std::vector<tag*> fileTags = f->get_tags();
        bool matchesGroups = true;
        for (const std::unique_ptr<QueryNode> &ast : groups)
        {
            if (!ast->Evaluate(fileTags))
            {
                matchesGroups = false;
                break;
            }
        }
        if (matchesGroups)
            results.push_back(f);
    }
    return { results, true };
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
}

library::~library()
{
    clear();
}