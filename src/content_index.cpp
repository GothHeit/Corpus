#include <chrono>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include "../include/content_index.hpp"
#include "../include/text.hpp"
#include "../include/text_extract.hpp"
#include "../third_party/sqlite/sqlite3.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{

// The index is rebuilt from the files, so it can be thrown away. content_docs says which files have an entry;
// content_fts is contentless (the text isn't kept), keyed by the file id. Neither is tied to the catalog by a foreign
// key (they live in another file): the attached catalog ("cat") is consulted instead, and prune() sweeps the rest.
const char *index_sql =
    "CREATE TABLE IF NOT EXISTS content_docs("
    "    file_id INTEGER PRIMARY KEY,"
    "    size INTEGER NOT NULL,"
    "    mtime INTEGER NOT NULL,"
    "    format INTEGER NOT NULL"
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS content_fts USING fts5("
    "    text, content='', contentless_delete=1, tokenize='unicode61 remove_diacritics 2'"
    ");";

const std::size_t max_text_bytes = 48u << 20; // of one file's text; more is cut (a very long book is ~2 MB)

/// A word as a safe FTS5 prefix query: letters and digits only, quoted. "" if there are none.
std::string prefix_query(const std::string &word, std::size_t &letters)
{
    std::string folded = fold_search(word);
    std::string token;
    letters = 0;
    for(char c : folded)
    {
        unsigned char u = static_cast<unsigned char>(c);
        if(u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z'))
        {
            token.push_back(c);
            if((u & 0xC0) != 0x80)
                letters++;
        }
        else if(!token.empty())
        {
            break; // "don't" -> "don": the tokenizer splits there too, and the rest is optional detail
        }
    }
    if(token.empty())
        return "";
    return "\"" + token + "\"*";
}

/// Runs a statement that returns one integer; `fallback` if it can't.
std::int64_t scalar(sqlite3 *handle, const char *sql, std::int64_t fallback = 0)
{
    sqlite3_stmt *st = nullptr;
    std::int64_t value = fallback;
    if(handle && sqlite3_prepare_v2(handle, sql, -1, &st, nullptr) == SQLITE_OK)
    {
        if(sqlite3_step(st) == SQLITE_ROW)
            value = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    return value;
}

} // namespace

std::filesystem::path content_index_path(const std::filesystem::path &catalog)
{
    std::filesystem::path stem = catalog.stem();
    stem += "-index";
    stem += catalog.extension();
    return catalog.parent_path() / stem;
}

void content_index::set_error(const std::string &message)
{
    std::lock_guard<std::mutex> guard(error_lock);
    error = message;
}

std::string content_index::last_error()
{
    std::lock_guard<std::mutex> guard(error_lock);
    return error;
}

bool content_index::fail(sqlite3 *handle)
{
    set_error(handle ? sqlite3_errmsg(handle) : "content index is not open");
    return false;
}

bool content_index::exec(sqlite3 *handle, const char *sql)
{
    if(!handle)
        return fail(handle);
    return sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) == SQLITE_OK || fail(handle);
}

bool content_index::open_connection(sqlite3 *&handle, const std::string &index_path, const std::string &catalog_path)
{
    if(sqlite3_open_v2(index_path.c_str(), &handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        return fail(handle);
    sqlite3_busy_timeout(handle, 5000);
    // WAL: a search reads while a book is being stored. NORMAL is safe in WAL, and a lost write only means a book is
    // read again.
    if(!exec(handle, "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;"))
        return false;
    sqlite3_stmt *st = nullptr;
    if(sqlite3_prepare_v2(handle, "ATTACH DATABASE ?1 AS cat;", -1, &st, nullptr) != SQLITE_OK)
        return fail(handle);
    sqlite3_bind_text(st, 1, catalog_path.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok || fail(handle);
}

bool content_index::open(const std::string &utf8_index_path, const std::string &utf8_catalog_path)
{
    close();
    if(!open_connection(writer, utf8_index_path, utf8_catalog_path) || !exec(writer, index_sql) ||
        !open_connection(reader, utf8_index_path, utf8_catalog_path))
    {
        close();
        return false;
    }
    migrate_from_catalog();
    return true;
}

/// Older versions kept the index in the catalog file itself. It is rebuilt from the books, so those tables are only
/// dropped (and the space given back); the catalog's other tables are untouched. Runs once, before the window is up.
void content_index::migrate_from_catalog()
{
    if(scalar(writer, "SELECT COUNT(*) FROM cat.sqlite_schema WHERE name IN ('content_docs', 'content_fts');") == 0)
        return;
    if(sqlite3_exec(writer, "DROP TABLE IF EXISTS cat.content_fts; DROP TABLE IF EXISTS cat.content_docs;", nullptr,
        nullptr, nullptr) == SQLITE_OK)
        sqlite3_exec(writer, "VACUUM cat;", nullptr, nullptr, nullptr);
}

void content_index::close()
{
    {
        std::lock_guard<std::mutex> guard(write_lock);
        sqlite3_close(writer);
        writer = nullptr;
    }
    std::lock_guard<std::mutex> guard(read_lock);
    sqlite3_close(reader);
    reader = nullptr;
}

content_index::~content_index()
{
    close();
}

bool content_index::is_current(std::int64_t file_id, const content_signature &sig)
{
    std::lock_guard<std::mutex> guard(read_lock);
    sqlite3_stmt *st = nullptr;
    if(!reader || sqlite3_prepare_v2(reader, "SELECT size, mtime, format FROM content_docs WHERE file_id = ?1;", -1,
        &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, file_id);
    bool current = false;
    if(sqlite3_step(st) == SQLITE_ROW)
    {
        content_signature stored;
        stored.size = sqlite3_column_int64(st, 0);
        stored.mtime = sqlite3_column_int64(st, 1);
        stored.format = sqlite3_column_int(st, 2);
        current = stored == sig;
    }
    sqlite3_finalize(st);
    return current;
}

bool content_index::store(std::int64_t file_id, const content_signature &sig, const std::string &text,
    std::uint64_t for_generation)
{
    std::lock_guard<std::mutex> guard(write_lock);
    if(!writer)
        return fail(writer);
    if(for_generation != ~std::uint64_t(0) && for_generation != epoch)
    {
        set_error("the index was cleared meanwhile");
        return false;
    }
    // A file removed from the catalog while its text was being read has nothing to attach the entry to. (Checked
    // apart from the transaction below, so the catalog isn't read-locked during the long insert.)
    sqlite3_stmt *st = nullptr;
    bool exists = false;
    if(sqlite3_prepare_v2(writer, "SELECT 1 FROM cat.files WHERE id = ?1;", -1, &st, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int64(st, 1, file_id);
        exists = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    if(!exists)
    {
        set_error("the file is not in the catalog");
        return false;
    }
    if(!exec(writer, "SAVEPOINT content_store;"))
        return false;
    bool ok = true;
    if(sqlite3_prepare_v2(writer, "DELETE FROM content_fts WHERE rowid = ?1;", -1, &st, nullptr) != SQLITE_OK)
        ok = fail(writer);
    else
    {
        sqlite3_bind_int64(st, 1, file_id);
        ok = sqlite3_step(st) == SQLITE_DONE || fail(writer);
        sqlite3_finalize(st);
    }
    if(ok)
    {
        ok = sqlite3_prepare_v2(writer, "INSERT OR REPLACE INTO content_docs(file_id, size, mtime, format) "
            "VALUES(?1, ?2, ?3, ?4);", -1, &st, nullptr) == SQLITE_OK || fail(writer);
        if(ok)
        {
            sqlite3_bind_int64(st, 1, file_id);
            sqlite3_bind_int64(st, 2, sig.size);
            sqlite3_bind_int64(st, 3, sig.mtime);
            sqlite3_bind_int(st, 4, sig.format);
            ok = sqlite3_step(st) == SQLITE_DONE || fail(writer);
            sqlite3_finalize(st);
        }
    }
    if(ok && !text.empty())
    {
        ok = sqlite3_prepare_v2(writer, "INSERT INTO content_fts(rowid, text) VALUES(?1, ?2);", -1, &st, nullptr) ==
            SQLITE_OK || fail(writer);
        if(ok)
        {
            sqlite3_bind_int64(st, 1, file_id);
            sqlite3_bind_text(st, 2, text.data(), static_cast<int>(text.size()), SQLITE_STATIC);
            ok = sqlite3_step(st) == SQLITE_DONE || fail(writer);
            sqlite3_finalize(st);
        }
    }
    if(!ok)
    {
        std::string message = last_error();
        sqlite3_exec(writer, "ROLLBACK TO content_store; RELEASE content_store;", nullptr, nullptr, nullptr);
        set_error(message);
        return false;
    }
    return exec(writer, "RELEASE content_store;");
}

bool content_index::remove(std::int64_t file_id)
{
    std::lock_guard<std::mutex> guard(write_lock);
    if(!writer)
        return fail(writer);
    sqlite3_stmt *st = nullptr;
    bool ok = sqlite3_prepare_v2(writer, "DELETE FROM content_fts WHERE rowid = ?1;", -1, &st, nullptr) == SQLITE_OK;
    if(ok)
    {
        sqlite3_bind_int64(st, 1, file_id);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    if(ok)
    {
        ok = sqlite3_prepare_v2(writer, "DELETE FROM content_docs WHERE file_id = ?1;", -1, &st, nullptr) == SQLITE_OK;
        if(ok)
        {
            sqlite3_bind_int64(st, 1, file_id);
            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
        }
    }
    return ok || fail(writer);
}

bool content_index::clear()
{
    std::lock_guard<std::mutex> guard(write_lock);
    if(!writer)
        return fail(writer);
    epoch++;
    // content_fts is dropped and made again (cheaper than deleting row by row); VACUUM returns the space. It only
    // touches the index file, never the catalog.
    return exec(writer, "DROP TABLE IF EXISTS content_fts; DROP TABLE IF EXISTS content_docs;") &&
        exec(writer, index_sql) && exec(writer, "VACUUM main;");
}

int content_index::prune()
{
    std::lock_guard<std::mutex> guard(write_lock);
    if(!writer)
        return 0;
    int dropped = 0;
    // Entries of files the catalog no longer has.
    sqlite3_exec(writer, "DELETE FROM content_docs WHERE file_id NOT IN (SELECT id FROM cat.files);", nullptr, nullptr,
        nullptr);
    dropped += sqlite3_changes(writer);
    // An id can be handed to a new file after the old one was removed: if that file has no text extractor it would
    // keep the old file's words, so its entry goes. (One that has an extractor is read again by the signature.)
    std::vector<std::int64_t> stale;
    sqlite3_stmt *st = nullptr;
    if(sqlite3_prepare_v2(writer, "SELECT d.file_id, f.path FROM content_docs d JOIN cat.files f ON f.id = d.file_id;",
        -1, &st, nullptr) == SQLITE_OK)
    {
        while(sqlite3_step(st) == SQLITE_ROW)
        {
            const unsigned char *path = sqlite3_column_text(st, 1);
            if(!path || !find_text_extractor(reinterpret_cast<const char*>(path)))
                stale.push_back(sqlite3_column_int64(st, 0));
        }
        sqlite3_finalize(st);
    }
    for(std::int64_t id : stale)
    {
        if(sqlite3_prepare_v2(writer, "DELETE FROM content_docs WHERE file_id = ?1;", -1, &st, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int64(st, 1, id);
            if(sqlite3_step(st) == SQLITE_DONE)
                dropped += sqlite3_changes(writer);
            sqlite3_finalize(st);
        }
    }
    // Text rows with no entry.
    sqlite3_exec(writer, "DELETE FROM content_fts WHERE rowid NOT IN (SELECT file_id FROM content_docs);", nullptr,
        nullptr, nullptr);
    return dropped;
}

std::unordered_set<std::int64_t> content_index::match(const std::string &word)
{
    std::unordered_set<std::int64_t> ids;
    std::size_t letters = 0;
    std::string query = prefix_query(word, letters);
    if(query.empty() || letters < content_min_word)
        return ids;
    std::lock_guard<std::mutex> guard(read_lock);
    sqlite3_stmt *st = nullptr;
    // Joined to the entries and to the catalog: a text row whose file is gone never matches, even before prune()
    // sweeps it.
    if(!reader || sqlite3_prepare_v2(reader, "SELECT rowid FROM content_fts WHERE content_fts MATCH ?1 AND rowid IN "
        "(SELECT d.file_id FROM content_docs d JOIN cat.files f ON f.id = d.file_id);", -1, &st, nullptr) != SQLITE_OK)
        return ids;
    sqlite3_bind_text(st, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    while(sqlite3_step(st) == SQLITE_ROW)
        ids.insert(sqlite3_column_int64(st, 0));
    sqlite3_finalize(st);
    return ids;
}

std::size_t content_index::indexed_count()
{
    std::lock_guard<std::mutex> guard(read_lock);
    return static_cast<std::size_t>(scalar(reader, "SELECT COUNT(*) FROM content_docs d JOIN cat.files f ON "
        "f.id = d.file_id;"));
}

// ---- content_indexer ----

content_indexer::content_indexer(content_index &idx) : index(idx)
{
    worker = std::thread(&content_indexer::run, this);
}

content_indexer::~content_indexer()
{
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
    }
    wake.notify_all();
    if(worker.joinable())
        worker.join();
}

void content_indexer::set_progress_callback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> guard(lock);
    on_progress = std::move(callback);
}

void content_indexer::enqueue(const std::vector<content_job> &jobs)
{
    {
        std::lock_guard<std::mutex> guard(lock);
        for(const content_job &job : jobs)
        {
            if(queued.insert(job.file_id).second)
                queue.push_back(job);
        }
        pending_now = queue.size() + (running ? 1 : 0);
    }
    wake.notify_one();
}

void content_indexer::clear()
{
    std::lock_guard<std::mutex> guard(lock);
    queue.clear();
    queued.clear();
    pending_now = running ? 1 : 0;
}

void content_indexer::run()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    index.prune();
    std::size_t since_report = 0;
    auto last_report = std::chrono::steady_clock::now();
    while(true)
    {
        content_job job;
        std::function<void()> report;
        {
            std::unique_lock<std::mutex> guard(lock);
            if(queue.empty())
            {
                // Idle: say what is left of the last batch.
                if(since_report > 0 && on_progress)
                    report = on_progress;
                since_report = 0;
            }
            if(report)
            {
                guard.unlock();
                report();
                guard.lock();
            }
            wake.wait(guard, [&] { return stopping || !queue.empty(); });
            if(stopping)
                return;
            job = std::move(queue.front());
            queue.pop_front();
            queued.erase(job.file_id);
            running = true;
            pending_now = queue.size() + 1;
        }

        bool stored = false;
        const text_extractor *extractor = find_text_extractor(job.path);
        std::error_code ec;
        std::filesystem::path p = std::filesystem::u8path(job.path);
        if(extractor)
        {
            auto size = std::filesystem::file_size(p, ec);
            auto time = ec ? std::filesystem::file_time_type() : std::filesystem::last_write_time(p, ec);
            if(!ec)
            {
                content_signature sig;
                sig.size = static_cast<std::int64_t>(size);
                sig.mtime = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
                sig.format = extractor->version();
                if(!index.is_current(job.file_id, sig))
                {
                    std::uint64_t generation = index.generation(); // a clear() meanwhile drops this book
                    std::string text;
                    // A file that can't be read now (locked, half copied) is not stored, so the next scan tries again.
                    if(extractor->extract(job.path, text, max_text_bytes) == extract_status::ok)
                        stored = index.store(job.file_id, sig, text, generation);
                }
            }
        }
        std::function<void()> report_batch;
        {
            std::lock_guard<std::mutex> guard(lock);
            running = false;
            pending_now = queue.size();
            if(stored)
            {
                indexed_now++;
                since_report++;
                auto now = std::chrono::steady_clock::now();
                if(on_progress && now - last_report > std::chrono::seconds(2))
                {
                    last_report = now;
                    since_report = 0;
                    report_batch = on_progress;
                }
            }
        }
        if(report_batch)
            report_batch();
    }
}
