#ifndef CONTENT_INDEX_HPP
#define CONTENT_INDEX_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

struct sqlite3;

/// A search word shorter than this (in letters) is not looked up in book text: "a" would match every book.
const std::size_t content_min_word = 3;

/// @brief What the index knows about a file's text when it was indexed, to see whether the file changed since.
struct content_signature
{
    std::int64_t size = 0;      // bytes of the file
    std::int64_t mtime = 0;     // last write, seconds since 1970 (UTC)
    int format = 0;             // the extractor's version() when it was indexed

    bool operator==(const content_signature &o) const
    {
        return size == o.size && mtime == o.mtime && format == o.format;
    }
};

/// @brief The text of the library's documents, searchable (SQLite FTS5) and kept in the catalog's own database file
/// as extra tables, made on open. The text itself isn't stored, only the index (so it stays a fraction of the
/// books' size); a snippet to show for a hit is cut from the file again (see text_snippet). Words are folded by
/// FTS5's unicode61 tokenizer, so case and accents are ignored like in the rest of the search, and a query word
/// matches the start of a word in the text ("lord" finds "Lords").
///
/// It has its own connection to the file, so a background thread can write while the window searches; calls are
/// serialized inside. Rows are keyed by the file's database id: a file that moves keeps its entry, and removing a
/// file from the catalog drops its entry along with it (the index rows are swept by prune()).
class content_index
{
    sqlite3 *handle = nullptr;
    std::mutex lock;
    std::mutex error_lock;      // the indexer thread and the window both read and write error
    std::string error;

    void set_error(const std::string &message);
    bool exec(const char *sql);
    bool fail();

public:
    content_index() = default;
    content_index(const content_index&) = delete;
    content_index& operator=(const content_index&) = delete;

    /// Opens the catalog at the UTF-8 path (which must already hold the Corpus tables) and makes the index tables if
    /// they are missing. False if SQLite can't open it, or was built without FTS5.
    bool open(const std::string &utf8_path);
    void close();
    bool is_open() const { return handle != nullptr; }

    /// Whether the file's entry matches the signature, so it needs no new indexing.
    bool is_current(std::int64_t file_id, const content_signature &sig);

    /// Replaces the file's entry with its text and signature; one transaction. An empty text still records the
    /// signature, so a file with no text isn't read again until it changes.
    bool store(std::int64_t file_id, const content_signature &sig, const std::string &text);

    /// Drops a file's entry.
    bool remove(std::int64_t file_id);

    /// Throws the whole index away (every file's entry and all indexed words) and gives the disk space back. The
    /// catalog itself is untouched; indexing again rebuilds it. For a "clear search index" button.
    bool clear();

    /// Drops the entries of files no longer in the catalog. Returns how many.
    int prune();

    /// The ids of the files whose text has a word starting with `word` (any case or accents). Empty for a word
    /// shorter than content_min_word, or one with no letters.
    std::unordered_set<std::int64_t> match(const std::string &word);

    /// How many files have an entry (including those with no text).
    std::size_t indexed_count();

    std::string last_error();

    ~content_index();
};

/// @brief A file for the indexer to look at.
struct content_job
{
    std::int64_t file_id = 0;
    std::string path;
};

/// @brief Indexes in the background, one file at a time, at low priority: for each job it checks whether the file is
/// new or changed (size and write time), and only then reads it with the extractor for its extension. Files with no
/// extractor, or missing from disk, are skipped. Give it library::content_jobs() on startup and after files are
/// added; changed files are found by the signature, so giving it the same jobs again is cheap.
class content_indexer
{
    content_index &index;
    std::thread worker;
    std::mutex lock;
    std::condition_variable wake;
    std::deque<content_job> queue;
    std::unordered_set<std::int64_t> queued;    // ids in the queue, so none is queued twice
    bool stopping = false;
    bool running = false;                       // a job is being read
    std::atomic<std::size_t> indexed_now{0};
    std::atomic<std::size_t> pending_now{0};
    std::function<void()> on_progress;

    void run();

public:
    /// `index` must stay open while the indexer lives.
    explicit content_indexer(content_index &index);
    content_indexer(const content_indexer&) = delete;
    content_indexer& operator=(const content_indexer&) = delete;

    /// Called from the indexing thread each time a batch of files has been stored (and once when the queue empties),
    /// so a window can refresh its results; it must be quick and thread-safe (post a message). Set it before enqueue.
    void set_progress_callback(std::function<void()> callback);

    void enqueue(const std::vector<content_job> &jobs);

    /// Forgets the jobs not started yet.
    void clear();

    /// Jobs waiting (including the one running), and files stored since the indexer was made.
    std::size_t pending() const { return pending_now; }
    std::size_t indexed() const { return indexed_now; }

    /// Stops the thread (the job running finishes first).
    ~content_indexer();
};

#endif
