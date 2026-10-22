#include "../include/saving.hpp"
#include "../include/database.hpp"
#include "../include/tag.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"

#include <algorithm>
#include <string>
#include <fstream>

static std::string escape(const std::string &input)
{
    std::string out;

    for(const char c : input)
    {
        if(c == '\"') // (") becomes 
            out += "\\\""; // (\")
        
        else if(c == '\\') // (\) becomes
            out += "\\\\"; // (\\)
        else
            out += c;
    }
    return out;
}

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

///@brief Gets a string input (most commonly a line of input) and splits it into a vector of strings
///@param input The line to be split
///@param delimiters the paramater to split the line (a space by default)
///@return A vector of strings with the input minus the delimiter parameter
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

void save_library(const library &lib, const std::string &filename)
{
    std::ofstream out(filename);  
    if(!out)
    {
        return;
    }   
    
    
    out << "{\n";
    out << "    \"files\": [\n";
    bool firstfile = true;
    for(const file* f : lib.get_files())
    {        
        if(!firstfile)
        {
            out << ",\n";
        }
        out << "      {\n";
        out << "        \"path\": \"";
        out << escape(f->get_path());
        out << "\",\n";
        out << "        \"tags\": [";
        
        bool firsttag = true;
        
        for(const tag* t : f->get_tags())
        {
            if(!firsttag)
            {
                out << ", ";
            }
            firsttag = false;
            out << "\"";
            out << escape(t->get_id()) << "\"";
        }
        
        out << "]\n";
        firstfile = false;
        out << "      }";
    }
    
    out << "\n";
    out << "    ]\n";
    out << "}";
    
    out.flush();
    out.close();
}

/// @brief Reads a JSON library into lib. If lib has a database attached, everything read is written to it too.
/// @param filename Path to the JSON file.
/// @return A boolean describing whether the file could be opened.
bool load_library(library &lib, const std::string &filename)
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

        pos = text.find("\"tags\":", endpos);
        startpos = text.find("[", pos);
        endpos = text.find("\n", startpos);
        endpos = text.rfind("]", endpos);
        
        pos = endpos;

        std::vector<std::string> tags = split_tag_line(text.substr( startpos+2, endpos - startpos - 3 ));

        for(const std::string t : tags)
        {            
            lib.edit_file(f, lib.retrieve_tag(t), 1);
        }
    }
    return true;
}

/// @brief Fills an empty library from an open database and attaches it, so later changes are saved right away.
/// The first time (the database has no tables yet) it creates them and imports the JSON file instead,
/// all in one transaction: a crash halfway leaves the database new, and the import runs again next time.
/// @param lib An empty library.
/// @param db A database already opened with database::open.
/// @param import_filename JSON library to import on the first run, or empty to start empty.
/// If it can't be opened nothing is committed, so the database stays new.
/// @return A boolean describing whether the library could be loaded (see database::last_error).
/// On failure the library is left empty and detached, and a failed first import leaves the database new,
/// so open_library can simply be called again.
bool open_library(library &lib, database &db, const std::string &import_filename)
{
    lib.attach(nullptr);
    if(!db.is_new())
    {
        if(!db.load(lib))
        {
            lib.clear();
            return false;
        }
        lib.update_seen_files();
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
        db.set_error("could not open " + import_filename);
        return false;
    }
    if(!db.last_error().empty() || !db.commit())
    {
        db.rollback();
        lib.attach(nullptr);
        lib.clear();
        return false;
    }
    lib.update_seen_files();
    return true;
}