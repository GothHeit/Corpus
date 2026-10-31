#include <string>
#include <vector>
#include <algorithm>

#include "../include/file.hpp"
#include "../include/tag.hpp"
#include "../include/text.hpp"

static bool is_separator(unsigned char c)
{
    return c <= ' ' || c == 0x7f || c == '{' || c == '}' || c == '(' || c == ')';
}

bool is_valid_tag_name(const std::string &name)
{
    if(name.empty() || name[0] == '-' || name == "AND" || name == "OR" || name == "NOT")
    {
        return false;
    }
    for(unsigned char c : name)
    {
        if(is_separator(c))
        {
            return false;
        }
    }
    return true;
}

std::string normalize_tag_name(const std::string &text)
{
    std::string out;
    for(unsigned char c : text)
    {
        if(is_separator(c))
        {
            if(!out.empty() && out.back() != '_')
            {
                out += '_';
            }
        }
        else
        {
            out += static_cast<char>(c);
        }
    }
    std::size_t first = out.find_first_not_of("-_");
    if(first == std::string::npos)
    {
        return "";
    }
    out = out.substr(first, out.find_last_not_of("-_") - first + 1);
    return is_valid_tag_name(out) ? out : "";
}

std::string tag_parent(const std::string &name)
{
    std::size_t at = name.rfind(tag_path_separator);
    return at == std::string::npos ? std::string() : name.substr(0, at);
}

std::vector<std::string> tag_with_ancestors(const std::string &name)
{
    std::vector<std::string> out;
    for(std::string at = name; !at.empty(); at = tag_parent(at))
        out.push_back(at);
    return out;
}

bool tag_under(const std::string &name, const std::string &branch)
{
    return !branch.empty() && name.compare(0, branch.size(), branch) == 0 &&
        (name.size() == branch.size() || name[branch.size()] == tag_path_separator);
}

tag::tag(const std::string &id) : id(id), folded(fold_case(id)), search_name(fold_search(id)) {}

const std::string& tag::get_id() const
{
    return id;
}

void tag::set_id(const std::string &id)
{
    this->id = id;
    folded = fold_case(id);
    search_name = fold_search(id);
}

/// Name with case folded (see fold_case), kept so searches don't refold it per file.
const std::string& tag::get_folded_id() const
{
    return folded;
}

const std::string& tag::get_search_name() const
{
    return search_name;
}

std::int64_t tag::get_db_id() const
{
    return db_id;
}

void tag::set_db_id(std::int64_t id)
{
    this->db_id = id;
}

std::vector<std::string> tag::paths_to_files() const
{
    std::vector<std::string> out;
    for (const file* a : this->has_tag)
    { 
        out.push_back(a->get_path());
    }
    return out;
}

const std::vector<file*>& tag::get_files() const {return this->has_tag;}

void tag::add_file(file* f)
{
    f->add_tag(this);
}

void tag::remove_file(file* f)
{
    auto it = std::find(this->has_tag.begin(), this->has_tag.end(), f);
    if(it != this->has_tag.end())
    {
        this->has_tag.erase(it);
        f->remove_tag_unreciprocated(this);
    }
}

void tag::remove_file_unreciprocated(const file* f)
{
    auto it = std::find(this->has_tag.begin(), this->has_tag.end(), f);
    if(it != this->has_tag.end())
    {
        this->has_tag.erase(it);
    }
}

void tag::unlink_from_files()
{
    for(file* f : this->has_tag)
    {
        f->remove_tag_unreciprocated(this);
    }
    this->has_tag.clear();
}

tag::~tag()
{
    unlink_from_files();
}