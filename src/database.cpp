#include <string>
#include <unordered_map>
#include <vector>

#include "../include/database.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"
#include "../third_party/sqlite/sqlite3.h"

static const char *schema_sql =
    "CREATE TABLE files("
    "    id INTEGER PRIMARY KEY,"
    "    path TEXT NOT NULL UNIQUE,"
    "    identity TEXT"
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
    "CREATE INDEX file_tags_tag ON file_tags(tag_id);";

// Added in version 3. A file's progress goes with its row: it follows a relink and goes when the file is removed.
static const char *progress_sql =
    "CREATE TABLE reading_progress("
    "    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,"
    "    position INTEGER NOT NULL,"
    "    length INTEGER NOT NULL,"
    "    location TEXT NOT NULL,"
    "    last_read INTEGER NOT NULL"
    ");";

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

/// @brief Brings a database made by an older version of Corpus up to the current schema, in one transaction:
/// if anything fails, the file is left as it was.
bool database::migrate()
{
    if(!begin())
        return false;
    // Another program may have upgraded it while this one waited for the lock.
    if(!read_version())
    {
        rollback();
        return false;
    }
    // Version 2: the file system identity of each file, to find it again after it is moved or renamed.
    // Files stored before have none until it is recorded (see library::record_identities).
    if(version == 1)
    {
        if(!exec("ALTER TABLE files ADD COLUMN identity TEXT; PRAGMA user_version = 2;"))
        {
            rollback();
            return false;
        }
        version = 2;
    }
    // Version 3: how far each file was read.
    if(version == 2)
    {
        if(!exec(progress_sql) || !exec("PRAGMA user_version = 3;"))
        {
            rollback();
            return false;
        }
        version = 3;
    }
    if(!commit())
    {
        rollback();
        return false;
    }
    return read_version();
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
    if(version != 0 && version < schema_version && !migrate())
    {
        close();
        return false;
    }
    return true;
}

