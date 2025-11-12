#ifndef LIBRARY_HPP
#define LIBRARY_HPP

#include <string>
#include <vector>


class tag;
class file;

class library
{
    std::string name;
    std::vector<file*> lib_files;
    std::vector<tag*> tags;
    std::vector<file*> seen_files;
    std::vector<tag*> seen_tags;


    public: 
    
    ~library();

    void update_seen_files();

    file* add_file(const std::string &path);

    void edit_file(file* f, tag* t, bool add);

    void filter_files(tag* tofind,const bool &add);

    bool edit_tag(tag* t, const std::string &a);

    tag* create_tag(const std::string &name);

    tag* retrieve_tag(const std::string &name);

    void show();

    std::vector<tag*> current_filter() const;

    std::vector<file*> get_files() const;
    
    std::vector<tag*> get_tags() const;
    
};

#endif