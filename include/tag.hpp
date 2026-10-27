#ifndef TAG_HPP
#define TAG_HPP

#include <cstdint>
#include <string>
#include <vector>

class file;

/// Whether a name can be filtered by in the search box: not empty, no spaces, control characters or "{}()", not
/// starting with '-', and not AND, OR or NOT (see CanFilterBy in the GUI).
bool is_valid_tag_name(const std::string &name);

/// Turns text (a folder name, say) into a valid tag name: trims, turns runs of spaces, control characters and
/// "{}()" into one '_', drops '-' and '_' from the ends. Empty if nothing usable is left or it is AND, OR or NOT.
std::string normalize_tag_name(const std::string &text);

class tag
{
    
    void remove_file_unreciprocated(const file* f);   
    
    std::string id;
    std::string folded;
    std::string search_name; // the name folded for searches (see fold_search)
    std::vector<file*> has_tag;
    std::int64_t db_id = 0;
    
    public:
 
    friend class file;
    friend class library;

    explicit tag(const std::string &id);
    const std::string& get_id() const;
    void set_id(const std::string &id);
    const std::string& get_folded_id() const;
    const std::string& get_search_name() const;
    std::int64_t get_db_id() const;
    void set_db_id(std::int64_t id);
    std::vector<std::string> paths_to_files() const;
    const std::vector<file*>& get_files() const;
    void add_file(file* f);
    void remove_file(file* f);

    void unlink_from_files();

    ~tag();
};

#endif