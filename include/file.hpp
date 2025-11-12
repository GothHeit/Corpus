#ifndef FILE_HPP
#define FILE_HPP

#include <string>
#include <vector>

class tag;

class file
{
    std::string path = "NULL";
    std::vector<tag*> tags;

public:

    explicit file(const std::string &patt) : path(patt) {}

    void set_path(const std::string &a);
    std::string get_path() const;
    std::vector<tag*> get_tags() const;

    void add_tag(tag *t);
    void remove_tag(tag *t);

    bool tagged(const tag *t) const;

    bool operator==(const file &b) const;
    bool operator!=(const file &b) const;
};

#endif