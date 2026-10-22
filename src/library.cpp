#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <cctype>

#include "../include/database.hpp"
#include "../include/file.hpp"
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

/// @brief Adds a file to the library.
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
        f->set_db_id(db->insert_file(path));
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
    size_t open = query.find('{');

    if (open == std::string::npos)
    {
        if (query.empty())
            return { seen_files, true };

        std::vector<file*> results;
        for (file* f : seen_files)
        {
            if (MatchesSubstring(f, query))
                results.push_back(f);
        }
        return { results, true };
    }

    size_t close = query.find('}', open);
    if (close == std::string::npos)
        return { {}, false };   // '{' without matching '}'

    // The spaces around the braces only separate the text from them: "cat {pet}" searches for "cat".
    std::string plainText = TrimSpaces(query.substr(0, open) + query.substr(close + 1));
    std::string braceContent = query.substr(open + 1, close - open - 1);

    std::unique_ptr<QueryNode> ast = ParseQuery(braceContent);
    if (!ast)
        return { {}, false };   // malformed expression inside {}

    std::vector<file*> results;
    for (file* f : seen_files)
    {
        bool matchesText = plainText.empty() || MatchesSubstring(f, plainText);
        if (matchesText && ast->Evaluate(f->get_tags()))
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