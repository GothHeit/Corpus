#ifndef FILE_HPP
#define FILE_HPP

#include <cstdint>
#include <string>
#include <vector>

class tag;

class file
{
    std::string path;
    std::vector<tag*> tags;
    std::int64_t db_id = 0;
    std::string identity;

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

    void add_tag(tag *t);
    void remove_tag(tag *t);

    bool tagged(const tag *t) const;

    bool operator==(const file &b) const;
    bool operator!=(const file &b) const;

    void unlink_from_tags();
    ~file();
};

#endif