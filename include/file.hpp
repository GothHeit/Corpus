#ifndef FILE_HPP
#define FILE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class tag;

/// @brief How far a document (a PDF, a comic, a book) has been read, to open it where reading stopped.
struct reading_progress
{
    std::int64_t position = 0;  // where reading stopped: for paged files, the page index from 0
    std::int64_t length = 0;    // how many positions there are (the page count), or 0 if unknown
    std::string location;       // where to resume in a file without fixed pages (an EPUB), as its reader writes it, or ""
    std::int64_t last_read = 0; // when it was last read, in seconds since 1970 (UTC)
};

class file
{
    std::string path;
    std::vector<tag*> tags;
    std::int64_t db_id = 0;
    std::string identity;
    std::unique_ptr<reading_progress> progress; // only for files that were read

    void remove_tag_unreciprocated(tag* t);

public:

    friend class tag;

    explicit file(const std::string &patt);

    void set_path(const std::string &a);
    std::string get_path() const;
    std::vector<tag*> get_tags() const;

    std::int64_t get_db_id() const;
    void set_db_id(std::int64_t id);

    const std::string& get_identity() const;
    void set_identity(const std::string &id);

    const reading_progress* get_progress() const;
    void set_progress(const reading_progress &p);
    void clear_progress();

    void add_tag(tag *t);
    void remove_tag(tag *t);

    bool tagged(const tag *t) const;

    bool operator==(const file &b) const;
    bool operator!=(const file &b) const;

    void unlink_from_tags();
    ~file();
};

#endif