/// @brief Same as open(const std::string&). Without it, open("corpus.db") would be ambiguous between
/// the path and string overloads.
bool database::open(const char *utf8_path)
{
    return open(std::string(utf8_path));
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
    std::string stamp = "PRAGMA user_version = " + std::to_string(schema_version) + ";";
    if(!exec(schema_sql) || !exec(progress_sql) || !exec(stamp.c_str()))
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

bool database::savepoint()
{
    return exec("SAVEPOINT corpus_batch;");
}

bool database::release_savepoint()
{
    if(exec("RELEASE corpus_batch;"))
        return true;
    undo_savepoint();
    return false;
}

void database::undo_savepoint()
{
    if(handle && !sqlite3_get_autocommit(handle))
    {
        std::string keep = error;
        sqlite3_exec(handle, "ROLLBACK TO corpus_batch; RELEASE corpus_batch;", nullptr, nullptr, nullptr);
        error = keep;
    }
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

/// @brief Binds a file identity, or NULL for an empty one.
static void bind_identity(sqlite3_stmt *st, int index, const std::string &identity)
{
    if(identity.empty())
        sqlite3_bind_null(st, index);
    else
        sqlite3_bind_text(st, index, identity.data(), static_cast<int>(identity.size()), SQLITE_STATIC);
}

/// @brief Stores a file path, or finds it if it is already stored.
/// @param identity The file's file system identity (see file_identity), or "" if unknown. An empty one
/// doesn't replace the identity of a file that is already stored.
/// @return The row id of the file, or 0 on error.
std::int64_t database::insert_file(const std::string &path, const std::string &identity)
{
    sqlite3_stmt *st = prepare(insert_file_stmt,
        "INSERT INTO files(path, identity) VALUES(?1, ?2) ON CONFLICT(path) DO UPDATE SET path = excluded.path,"
        " identity = COALESCE(excluded.identity, identity) RETURNING id;");
    if(!st)
        return 0;
    sqlite3_bind_text(st, 1, path.data(), static_cast<int>(path.size()), SQLITE_STATIC);
    bind_identity(st, 2, identity);
    std::int64_t id = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : 0;
    if(!id)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return id;
}

/// @brief Stores a file again under the row id it had before it was deleted, so it keeps its place among the
/// files (the GUI lists them in the order they were added).
/// @return The row id: 'id' if it is still free, else a new one as insert_file gives; 0 on error.
std::int64_t database::restore_file(std::int64_t id, const std::string &path, const std::string &identity)
{
    if(id <= 0)
        return insert_file(path, identity);
    sqlite3_stmt *st = prepare(restore_file_stmt, "INSERT OR IGNORE INTO files(id, path, identity) VALUES(?1, ?2, ?3);");
    if(!st)
        return 0;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_text(st, 2, path.data(), static_cast<int>(path.size()), SQLITE_STATIC);
    bind_identity(st, 3, identity);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    bool inserted = ok && sqlite3_changes(handle) == 1;
    if(!ok)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    if(!ok)
        return 0;
    return inserted ? id : insert_file(path, identity);
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

/// @brief Records the file system identity of a stored file ("" forgets it).
bool database::set_identity(std::int64_t id, const std::string &identity)
{
    sqlite3_stmt *st = prepare(set_identity_stmt, "UPDATE files SET identity = ?2 WHERE id = ?1;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bind_identity(st, 2, identity);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return ok;
}

/// @brief Gives a stored file a new path and identity, keeping its row and so its tags.
/// Fails if another file already has that path.
bool database::move_file(std::int64_t id, const std::string &path, const std::string &identity)
{
    sqlite3_stmt *st = prepare(move_file_stmt, "UPDATE files SET path = ?2, identity = ?3 WHERE id = ?1;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_text(st, 2, path.data(), static_cast<int>(path.size()), SQLITE_STATIC);
    bind_identity(st, 3, identity);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return ok;
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

/// @brief Records how far a stored file was read, replacing what was recorded before.
bool database::set_progress(std::int64_t file_id, const reading_progress &p)
{
    sqlite3_stmt *st = prepare(set_progress_stmt,
        "INSERT OR REPLACE INTO reading_progress(file_id, position, length, location, last_read) VALUES(?1, ?2, ?3, ?4, ?5);");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, file_id);
    sqlite3_bind_int64(st, 2, p.position);
    sqlite3_bind_int64(st, 3, p.length);
    sqlite3_bind_text(st, 4, p.location.data(), static_cast<int>(p.location.size()), SQLITE_STATIC);
    sqlite3_bind_int64(st, 5, p.last_read);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    return ok;
}

/// @brief Forgets how far a stored file was read.
bool database::clear_progress(std::int64_t file_id)
{
    sqlite3_stmt *st = prepare(clear_progress_stmt, "DELETE FROM reading_progress WHERE file_id = ?1;");
    if(!st)
        return false;
    sqlite3_bind_int64(st, 1, file_id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    if(!ok)
        fail();
    sqlite3_reset(st);
    return ok;
}

/// @brief Merges each extra tag row into the kept one: its links move there (a file both rows tag stays tagged
/// once) and the row is deleted.
/// @param extra_rows Pairs of the kept row and a row to merge into it.
bool database::merge_tag_rows(const std::vector<std::pair<std::int64_t, std::int64_t>> &extra_rows)
{
    if(extra_rows.empty())
        return true;
    sqlite3_stmt *st = nullptr;
    if(sqlite3_prepare_v2(handle, "UPDATE OR IGNORE file_tags SET tag_id = ?1 WHERE tag_id = ?2;", -1, &st,
        nullptr) != SQLITE_OK)
    {
        fail();
        return false;
    }
    for(const auto &row : extra_rows)
    {
        sqlite3_bind_int64(st, 1, row.first);
        sqlite3_bind_int64(st, 2, row.second);
        if(sqlite3_step(st) != SQLITE_DONE)
        {
            fail();
            sqlite3_finalize(st);
            return false;
        }
        sqlite3_reset(st);
        // The links left are ones the kept row already has: they go with the row (ON DELETE CASCADE).
        if(!delete_tag(row.second))
        {
            sqlite3_finalize(st);
            return false;
        }
    }
    sqlite3_finalize(st);
    return true;
}

/// @brief Merges each extra row into the row of the file it belongs to: its tag links move there (the union is
/// kept, a link both rows have stays once), so does its reading progress if it is the more recent one, and the
/// row is deleted. Then the kept rows get the path and identity
/// the library has for them.
/// @param files Every row id read so far and its file, whose db_id is the row that stays.
bool database::merge_rows(const std::unordered_map<std::int64_t, file*> &files,
    const std::vector<std::int64_t> &extra_rows, const std::vector<file*> &renamed)
{
    if(extra_rows.empty() && renamed.empty())
        return true;
    // Each statement takes the kept row as ?1 and the extra row as ?2. Of the two rows' progress, the older one
    // is deleted, then what is left moves unless the kept row has its own (both read at the same time).
    static const char *move_sql[] = {
        "UPDATE OR IGNORE file_tags SET file_id = ?1 WHERE file_id = ?2;",
        "DELETE FROM reading_progress WHERE file_id IN (?1, ?2) AND last_read <"
        " (SELECT MAX(last_read) FROM reading_progress WHERE file_id IN (?1, ?2));",
        "UPDATE OR IGNORE reading_progress SET file_id = ?1 WHERE file_id = ?2;",
    };
    const std::size_t move_count = sizeof(move_sql) / sizeof(move_sql[0]);
    sqlite3_stmt *moves[move_count] = {};
    auto finalize = [&moves]()
    {
        for(sqlite3_stmt *st : moves)
            sqlite3_finalize(st);
    };
    for(std::size_t i = 0; i < move_count; i++)
    {
        if(sqlite3_prepare_v2(handle, move_sql[i], -1, &moves[i], nullptr) != SQLITE_OK)
        {
            fail();
            finalize();
            return false;
        }
    }
    for(std::int64_t id : extra_rows)
    {
        for(sqlite3_stmt *st : moves)
        {
            sqlite3_bind_int64(st, 1, files.at(id)->get_db_id());
            sqlite3_bind_int64(st, 2, id);
            if(sqlite3_step(st) != SQLITE_DONE)
            {
                fail();
                finalize();
                return false;
            }
            sqlite3_reset(st);
        }
        // The links left are ones the kept row already has, and the progress left is the older one:
        // they go with the row (ON DELETE CASCADE).
        if(!delete_file(id))
        {
            finalize();
            return false;
        }
    }
    finalize();
    for(file *f : renamed)
    {
        if(!move_file(f->get_db_id(), f->get_path(), f->get_identity()))
            return false;
    }
    return true;
}

/// @brief Reads every file, tag, link and reading progress into the library. The library should have no database attached yet,
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

    if(sqlite3_prepare_v2(handle, "SELECT id, path, identity FROM files ORDER BY id;", -1, &st, nullptr) != SQLITE_OK)
    {
        fail();
        rollback();
        return false;
    }
    // Rows the library writes another way: older builds stored "c:\x" and "C:\x" as two files, which the
    // library now keeps as one (see library::add_file). They are fixed once all rows are read.
    std::vector<std::int64_t> extra_rows;
    std::vector<file*> renamed;
    while((rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        std::int64_t id = sqlite3_column_int64(st, 0);
        const char *path = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        std::string stored(path, sqlite3_column_bytes(st, 1));
        const char *identity = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        file *f = lib.add_file(stored);
        files[id] = f;
        if(f->get_db_id())
        {
            // The same file again: the first row (the oldest) stays, this one is merged into it.
            extra_rows.push_back(id);
            if(identity && f->get_identity().empty())
                f->set_identity(std::string(identity, sqlite3_column_bytes(st, 2)));
            if(renamed.empty() || renamed.back() != f)
                renamed.push_back(f);
            continue;
        }
        f->set_db_id(id);
        if(identity)
            f->set_identity(std::string(identity, sqlite3_column_bytes(st, 2)));
        if(f->get_path() != stored)
            renamed.push_back(f);
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE)
    {
        fail();
        rollback();
        return false;
    }
    if(!merge_rows(files, extra_rows, renamed))
    {
        rollback();
        return false;
    }

    if(sqlite3_prepare_v2(handle, "SELECT id, name FROM tags ORDER BY id;", -1, &st, nullptr) != SQLITE_OK)
    {
        fail();
        rollback();
        return false;
    }
    // Tags whose names differ only in case ("Beach" and "beach", from older builds) are one tag to the library:
    // the oldest row keeps its spelling, and the others are merged into it once all rows are read.
    std::vector<std::pair<std::int64_t, std::int64_t>> extra_tags; // kept row, extra row
    while((rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        std::int64_t id = sqlite3_column_int64(st, 0);
        const char *name = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        tag *t = lib.retrieve_tag(std::string(name, sqlite3_column_bytes(st, 1)));
        tags[id] = t;
        if(t->get_db_id())
            extra_tags.emplace_back(t->get_db_id(), id);
        else
            t->set_db_id(id);
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE)
    {
        fail();
        rollback();
        return false;
    }
    if(!merge_tag_rows(extra_tags))
    {
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
            f->second->add_tag(t->second);
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE)
    {
        fail();
        rollback();
        return false;
    }

    if(sqlite3_prepare_v2(handle, "SELECT file_id, position, length, location, last_read FROM reading_progress;", -1,
        &st, nullptr) != SQLITE_OK)
    {
        fail();
        rollback();
        return false;
    }
    while((rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        auto f = files.find(sqlite3_column_int64(st, 0));
        if(f == files.end())
            continue;
        reading_progress p;
        p.position = sqlite3_column_int64(st, 1);
        p.length = sqlite3_column_int64(st, 2);
        const char *location = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
        if(location)
            p.location.assign(location, sqlite3_column_bytes(st, 3));
        p.last_read = sqlite3_column_int64(st, 4);
        f->second->set_progress(p);
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
