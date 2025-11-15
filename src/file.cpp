#include <string>
#include <vector>
#include <algorithm>

#include "../include/file.hpp"
#include "../include/tag.hpp"

    file::file(const std::string &patt) : path(patt) {}


    /// @brief Changes the path for the file. (Doesn't move the file)
    /// @param a New path to be registered.
    void file::set_path(const std::string &a){this->path = a;}

    /// @brief Gets the path to the file.
    /// @return A string with the path to the file.
    std::string file::get_path() const {return this->path;}

    /// @brief Adds a tag to the file.
    /// @param t Tag to be added.
    void file::add_tag(tag* t)
    {
        for(auto& ta : this->tags)
        {
            if(ta->get_id() == t->get_id())
            {
                return;
            }
        }            
        this->tags.push_back(t);
        t->add_file(this);
    }
    
    /// @brief Gets the tags this file includes.
    /// @return A vector of pointers to the tags this file includes.
    std::vector<tag*> file::get_tags() const
    {
        return tags;
    }
    
    /// @brief Verifies if the file contains a specific tag.
    /// @param t The tag to be checked.
    /// @return A boolean describing whether the file contains said tag.
    bool file::tagged(const tag* t) const
    {
        for(tag* ta : this->tags)
        {
            if(ta == t){return true;}
        }
        return false;
    }
    
    /// @brief Compares the file to another based on the path.
    /// @param b File to be compared with.
    /// @return A boolean describing whether the files are the same.
    bool file::operator==(const file &b) const
    {
        return this->get_path() == b.get_path();
    }
    
    /// @brief Compares the file to another based on the path.
    /// @param b File to be compared with.
    /// @return A boolean describing whether the files are different.
    bool file::operator!=(const file &b) const
    {
        return !(*this==b);
    }
    
    
    
    /// @brief Removes a tag from the file. (And calls the remotion of the file from the tag)
    /// @param t Tag to be removed from the file.
    void file::remove_tag(tag* t)
    {
        for(size_t i=0; i< this->tags.size(); ++i)
        {
            if(this->tags[i]->get_id() == t->get_id())
            {
                this->tags.erase(this->tags.begin() + i);
                t->remove_file_unreciprocated(this);
                return;
            }
        }
    }

    
    void file::unlink_from_tags()
    {
        for(tag* t : this->get_tags())
        {
            t->remove_file(this);
        }
        this->tags.clear();
    }
    
    void file::remove_tag_unreciprocated(tag* t)
    {
        for(int i=0; i<this->tags.size(); ++i)
        {
            if(this->tags[i] == t)
            {
                this->tags.erase(this->tags.begin() + i);
                return;
            }
        }  
    }
    


    file::~file()
    {
        unlink_from_tags();
    }