#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;
struct reading_progress;

class file;
class library;

/// A SQLite database holding a library: files, tags, links, reading progress and watched folders.
class database
{
    enum statement
    {
        insert_file_stmt,
        restore_file_stmt,
        set_identity_stmt,
        move_file_stmt,
        insert_tag_stmt,
        rename_tag_stmt,
        link_stmt,
        unlink_stmt,
        delete_file_stmt,
        delete_tag_stmt,
        set_progress_stmt,
        clear_progress_stmt,
        watch_folder_stmt,
        unwatch_folder_stmt,
        ignore_file_stmt,
        unignore_file_stmt,
        statement_count
    };

    sqlite3* handle = nullptr;
    sqlite3_stmt* statements[statement_count] = {};
    int version = 0;
    std::string error;

    sqlite3_stmt* prepare(statement s, const char *sql);
    bool exec(const char *sql);
    bool read_version();
    bool fail();
    bool migrate();
    bool merge_rows(const std::unordered_map<std::int64_t, file*> &files, const std::vector<std::int64_t> &extra_rows,
        const std::vector<file*> &renamed);
    bool merge_tag_rows(const std::vector<std::pair<std::int64_t, std::int64_t>> &extra_rows);

    public:

    static const int schema_version = 3;

    database() = default;
    database(const database&) = delete;
    database& operator=(const database&) = delete;

    bool open(const std::filesystem::path &path);
    bool open(const std::string &utf8_path);
    bool open(const char *utf8_path);
    void close();
    bool is_open() const;

    bool is_new() const;
    bool create_schema();

    bool begin();
    bool commit();
    void rollback();

    // A group of writes that succeeds or fails as a whole, inside or outside begin/commit (outside, release
    // commits it). undo_savepoint takes back everything since savepoint and ends it.
    bool savepoint();
    bool release_savepoint();
    void undo_savepoint();

    std::int64_t insert_file(const std::string &path, const std::string &identity = "");
    std::int64_t restore_file(std::int64_t id, const std::string &path, const std::string &identity);
    bool set_identity(std::int64_t id, const std::string &identity);
    bool move_file(std::int64_t id, const std::string &path, const std::string &identity);
    std::int64_t insert_tag(const std::string &name);
    bool rename_tag(std::int64_t id, const std::string &name);
    bool link(std::int64_t file_id, std::int64_t tag_id);
    bool unlink(std::int64_t file_id, std::int64_t tag_id);
    bool delete_file(std::int64_t id);
    bool delete_tag(std::int64_t id);
    bool set_progress(std::int64_t file_id, const reading_progress &p);
    bool clear_progress(std::int64_t file_id);
    bool watch_folder(const std::string &path);
    bool unwatch_folder(const std::string &path);
    bool ignore_file(const std::string &path, const std::string &identity);
    bool unignore_file(const std::string &path);

    bool load(library &lib);

    // Whether the database holds any file or tag.
    bool has_content();

    // A consistent copy in a new file (SQLite online backup: safe while in use, plain single file, not WAL).
    // Call outside begin/commit. Replaces an existing file.
    bool backup_to(const std::filesystem::path &dest);

    // Whether a file is a database this build can open: quick_check passes and it has the Corpus tables at a
    // schema version up to schema_version. Opened read-only; on false, error says why.
    static bool verify_file(const std::filesystem::path &file, std::string &error);

    const std::string& last_error() const;
    void set_error(const std::string &message);
    void clear_error();

    ~database();
};

#endif
