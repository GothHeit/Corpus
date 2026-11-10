#ifndef LIBRARY_HPP
#define LIBRARY_HPP

#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "board.hpp"
#include "file.hpp"
#include "tag.hpp"
#include "tag_query.hpp"

class tag;
class file;
struct reading_progress;
struct audio_tags;
class database;
class content_index;
struct content_job;
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
    // The note and fields (see tag_attrs) of the tags above that had any: deleted_attrs as they were when the tag was
    // deleted, created_attrs as they are on the tag made (a rename is a delete and a create). Undo puts back the
    // first, redo the second.
    std::vector<std::pair<std::string, tag_attrs>> deleted_attrs, created_attrs;
    // A merge folds the deleted tag's fields into the surviving tag: its fields before (undo) and after (redo).
    std::vector<std::pair<std::string, tag_attrs>> survivor_before, survivor_after;

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
    bool has_audio = false;
    audio_tags audio;
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
    content_index* content = nullptr;
    std::vector<board*> boards;
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

    /// Stores what the file's own tags say (read by audio_read_tags; the library never touches the file). Saved with
    /// the catalog; goes with the file on removal. True if the file is ours and the database accepted it.
    bool set_audio_tags(file* f, const audio_tags &tags);

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

    /// Sets a tag's note and fields (all of them: pass the old ones with one changed). Empty ones clear it. False
    /// if the tag isn't in this library or the database refused (nothing changed). A rename keeps them, a merge
    /// into another tag folds them into it (merge_tag_attrs), deleting the tag drops them.
    bool set_tag_attrs(tag* t, const tag_attrs &attrs);

    bool delete_tag(tag* t, tag_change *change = nullptr);

    /// One tag a promotion renamed: its old and new name.
    using tag_move = std::pair<std::string, std::string>;

    int delete_tag_keep_children(const std::string &branch, std::vector<tag_move> *moves = nullptr,
        tag_change *change = nullptr);

    tag* retrieve_tag(const std::string &name);

    tag* find_tag(const std::string &name) const;

    /// Searches by name and tag, and with in_content also in the text of the documents the content index knows (see
    /// attach_content): each plain word may then be found in the path, a tag name, or the text. Without an attached
    /// index in_content does nothing.
    SearchResult search(const std::string &query, LibraryView view = LibraryView::All, bool in_content = false) const;

    /// Lets search look in the text of documents. Not owned; detach (nullptr) or keep it open as long as the library
    /// searches. The index must be of the same database the library is attached to.
    void attach_content(content_index* index);

    /// The files whose type has a text extractor (EPUB for now) and a database row, for content_indexer::enqueue.
    /// Files missing on disk are skipped by the indexer, not here.
    std::vector<content_job> content_jobs() const;

    /// Boards (see board.hpp). Saved with the catalog; every call writes the database first and changes memory only
    /// if that worked. A board's file items go when the file is removed from the library.
    const std::vector<board*>& get_boards() const;

    board* find_board(std::int64_t db_id) const;

    /// Makes a board with this name and query (the search box's text); nullptr if the database refused.
    board* add_board(const std::string &name, const std::string &query);

    bool rename_board(board* b, const std::string &name);

    bool set_board_query(board* b, const std::string &query);

    /// Deletes the board and where it put its files; the pointer is dead afterwards. The files stay.
    bool delete_board(board* b);

    /// Places files on the board, or moves ones already there, in one atomic group.
    bool place_on_board(board* b, const std::vector<board_item> &items);

    /// Takes files (by database id) off the board, back to the tray. Ids that were not placed are ignored.
    bool unplace_from_board(board* b, const std::vector<std::int64_t> &file_ids);

    /// For database::load only: fills memory without writing.
    void load_board(std::int64_t db_id, const std::string &name, const std::string &query);
    void load_board_item(std::int64_t board_id, const board_item &item);

    const std::vector<file*>& get_files() const;

    const std::vector<tag*>& get_tags() const;

    ~library();
};

#endif