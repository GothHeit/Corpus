#ifndef TAG_HPP
#define TAG_HPP

#include <string>
#include <vector>

class file;

class tag
{
    
    void remove_file_unreciprocated(const file* f);   
    
    std::string id;
    std::vector<file*> has_tag;
    
    public:
 
    friend class file;

    explicit tag(const std::string &id);
    std::string get_id() const;
    std::vector<std::string> paths_to_files() const;
    std::vector<file*> get_files() const;
    void add_file(file* f);
    void remove_file(file* f);

    void unlink_from_files();

    ~tag();
};

#endif