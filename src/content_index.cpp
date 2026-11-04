#include <chrono>
#include <cstring>
#include <filesystem>
#include <system_error>

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

// The index is rebuilt from the files, so it can be thrown away: it never changes the catalog's schema version.
// content_docs follows the file's row (ON DELETE CASCADE); content_fts is contentless (the text isn't kept), keyed by
// the file id, and is swept by prune() since a virtual table can't be in a foreign key.
const char *index_sql =
    "CREATE TABLE IF NOT EXISTS content_docs("
    "    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,"
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

} // namespace

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

bool content_index::fail()
{
    set_error(handle ? sqlite3_errmsg(handle) : "content index is not open");
    return false;
}

bool content_index::exec(const char *sql)
{
    if(!handle)
        return fail();
    return sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) == SQLITE_OK || fail();
}

bool content_index::open(const std::string &utf8_path)
{
    close();
    if(sqlite3_open_v2(utf8_path.c_str(), &handle, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
    {
        fail();
        close();
        return false;
    }
    sqlite3_busy_timeout(handle, 5000);
    if(!exec("PRAGMA foreign_keys = ON;") || !exec(index_sql))
    {
        close();
        return false;
    }
    return true;
}

void content_index::close()
{
    std::lock_guard<std::mutex> guard(lock);
    sqlite3_close(handle);
    handle = nullptr;
}

content_index::~content_index()
{
    close();
}

bool content_index::is_current(std::int64_t file_id, const content_signature &sig)
{
    std::lock_guard<std::mutex> guard(lock);
    sqlite3_stmt *st = nullptr;
    if(!handle || sqlite3_prepare_v2(handle, "SELECT size, mtime, format FROM content_docs WHERE file_id = ?1;", -1, &st,
        nullptr) != SQLITE_OK)
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

bool content_index::store(std::int64_t file_id, const content_signature &sig, const std::string &text)
{
    std::lock_guard<std::mutex> guard(lock);
    if(!handle)
        return fail();
    if(!exec("SAVEPOINT content_store;"))
        return false;
    bool ok = true;
    sqlite3_stmt *st = nullptr;
    if(sqlite3_prepare_v2(handle, "DELETE FROM content_fts WHERE rowid = ?1;", -1, &st, nullptr) != SQLITE_OK)
        ok = fail();
    else
    {
        sqlite3_bind_int64(st, 1, file_id);
        ok = sqlite3_step(st) == SQLITE_DONE || fail();
        sqlite3_finalize(st);
    }
    if(ok)
    {
        // The foreign key makes this fail for a file removed from the catalog while its text was being read.
        ok = sqlite3_prepare_v2(handle, "INSERT OR REPLACE INTO content_docs(file_id, size, mtime, format) "
            "VALUES(?1, ?2, ?3, ?4);", -1, &st, nullptr) == SQLITE_OK || fail();
        if(ok)
        {
            sqlite3_bind_int64(st, 1, file_id);
            sqlite3_bind_int64(st, 2, sig.size);
            sqlite3_bind_int64(st, 3, sig.mtime);
            sqlite3_bind_int(st, 4, sig.format);
            ok = sqlite3_step(st) == SQLITE_DONE || fail();
            sqlite3_finalize(st);
        }
    }
    if(ok && !text.empty())
    {
        ok = sqlite3_prepare_v2(handle, "INSERT INTO content_fts(rowid, text) VALUES(?1, ?2);", -1, &st, nullptr) ==
            SQLITE_OK || fail();
        if(ok)
        {
            sqlite3_bind_int64(st, 1, file_id);
            sqlite3_bind_text(st, 2, text.data(), static_cast<int>(text.size()), SQLITE_STATIC);
            ok = sqlite3_step(st) == SQLITE_DONE || fail();
            sqlite3_finalize(st);
        }
    }
    if(!ok)
    {
        std::string message = last_error();
        sqlite3_exec(handle, "ROLLBACK TO content_store; RELEASE content_store;", nullptr, nullptr, nullptr);
        set_error(message);
        return false;
    }
    return exec("RELEASE content_store;");
}

bool content_index::remove(std::int64_t file_id)
{
    std::lock_guard<std::mutex> guard(lock);
    if(!handle)
        return fail();
    sqlite3_stmt *st = nullptr;
    bool ok = sqlite3_prepare_v2(handle, "DELETE FROM content_fts WHERE rowid = ?1;", -1, &st, nullptr) == SQLITE_OK;
    if(ok)
    {
        sqlite3_bind_int64(st, 1, file_id);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
    }
    if(ok)
    {
        ok = sqlite3_prepare_v2(handle, "DELETE FROM content_docs WHERE file_id = ?1;", -1, &st, nullptr) == SQLITE_OK;
        if(ok)
        {
            sqlite3_bind_int64(st, 1, file_id);
            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);
        }
    }
    return ok || fail();
}

bool content_index::clear()
{
    std::lock_guard<std::mutex> guard(lock);
    if(!handle)
        return fail();
    // content_fts is dropped and made again (cheaper than deleting row by row); VACUUM returns the space.
    return exec("DROP TABLE IF EXISTS content_fts; DROP TABLE IF EXISTS content_docs;") && exec(index_sql) &&
        exec("VACUUM;");
}

int content_index::prune()
{
    std::lock_guard<std::mutex> guard(lock);
    if(!handle)
        return 0;
    // Text rows of files the catalog no longer has (their content_docs row went with the file).
    sqlite3_exec(handle, "DELETE FROM content_fts WHERE rowid NOT IN (SELECT file_id FROM content_docs);", nullptr,
        nullptr, nullptr);
    return sqlite3_changes(handle);
}

std::unordered_set<std::int64_t> content_index::match(const std::string &word)
{
    std::unordered_set<std::int64_t> ids;
    std::size_t letters = 0;
    std::string query = prefix_query(word, letters);
    if(query.empty() || letters < content_min_word)
        return ids;
    std::lock_guard<std::mutex> guard(lock);
    sqlite3_stmt *st = nullptr;
    // Joined to content_docs: a text row whose file is gone never matches, even before prune() sweeps it.
    if(!handle || sqlite3_prepare_v2(handle, "SELECT rowid FROM content_fts WHERE content_fts MATCH ?1 AND rowid IN "
        "(SELECT file_id FROM content_docs);", -1, &st, nullptr) != SQLITE_OK)
        return ids;
    sqlite3_bind_text(st, 1, query.c_str(), -1, SQLITE_TRANSIENT);
    while(sqlite3_step(st) == SQLITE_ROW)
        ids.insert(sqlite3_column_int64(st, 0));
    sqlite3_finalize(st);
    return ids;
}

std::size_t content_index::indexed_count()
{
    std::lock_guard<std::mutex> guard(lock);
    sqlite3_stmt *st = nullptr;
    std::size_t count = 0;
    if(handle && sqlite3_prepare_v2(handle, "SELECT COUNT(*) FROM content_docs;", -1, &st, nullptr) == SQLITE_OK)
    {
        if(sqlite3_step(st) == SQLITE_ROW)
            count = static_cast<std::size_t>(sqlite3_column_int64(st, 0));
        sqlite3_finalize(st);
    }
    return count;
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
                    std::string text;
                    // A file that can't be read now (locked, half copied) is not stored, so the next scan tries again.
                    if(extractor->extract(job.path, text, max_text_bytes) == extract_status::ok)
                        stored = index.store(job.file_id, sig, text);
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
