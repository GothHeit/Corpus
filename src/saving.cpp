#include "../include/saving.hpp"
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

void load_library(library &lib, const std::string &filename)
{
    std::ifstream in(filename);
    if(!in)
        return;
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
}