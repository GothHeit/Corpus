#include "../include/saving.hpp"
#include "../include/database.hpp"
#include "../include/tag.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"

#include <algorithm>
#include <string>
#include <fstream>

static std::string unescape(const std::string &input)
{
    std::string out;
    bool escapeMode = false;
    for (const char c : input)
    { 
        if(escapeMode)
        {
            if(c == '\\')
            {
                out += '\\';
            }
            else if(c== '\"')
            {
                out += '\"';
            }    
            else
            {
                out += "\\";
                out += c;
            }
            escapeMode = false;
        }

        else
        {
            if(c == '\\')
                escapeMode = true;
            else
                out += c;

        }
    }
    return out;
}

/// Splits a line on the delimiters (a space by default).
static std::vector<std::string> split_tag_line(const std::string &input, const std::string &delimiters="\""){
        std::vector<std::string> out{};
        std::string buff{};
        
        bool between_tags = false;
        
        int size = input.size();
        for(int i=0; i<size; ++i)
            {
                if(!between_tags && (delimiters.find(input[i])==std::string::npos || input[i-1] == '\\'))
                {
                    buff+=input[i];        
                    if(i==size-1)
                        out.push_back(unescape(buff));
                }
                else
                {
                    if(between_tags && input[i] == '\"')
                    {
                        between_tags  = false;
                        continue;
                    }
                    between_tags = true;

                    if(buff.size() > 0)
                        out.push_back(unescape(buff));
                    buff.clear();
                }
            }
        return out;
}

/// Reads a JSON library (the format older versions saved) into lib, writing it to the database if one is attached.
/// False if the file can't be opened.
bool load_library(library &lib, const std::filesystem::path &filename)
{
    std::ifstream in(filename);
    if(!in)
        return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    
    int pos = 0;
    while (true)
    {
        pos = text.find("\"path\":", pos);
        if(pos == std::string::npos)
        {
            break;
        }
        int startpos = text.find("\"", pos+6);
        int endpos = text.find("\n",startpos);
        endpos = text.rfind("\"", endpos); 
        std::string path;

        if(endpos - startpos > 1)
            path = unescape( text.substr(startpos+1, endpos - startpos - 1));
        else
            path = "None";

        file* f = lib.add_file(path);
        if(!f)
        {
            return true; // the database failed: its last_error says why, and open_library rolls back
        }

        pos = text.find("\"tags\":", endpos);
        startpos = text.find("[", pos);
        endpos = text.find("\n", startpos);
        endpos = text.rfind("]", endpos);
        
        pos = endpos;

        std::vector<std::string> tags = split_tag_line(text.substr( startpos+2, endpos - startpos - 3 ));

        for(const std::string &t : tags)
        {
            tag* found = lib.retrieve_tag(t);
            if(!found || !lib.edit_file(f, found, 1))
            {
                return true; // as above
            }
        }
    }
    return true;
}

/// Fills an empty library from an open database and attaches it. The first time (no tables yet) it creates them
/// and imports the JSON file in one transaction, so a crash halfway leaves the database new and the import reruns.
/// An import file that can't be opened commits nothing. On failure the library is left empty and detached, so
/// open_library can simply be called again. See database::last_error.
bool open_library(library &lib, database &db, const std::filesystem::path &import_filename)
{
    lib.attach(nullptr);
    if(!db.is_new())
    {
        if(!db.load(lib))
        {
            lib.clear();
            return false;
        }
        lib.attach(&db);
        return true;
    }

    if(!db.begin())
    {
        return false;
    }
    db.clear_error();
    if(!db.create_schema())
    {
        db.rollback();
        return false;
    }
    lib.attach(&db);
    if(!import_filename.empty() && !load_library(lib, import_filename))
    {
        db.rollback();
        lib.attach(nullptr);
        lib.clear();
        db.set_error("could not open " + import_filename.u8string());
        return false;
    }
    if(!db.last_error().empty() || !db.commit())
    {
        db.rollback();
        lib.attach(nullptr);
        lib.clear();
        return false;
    }
    return true;
}