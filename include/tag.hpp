#ifndef TAG_HPP
#define TAG_HPP

#include <cstdint>
#include <string>
#include <vector>

class file;

class tag
{
    
    void remove_file_unreciprocated(const file* f);   
    
    std::string id;
    std::string folded;
    std::vector<file*> has_tag;
    std::int64_t db_id = 0;
    
    public:
 
    friend class file;

    explicit tag(const std::string &id);
    std::string get_id() const;
    void set_id(const std::string &id);
    const std::string& get_folded_id() const;
    std::int64_t get_db_id() const;
    void set_db_id(std::int64_t id);
    std::vector<std::string> paths_to_files() const;
    std::vector<file*> get_files() const;
    void add_file(file* f);
    void remove_file(file* f);

    void unlink_from_files();

    ~tag();
};

#endif