#include <string>
#include <vector>
#include <algorithm>

#include "../include/file.hpp"
#include "../include/tag.hpp"

    tag::tag(const std::string &id) : id(id) {}
    
    std::string tag::get_id() const
    {
        return id;
    }

    /// @brief Gets the files this tag contains as paths.
    /// @return A vector of strings with the paths to such files.
    std::vector<std::string> tag::paths_to_files() const
    {
        std::vector<std::string> out;
        for (const file* a : this->has_tag)
        { 
            out.push_back(a->get_path());
        }
        return out;
    }

    /// @brief Gets the files this tag contains.
    /// @return A vector of pointers to such files.
    std::vector<file*> tag::get_files() const {return this->has_tag;}

    /// @brief Adds a file to this tag.
    /// @param f File to be added.
    void tag::add_file(file* f)
    {
        for(const file* fi : this->has_tag)
        {
            if(fi == f)
            {
                return;
            }
        }
        this->has_tag.push_back(f);
    }

    /// @brief Removes a file from the tag (and the tag from the file).
    /// @param f File to be removed.
    void tag::remove_file(file* f)
    {
        for(int i=0; i<this->has_tag.size(); ++i)
        {
            if(this->has_tag[i] == f)
            {
                this->has_tag.erase(this->has_tag.begin() + i);
                f->remove_tag_unreciprocated(this);
                return;
            }
        }  
    }

    void tag::remove_file_unreciprocated(const file* f)
    {
        for(int i=0; i<this->has_tag.size(); ++i)
        {
            if(this->has_tag[i] == f)
            {
                this->has_tag.erase(this->has_tag.begin() + i);
                return;
            }
        }  
    }

    void tag::unlink_from_files()
    {
        for(file* f : this->get_files())
        {
            f->remove_tag(this);
        }   
    }

    tag::~tag()
    {
        unlink_from_files();
    }