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

/// What a tag can say about itself besides its name: a free-text note on any tag, and optional structured fields
/// mostly used on reference tags ("ref/..."): who made the thing, when, where it is, and what was taken from it and
/// what was refused. Every field is free text and may be empty. Saved with the catalog (table tag_attr).
struct tag_attrs
{
    std::string note;
    std::string creator;
    std::string year;     // text, so "1971", "c. 1971" and "1970s" all fit
    std::string url;
    std::string taken;
    std::string refused;

    bool empty() const
    {
        return note.empty() && creator.empty() && year.empty() && url.empty() && taken.empty() && refused.empty();
    }

    bool operator==(const tag_attrs &o) const
    {
        return note == o.note && creator == o.creator && year == o.year && url == o.url && taken == o.taken
            && refused == o.refused;
    }

    bool operator!=(const tag_attrs &o) const
    {
        return !(*this == o);
    }
};

/// Folds the fields of from into into for a merge of two tags: a field into lacks is taken from from, one both have
/// and that differs is joined, into's text first ("a\n\nb"). Returns whether into changed.
bool merge_tag_attrs(tag_attrs &into, const tag_attrs &from);

class tag
{
    
    void remove_file_unreciprocated(const file* f);   
    
    std::string id;
    std::string folded;
    std::string search_name; // the name folded for searches (see fold_search)
    std::vector<file*> has_tag;
    std::int64_t db_id = 0;
    tag_attrs attrs;
    
    public:
 
    friend class file;
    friend class library;

    explicit tag(const std::string &id);
    const std::string& get_id() const;
    void set_id(const std::string &id);
    const std::string& get_folded_id() const;
    const std::string& get_search_name() const;
    std::int64_t get_db_id() const;
    const tag_attrs& get_attrs() const {return attrs;}
    void set_attrs(const tag_attrs &a) {attrs = a;}
    void set_db_id(std::int64_t id);
    std::vector<std::string> paths_to_files() const;
    const std::vector<file*>& get_files() const;
    void add_file(file* f);
    void remove_file(file* f);

    void unlink_from_files();

    ~tag();
};

#endif