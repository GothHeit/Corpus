#ifndef LIBRARY_HPP
#define LIBRARY_HPP

#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "file.hpp"
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

/// @brief What a tag operation changed, by names and paths (not pointers) so it survives a reload. Every tag operation
/// of library can fill one; apply_tag_change takes it back (undo) or makes it again (redo). It is the net effect: a
/// link added and taken off again within one operation is in neither list. The GUI keeps it for Ctrl+Z instead of
/// working out what the core is about to do.
struct tag_change
{
    using link = std::pair<std::string, std::string>;  // tag name, file path
    std::vector<link> added;            // links the operation made
    std::vector<link> removed;          // links it took away (including those of a tag it deleted)
    std::vector<std::string> created;   // tags it made
    std::vector<std::string> deleted;   // tags it deleted

    bool empty() const
    {
        return added.empty() && removed.empty() && created.empty() && deleted.empty();
    }
};

/// @brief A file as the library had it, to put it back after it was removed (see library::snapshot_files and
/// library::restore_files).
struct file_snapshot
{
    std::string path;
    std::string identity;
    std::int64_t db_id = 0;
    std::vector<std::string> tags;  // the names of its tags
    bool has_progress = false;
    reading_progress progress;
};

/// @brief What library::sync_watched did.
struct watch_sync
{
    std::vector<file*> added;     // files new to the library
    std::vector<file*> relinked;  // missing files found at a new path by their identity: moved there
    std::vector<std::string> relinked_from;  // the path each of them had
    bool failed = false;          // the database refused, and nothing changed
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
    std::vector<std::string> watched;                     // sorted
    std::unordered_map<std::string, std::string> ignored; // path -> identity of a file removed from a watched folder

    bool owns(const file* f) const;

    // The record of the operation running (see tag_change), if its caller asked for one.
    struct change_log;
    class recording;
    change_log* log = nullptr;
    void note_link(const file* f, const tag* t, bool add);
    void note_tag(const tag* t, bool created);
    bool link_now(file* f, tag* t, bool add);

    public:

    void attach(database* d);

    database* attached() const;

    void clear();

    file* add_file(const std::string &path);

    int add_files(const std::vector<scanned_file> &files);

    file* find_file(const std::string &path) const;

    bool edit_file(file* f, tag* t, const bool add);

    int edit_files(const std::vector<file*> &files, const std::vector<tag*> &tags, const bool add, tag_change *change = nullptr);

    int tag_files(const std::vector<file*> &files, const std::vector<std::string> &names, tag_change *change = nullptr);

    int remove_level(const std::vector<file*> &files, const std::string &level, std::vector<std::string> *joined = nullptr,
        tag_change *change = nullptr);

    int apply_tag_change(const tag_change &change, bool undo);

    bool remove_file(file* f);

    int remove_files(const std::vector<file*> &files);

    std::vector<file_snapshot> snapshot_files(const std::vector<file*> &files) const;

    int restore_files(const std::vector<file_snapshot> &files);

    std::vector<file*> missing_files() const;

    int record_identities();

    std::vector<moved_file> find_moved(const std::string &folder, std::error_code *error = nullptr) const;

    bool relink(file* f, const std::string &new_path);

    bool set_progress(file* f, reading_progress progress);

    bool clear_progress(file* f);

    std::vector<file*> recently_read(std::size_t limit = 0) const;

    bool watch_folder(const std::string &folder);

    bool unwatch_folder(const std::string &folder);

    const std::vector<std::string>& watched_folders() const;

    bool is_watched(const std::string &path) const;

    bool ignore_file(const std::string &path, const std::string &identity);

    watch_sync sync_watched(const std::vector<scanned_file> &found);

    rename_result rename_tag(tag* t, const std::string &name, tag** result = nullptr, tag_change *change = nullptr);

    int rename_branch(const std::string &branch, const std::string &name, tag_change *change = nullptr);

    tag* create_tag(const std::string &name);

    bool delete_tag(tag* t, tag_change *change = nullptr);

    /// One tag a promotion renamed: its old and new name.
    using tag_move = std::pair<std::string, std::string>;

    int delete_tag_keep_children(const std::string &branch, std::vector<tag_move> *moves = nullptr,
        tag_change *change = nullptr);

    tag* retrieve_tag(const std::string &name);

    tag* find_tag(const std::string &name) const;

    SearchResult search(const std::string &query, LibraryView view = LibraryView::All) const;

    const std::vector<file*>& get_files() const;

    const std::vector<tag*>& get_tags() const;

    ~library();
};

#endif