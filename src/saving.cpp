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

    for(char c : input)
    {
        if(c == '\"') // (") vira 
            out += "\\\""; // (\")
        
        else if(c == '\\') // (\) vira
            out += "\\\\"; // (\\)
        else
            out += c;
    }
    return out;
}

static std::string unescape(const std::string &input)
{
    std::string out;
    bool barramode = false;
    for (char c : input)
    { 
        if(barramode)
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
            barramode = false;
        }

        else
        {
            if(c == '\\')
                barramode = true;
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
std::vector<std::string> split_tag_line(std::string input, std::string delimiters="\""){
        std::vector<std::string> out{};
        std::string calma{};
        
        bool between_tags = false;
        
        int size = input.size();
        for(int i=0; i<size; ++i)
            {
            if(!between_tags && (delimiters.find(input[i])==std::string::npos || input[i-1] == '\\'))
            {
                calma+=input[i];        
                if(i==size-1)
                   out.push_back(unescape(calma));
            }
            else
            {
                if(between_tags && input[i] == '\"')
                {
                    between_tags  = false;
                    continue;
                }
                between_tags = true;

                if(calma.size() > 0)
                out.push_back(unescape(calma));
                calma.clear();
            
            }
        }
        return out;
}

void save_library(const library &lib, const std::string &filename)
{
    std::ofstream out(filename);
    if(!out)
        return;
    
    out << "{\n";
    out << "    \"files\": [\n";
    bool firstfile = true;
    for(auto &f : lib.get_files())
    {        
        if(!firstfile)
        {
            out << ",\n";
        }
        out << "    {\n";
        out << "      \"path\": \"";
        out << escape(f->get_path());
        out << "\",\n";
        out << "      \"tags\": [";
        bool firsttag = true;
        for(auto &t : f->get_tags())
        {
            if(!firsttag)
            {
                out << ", ";
            }
            firsttag = false;
            out << "\"";
            out << escape(t->id) << "\"";
        }
        out << "]\n";
        firstfile = false;
        out << "    }";
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
        int posinicio = text.find("\"", pos+6);
        int posfim = text.find("\n",posinicio);
        posfim = text.rfind("\"", posfim); 
        std::string path;

        if(posfim - posinicio > 1)
            path = unescape( text.substr(posinicio+1, posfim - posinicio - 1));
        else
            path = "None";

        file* f = lib.add_file(path);



        pos = text.find("\"tags\":", posfim);
        posinicio = text.find("[", pos);
        posfim = text.find("\n", posinicio);
        posfim = text.rfind("]", posfim);
        
        pos = posfim;

        std::vector<std::string> tags = split_tag_line(text.substr( posinicio+2, posfim - posinicio - 3 ));

        for(std::string t : tags)
        {            
            lib.edit_file(f, lib.retrieve_tag(t), 1);
        }

    }
    

  

}