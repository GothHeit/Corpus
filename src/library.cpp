#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <cctype>

#include "../include/file.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"

static std::string ToLower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool ContainsCaseInsensitive(const std::string &haystack, const std::string &needle)
{
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

static bool MatchesSubstring(file *f, const std::string &query)
{
    if (ContainsCaseInsensitive(f->get_path(), query))
        return true;

    for (tag* t : f->get_tags())
    {
        if (ContainsCaseInsensitive(t->get_id(), query))
            return true;
    }
    return false;
}

/// @brief Adds a file to the library.
/// @param path Path for the file to be added.
file* library::add_file(const std::string &path)
{
    for(file* f : this->lib_files)
    {
        if(f->get_path() == path)
        {
            return f;
        }
    }
    file* f = new file(path);
    lib_files.push_back(f);
    return f;
}

void library::edit_file(file* f, tag* t, const bool add)
{
    if(add)
    {
        f->add_tag(t);
    }
    else // !add
    {
        f->remove_tag(t);
    }
    update_seen_files();
}

/// @brief Changes the view filter applied to the files in the library.
/// @param tofind Tag to be added or removed from the filter;
/// @param add Boolean describing whether the tag should be added(1) or deleted(0) from the filter.
void library::filter_files(tag* tofind, const bool add)
{
    // 1 -> add, 0 -> remove
    if(add)
    {
        if(std::find(this->seen_tags.begin(), this->seen_tags.end(), tofind) == this->seen_tags.end())
            this->seen_tags.push_back(tofind);
        else
            return;
    }
    else
    {
        if(std::find(this->seen_tags.begin(), this->seen_tags.end(), tofind) == this->seen_tags.end())
            return;
        else
            this->seen_tags.erase(std::find(this->seen_tags.begin(), this->seen_tags.end(), tofind));
    }
    update_seen_files();
}


void library::update_seen_files()
{
    std::unordered_set<file*> out;

    if(seen_tags.size() == 0)
    {
        seen_files = lib_files;
        return;
    }

    for(const tag* ta : this->seen_tags)
    {
        for(file* fi : ta->get_files())
        {
            out.insert(fi);
        }
    }
    
    for(const tag* ta : this->seen_tags)
    {
        std::unordered_set<file*> temp;
        for(file* f : out)
        {
            if(f->tagged(ta))
            {
                temp.insert(f);
            }
        }
        out = temp;
    }

    this->seen_files.assign(out.begin(), out.end());
}

/// @brief edits a tag identifier.
/// @param t tag to be edited.
/// @param a the new identifier intended for the tag.
/// @return a boolean describing a tag with that identifier already exists. 
bool library::edit_tag(tag* t, const std::string &a)
{
    for(const tag* ta : this->tags)
    {
        if(ta->get_id() == a)
        {
            return 0;
        }
    }
    t->set_id(a);
    return 1;
}

tag* library::create_tag(const std::string &name)
{
    tag* t = new tag(name);
    tags.push_back(t);
    return t;
}

tag* library::retrieve_tag(const std::string &name)
{
    for(tag* ta : tags)
    {
        if(ta->get_id() == name)
        {
            return ta;
        }
    }
    return this->create_tag(name);
}

const std::vector<file*>& library::get_files() const
{
    return lib_files;
}

const std::vector<tag*>& library::get_tags() const
{
    return tags;
}

const std::vector<tag*>& library::current_filter() const
{
    return seen_tags;
}

const std::vector<file*>& library::show() const
{
    return seen_files;
}

SearchResult library::search(const std::string &query) const
{
    size_t open = query.find('{');

    if (open == std::string::npos)
    {
        if (query.empty())
            return { seen_files, true };

        std::vector<file*> results;
        for (file* f : seen_files)
        {
            if (MatchesSubstring(f, query))
                results.push_back(f);
        }
        return { results, true };
    }

    size_t close = query.find('}', open);
    if (close == std::string::npos)
        return { {}, false };   // '{' without matching '}'

    std::string plainText = query.substr(0, open) + query.substr(close + 1);
    std::string braceContent = query.substr(open + 1, close - open - 1);

    std::unique_ptr<QueryNode> ast = ParseQuery(braceContent);
    if (!ast)
        return { {}, false };   // malformed expression inside {}

    std::vector<file*> results;
    for (file* f : seen_files)
    {
        bool matchesText = plainText.empty() || MatchesSubstring(f, plainText);
        if (matchesText && ast->Evaluate(f->get_tags()))
            results.push_back(f);
    }
    return { results, true };
}

library::~library()
{

    for (file* f : this->lib_files)
    {
        delete f;
    }
    for (tag* t : this->tags)
    {
        delete t;
    }

}