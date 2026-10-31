#ifndef TAG_HPP
#define TAG_HPP

#include <cstdint>
#include <string>
#include <vector>

class file;

/// Whether a name can be filtered by in the search box: not empty, no spaces, control characters or "{}()", not
/// starting with '-', and not AND, OR or NOT (see CanFilterBy in the GUI). Nested names (see tag_path_separator)
/// have no empty level: "A/", "/A" and "A//B" are invalid.
bool is_valid_tag_name(const std::string &name);

/// Turns text (a folder name, say) into a valid tag name: trims, turns runs of spaces, control characters and
/// "{}()" into one '_', drops empty levels ("A//B" is "A/B") and '-' and '_' from the ends. Empty if nothing usable
/// is left or it is AND, OR or NOT.
std::string normalize_tag_name(const std::string &text);

/// Tags nest by name: "Music/Album/Disc_1" sits under "Music/Album", which sits under "Music". A parent doesn't
/// have to exist as a tag of its own.
constexpr char tag_path_separator = '/';

/// The branch a nested tag sits under ("Music/Album" for "Music/Album/Disc_1"); empty for a top-level name.
std::string tag_parent(const std::string &name);

/// The tag and every branch above it, nearest first ("Music/Album" gives Music/Album, Music). A folder import puts
/// all of them on a file as real tags, and so does every add (library::edit_files), so deleting one level
/// (library::delete_tag_keep_children) leaves the rest.
std::vector<std::string> tag_with_ancestors(const std::string &name);

/// Whether name is branch itself or a tag nested under it. Compares bytes: fold both with fold_case for tag identity.
bool tag_under(const std::string &name, const std::string &branch);

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