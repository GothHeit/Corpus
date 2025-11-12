#include <string>
#include <vector>
#include <algorithm>

#include "../include/file.hpp"
#include "../include/tag.hpp"

    tag::tag(const std::string &id){this->id = id;}
    
    /// @brief Gets the files this tag contains as paths.
    /// @return A vector of strings with the paths to such files.
    std::vector<std::string> tag::paths_to_files() const
    {
        std::vector<std::string> out;
        for (auto& a : this->has_tag)
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
        for(auto& fi : this->has_tag)
        {
            if(fi->get_path() == f->get_path())
            {
                return;
            }
        }
        this->has_tag.push_back(f);
    }

    /// @brief Removes a file from this tag.
    /// @param f File to be removed.
    void tag::remove_file(const file* f)
    {
        for(int i=0; i<this->has_tag.size(); ++i)
        {
            if(this->has_tag[i]->get_path() == f->get_path())
            {
                this->has_tag.erase(this->has_tag.begin() + i);
                return;
            }
        }  
    }
