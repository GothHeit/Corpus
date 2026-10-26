#ifndef LIBRARY_HPP
#define LIBRARY_HPP

#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "tag_query.hpp"

class tag;
class file;
struct reading_progress;
class database;
struct scanned_file;

/// @brief A file of the library that is missing from its path, and where the same file is now.
struct moved_file
{
    file* f;
    std::string new_path;
};

/// @brief What library::rename_tag did.
enum class rename_result
{
    Renamed,    // the tag has the new name
    Merged,     // another tag already had that name (case aside): the tag's files moved to it and the tag is gone
    Unchanged,  // the tag already had exactly that name
    Invalid,    // the name isn't a valid tag name (see is_valid_tag_name), or the tag isn't in this library
    Failed      // the database refused; nothing changed
};

/// @brief The sidebar's Library views: which files a search looks in.
enum class LibraryView
{
    All,        // every file
    Untagged,   // files with no tag
    Missing     // files no longer at their path (see file_missing)
};

class library
{
    std::vector<file*> lib_files;
    std::vector<tag*> tags;
    std::unordered_map<std::string, file*> files_by_path;
    std::unordered_map<std::string, tag*> tags_by_name; // by folded name (see fold_case): one tag per name, case aside
    database* db = nullptr;

    bool owns(const file* f) const;

    public:

    void attach(database* d);

    database* attached() const;

    void clear();

    file* add_file(const std::string &path);

    int add_files(const std::vector<scanned_file> &files);

    file* find_file(const std::string &path) const;

    bool edit_file(file* f, tag* t, const bool add);

    int edit_files(const std::vector<file*> &files, const std::vector<tag*> &tags, const bool add);

    bool remove_file(file* f);

    int remove_files(const std::vector<file*> &files);

    std::vector<file*> missing_files() const;

    int record_identities();

    std::vector<moved_file> find_moved(const std::string &folder, std::error_code *error = nullptr) const;

    bool relink(file* f, const std::string &new_path);

    bool set_progress(file* f, reading_progress progress);

    bool clear_progress(file* f);

    std::vector<file*> recently_read(std::size_t limit = 0) const;

    rename_result rename_tag(tag* t, const std::string &name, tag** result = nullptr);

    tag* create_tag(const std::string &name);

    bool delete_tag(tag* t);

    tag* retrieve_tag(const std::string &name);

    tag* find_tag(const std::string &name) const;

    SearchResult search(const std::string &query, LibraryView view = LibraryView::All) const;

    const std::vector<file*>& get_files() const;

    const std::vector<tag*>& get_tags() const;

    ~library();
};

#endif