#include <string>
#include <vector>
#include <algorithm>

#include "../include/file.hpp"
#include "../include/tag.hpp"
#include "../include/text.hpp"

file::file(const std::string &patt) : path(patt), search_path(fold_search(patt)) {}


void file::set_path(const std::string &a)
{
    this->path = a;
    this->search_path = fold_search(a);
}

const std::string& file::get_path() const {return this->path;}

const std::string& file::get_search_path() const {return this->search_path;}

std::int64_t file::get_db_id() const {return this->db_id;}

void file::set_db_id(std::int64_t id){this->db_id = id;}

const std::string& file::get_identity() const {return this->identity;}

void file::set_identity(const std::string &id){this->identity = id;}

const reading_progress* file::get_progress() const {return this->progress.get();}

/// Memory only: library::set_progress also saves it.
void file::set_progress(const reading_progress &p)
{
    if(this->progress)
        *this->progress = p;
    else
        this->progress.reset(new reading_progress(p));
}

void file::clear_progress(){this->progress.reset();}

void file::add_tag(tag* t)
{
    if(this->tagged(t))
    {
        return;
    }
    this->tags.push_back(t);
    t->has_tag.push_back(this); // this file wasn't tagged, so the tag can't list it yet
}

const std::vector<tag*>& file::get_tags() const
{
    return tags;
}

bool file::tagged(const tag* t) const
{
    for(tag* ta : this->tags)
    {
        if(ta == t){return true;}
    }
    return false;
}

bool file::operator==(const file &b) const
{
    return this->get_path() == b.get_path();
}

bool file::operator!=(const file &b) const
{
    return !(*this==b);
}



/// Also removes the file from the tag.
void file::remove_tag(tag* t)
{
    for(size_t i=0; i< this->tags.size(); ++i)
    {
        if(this->tags[i] == t)
        {
            this->tags.erase(this->tags.begin() + i);
            t->remove_file_unreciprocated(this);
            return;
        }
    }
}


void file::unlink_from_tags()
{
    for(tag* t : this->tags)
    {
        t->remove_file_unreciprocated(this);
    }
    this->tags.clear();
}

void file::remove_tag_unreciprocated(tag* t)
{
    auto it = std::find(this->tags.begin(), this->tags.end(), t);
    if(it != this->tags.end())
    {
        this->tags.erase(it);
    }
}



file::~file()
{
    unlink_from_tags();
}