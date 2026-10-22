#ifndef LIBRARY_HPP
#define LIBRARY_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include "tag_query.hpp"

class tag;
class file;
class database;

class library
{
    std::string name;
    std::vector<file*> lib_files;
    std::vector<tag*> tags;
    std::unordered_map<std::string, file*> files_by_path;
    std::unordered_map<std::string, tag*> tags_by_name;
    std::vector<file*> seen_files;
    std::vector<tag*> seen_tags;
    database* db = nullptr;


    public: 
    
    
    void attach(database* d);

    database* attached() const;

    void clear();

    void update_seen_files();
    
    file* add_file(const std::string &path);
    
    void edit_file(file* f, tag* t, const bool add);
    
    void filter_files(tag* tofind, const bool add);
    
    bool edit_tag(tag* t, const std::string &a);
    
    tag* create_tag(const std::string &name);
    
    tag* retrieve_tag(const std::string &name);

    tag* find_tag(const std::string &name) const;
    
    const std::vector<file*>& show() const;

    SearchResult search(const std::string &query) const;

    const std::vector<tag*>& current_filter() const;
    
    const std::vector<file*>& get_files() const;
    
    const std::vector<tag*>& get_tags() const;
    
    ~library();
};

#endif