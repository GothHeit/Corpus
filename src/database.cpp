#include <string>
#include <unordered_map>

#include "../include/database.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"
#include "../third_party/sqlite/sqlite3.h"

static const char *schema_sql =
    "CREATE TABLE files("
    "    id INTEGER PRIMARY KEY,"
    "    path TEXT NOT NULL UNIQUE"
    ");"
    "CREATE TABLE tags("
    "    id INTEGER PRIMARY KEY,"
    "    name TEXT NOT NULL UNIQUE"
    ");"
    "CREATE TABLE file_tags("
    "    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
    "    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,"
    "    PRIMARY KEY(file_id, tag_id)"
    ") WITHOUT ROWID;"
    "CREATE INDEX file_tags_tag ON file_tags(tag_id);"
    "PRAGMA user_version = 1;";

/// @brief Records the current SQLite error message.
/// @return Always false, so callers can write "return fail();".
bool database::fail()
{
    error = handle ? sqlite3_errmsg(handle) : "database is not open";
    return false;
}

/// @brief Runs one or more SQL statements that return no rows.
bool database::exec(const char *sql)
{
    if(!handle)
        return fail();
    if(sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        return fail();
    return true;
}

/// @brief Gets a cached prepared statement, preparing it on first use.
/// @return The statement, reset and with no bindings, or nullptr on error.
sqlite3_stmt* database::prepare(statement s, const char *sql)
{
    if(!handle)
    {
        fail();
        return nullptr;
    }
    if(!statements[s])
    {
        if(sqlite3_prepare_v3(handle, sql, -1, SQLITE_PREPARE_PERSISTENT, &statements[s], nullptr) != SQLITE_OK)
        {
            fail();
            return nullptr;
        }
    }
    return statements[s];
}

/// @brief Reads the schema version stored in the file.
bool database::read_version()
{
    sqlite3_stmt *st = nullptr;
    if(sqlite3_prepare_v2(handle, "PRAGMA user_version;", -1, &st, nullptr) != SQLITE_OK)
    {
        return fail();
    }
    version = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
    sqlite3_finalize(st);
    return true;
}

/// @brief Opens (or creates) the database file. Doesn't create the tables, see is_new() and create_schema().
/// @param path Path to the database file.
/// @return A boolean describing whether the database could be opened.
bool database::open(const std::filesystem::path &path)
{
    // SQLite takes UTF-8 on every platform, so accented paths also work on Windows.
    return open(path.u8string());
}

/// @brief Same as open(const std::filesystem::path&), for a path already in UTF-8.
bool database::open(const std::string &utf8_path)
{
    close();
    if(sqlite3_open_v2(utf8_path.c_str(), &handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
    {
        fail();
        close();
        return false;
    }
    sqlite3_busy_timeout(handle, 2000);

    if(!exec("PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;"))
    {
        close();
        return false;
    }

    if(!read_version())
    {
        close();
        return false;
    }
    if(version > schema_version)
    {
        error = "database was made by a newer version of Corpus";
        close();
        return false;
    }
    return true;
}

void database::close()
{
    for(sqlite3_stmt *&st : statements)
    {
        sqlite3_finalize(st);
        st = nullptr;
    }
    sqlite3_close(handle);
    handle = nullptr;
    version = 0;
}

bool database::is_open() const
{
    return handle != nullptr;
}

/// @brief Tells whether the tables still have to be created (the file is new or was never set up).
bool database::is_new() const
{
    return version == 0;
}

/// @brief Creates the tables and stamps the schema version.
/// Run it inside a transaction together with the first import, so a crash halfway leaves the database new.
bool database::create_schema()
{
    if(!exec(schema_sql))
        return false;
    version = schema_version;
    return true;
}

bool database::begin()
{
    return exec("BEGIN IMMEDIATE;");
}

bool database::commit()
{
    if(exec("COMMIT;"))
        return true;
    // Some failures (a full disk, an I/O error) make SQLite roll the transaction back on its own,
    // undoing a create_schema() in it. Re-read the version so the database reports it is new again.
    if(handle && sqlite3_get_autocommit(handle))
    {
        std::string commit_error = error;
        read_version();
        error = commit_error;
    }
    return false;
}

void database::rollback()
{
    if(handle && !sqlite3_get_autocommit(handle))
    {
        sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
        // A rolled back create_schema() leaves the database new again.
        read_version();
    }
}

/// @brief Stores a file path, or finds it if it is already stored.
/// @return The row id of the file, or 0 on error.
std::int64_t database::insert_file(const std::string &path)
{
    sqlite3_stmt *st = prepare(insert_file_stmt,
        "INSERT INTO files(path) VALUES(?1) ON CONFLICT(path) DO UPDATE SET path = excluded.path RETURNING id;");
    if(!st)
        return 0;
    sqlite3_bind_text(st, 1, path.data(), static_cast<int>(path.size()), SQLITE_STATIC);
    std::int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    if(!id)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return id;
}

/// @brief Stores a tag name, or finds it if it is already stored.
/// @return The row id of the tag, or 0 on error.
std::int64_t database::insert_tag(const std::string &name)
{
    sqlite3_stmt *st = prepare(insert_tag_stmt,
        "INSERT INTO tags(name) VALUES(?1) ON CONFLICT(name) DO UPDATE SET name = excluded.name RETURNING id;");
    if(!st)
        return 0;
    sqlite3_bind_text(st, 1, name.data(), static_cast<int>(name.size()), SQLITE_STATIC);
    std::int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    if(!id)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return id;
}

bool database::rename_tag(std::int64_t id, const std::string &name)
{
    sqlite3_stmt *st = prepare(rename_tag_stmt, "UPDATE tags SET name = ?2 WHERE id = ?1;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_text(st, 2, name.data(), static_cast<int>(name.size()), SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return ok;
}

/// @brief Records that a file has a tag.
bool database::link(std::int64_t file_id, std::int64_t tag_id)
{
    sqlite3_stmt *st = prepare(link_stmt, "INSERT OR IGNORE INTO file_tags(file_id, tag_id) VALUES(?1, ?2);");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, file_id);
    sqlite3_bind_int64(st, 2, tag_id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    return ok;
}

/// @brief Records that a file no longer has a tag.
bool database::unlink(std::int64_t file_id, std::int64_t tag_id)
{
    sqlite3_stmt *st = prepare(unlink_stmt, "DELETE FROM file_tags WHERE file_id = ?1 AND tag_id = ?2;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, file_id);
    sqlite3_bind_int64(st, 2, tag_id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    return ok;
}

/// @brief Deletes a file row. Its links go with it (ON DELETE CASCADE).
bool database::delete_file(std::int64_t id)
{
    sqlite3_stmt *st = prepare(delete_file_stmt, "DELETE FROM files WHERE id = ?1;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    return ok;
}

/// @brief Deletes a tag row. Its links go with it (ON DELETE CASCADE).
bool database::delete_tag(std::int64_t id)
{
    sqlite3_stmt *st = prepare(delete_tag_stmt, "DELETE FROM tags WHERE id = ?1;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    return ok;
}

/// @brief Reads every file, tag and link into the library. The library should have no database attached yet,
/// or every row would be written back.
/// @return A boolean describing whether everything could be read.
bool database::load(library &lib)
{
    if(!exec("BEGIN;"))
        return false;

    std::unordered_map<std::int64_t, file*> files;
    std::unordered_map<std::int64_t, tag*> tags;
    sqlite3_stmt *st = nullptr;
    int rc = SQLITE_DONE;

    if(sqlite3_prepare_v2(handle, "SELECT id, path FROM files ORDER BY id;", -1, &st, nullptr) != SQLITE_OK)
    {
        fail();
        rollback();
        return false;
    }
    while((rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        std::int64_t id = sqlite3_column_int64(st, 0);
        const char *path = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        file *f = lib.add_file(std::string(path, sqlite3_column_bytes(st, 1)));
        f->set_db_id(id);
        files[id] = f;
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE)
    {
        fail();
        rollback();
        return false;
    }

    if(sqlite3_prepare_v2(handle, "SELECT id, name FROM tags ORDER BY id;", -1, &st, nullptr) != SQLITE_OK)
    {
        fail();
        rollback();
        return false;
    }
    while((rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        std::int64_t id = sqlite3_column_int64(st, 0);
        const char *name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        tag *t = lib.retrieve_tag(std::string(name, sqlite3_column_bytes(st, 1)));
        t->set_db_id(id);
        tags[id] = t;
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE)
    {
        fail();
        rollback();
        return false;
    }

    if(sqlite3_prepare_v2(handle, "SELECT file_id, tag_id FROM file_tags;", -1, &st, nullptr) != SQLITE_OK)
    {
        fail();
        rollback();
        return false;
    }
    while((rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        auto f = files.find(sqlite3_column_int64(st, 0));
        auto t = tags.find(sqlite3_column_int64(st, 1));
        if(f != files.end() && t != tags.end())
            lib.edit_file(f->second, t->second, 1);
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE)
    {
        fail();
        rollback();
        return false;
    }

    return commit();
}

const std::string& database::last_error() const
{
    return error;
}

void database::set_error(const std::string &message)
{
    error = message;
}

void database::clear_error()
{
    error.clear();
}

database::~database()
{
    close();
}
