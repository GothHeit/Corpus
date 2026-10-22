#ifndef LIBRARY_HPP
#define LIBRARY_HPP

#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tag_query.hpp"

class tag;
class file;
class database;
struct scanned_file;

/// @brief A file of the library that is missing from its path, and where the same file is now.
struct moved_file
{
    file* f;
    std::string new_path;
};

/// @brief The sidebar's Library views: which files a search looks in.
enum class LibraryView
{
    All,        // every file
    Untagged,   // files with no tag
    Missing     // files no longer at their path (see file_missing)
};

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
    // The Missing view's files, as the disk was when last checked (see refresh_missing).
    // Built by the first Missing search if refresh_missing wasn't called yet, so search, though const,
    // changes them: call it from one thread at a time.
    mutable std::unordered_set<const file*> missing;
    mutable bool missing_checked = false;


    public: 
    
    
    void attach(database* d);

    database* attached() const;

    void clear();

    void update_seen_files();
    
    file* add_file(const std::string &path);

    int add_files(const std::vector<scanned_file> &files);

    file* find_file(const std::string &path) const;
    
    void edit_file(file* f, tag* t, const bool add);

    bool remove_file(file* f);

    std::vector<file*> missing_files() const;

    std::vector<file*> refresh_missing();

    int record_identities();

    std::vector<moved_file> find_moved(const std::string &folder, std::error_code *error = nullptr) const;

    bool relink(file* f, const std::string &new_path);
    
    void filter_files(tag* tofind, const bool add);
    
    bool edit_tag(tag* t, const std::string &a);
    
    tag* create_tag(const std::string &name);

    bool delete_tag(tag* t);
    
    tag* retrieve_tag(const std::string &name);

    tag* find_tag(const std::string &name) const;
    
    const std::vector<file*>& show() const;

    SearchResult search(const std::string &query, LibraryView view = LibraryView::All) const;

    const std::vector<tag*>& current_filter() const;
    
    const std::vector<file*>& get_files() const;
    
    const std::vector<tag*>& get_tags() const;
    
    ~library();
};

#endif