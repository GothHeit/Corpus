#ifndef TAG_HPP
#define TAG_HPP

#include <string>
#include <vector>

class file;

class tag
{
    public:
    
    std::string id;
    std::vector<file*> has_tag{};

    tag(const std::string &id);
    std::vector<std::string> paths_to_files() const;
    std::vector<file*> get_files() const;
    void add_file(file* f);
    void remove_file(const file* f);
};

#endif