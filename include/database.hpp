#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

class library;

/// @brief A SQLite database holding a library: its files, its tags and which file has which tag.
class database
{
    enum statement
    {
        insert_file_stmt,
        insert_tag_stmt,
        rename_tag_stmt,
        link_stmt,
        unlink_stmt,
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

    public:

    static const int schema_version = 1;

    database() = default;
    database(const database&) = delete;
    database& operator=(const database&) = delete;

    bool open(const std::string &path);
    void close();
    bool is_open() const;

    bool is_new() const;
    bool create_schema();

    bool begin();
    bool commit();
    void rollback();

    std::int64_t insert_file(const std::string &path);
    std::int64_t insert_tag(const std::string &name);
    bool rename_tag(std::int64_t id, const std::string &name);
    bool link(std::int64_t file_id, std::int64_t tag_id);
    bool unlink(std::int64_t file_id, std::int64_t tag_id);

    bool load(library &lib);

    const std::string& last_error() const;
    void set_error(const std::string &message);
    void clear_error();

    ~database();
};

#endif
