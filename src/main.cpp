#include <iostream>
#include <filesystem>

#include "../include/saving.hpp"
#include "../include/tag.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"


int main(int argc, char* argv[])
{
    library a;
    std::string existing_libs = "libs/";
    std::string lib_file;
    std::cout << "1. Create a new library from scratch\n";
    std::cout << "2. Open an existing library\n";

    {
        std::string inp;
        std::cin >> inp;
        if(inp == "1")
        {
                std::cout << "How should your library be called?\n";
                std::cin >> lib_file;
                lib_file = existing_libs + lib_file + ".json";
        }
        if(inp == "2")
        {
            std::cout << "\n";
            int count=0;
            std::vector<std::string> files;
            for(const std::filesystem::directory_entry &a : std::filesystem::directory_iterator(existing_libs))
            {
                count++;
                std::cout << count << ". " << a.path() << '\n';
                files.push_back(a.path());
            }
            std::cout << "Input the index: ";
            std::cin >> inp;
            {
                int idx = std::stoi(inp);
                if(idx < 1 || idx > files.size())
                {
                    std::cout << "Invalid Index.\n";
                }
                load_library(a, files[idx-1]);
                lib_file = files[idx-1];
            }

        }
    }

    int input;
    while (true)
    {
        std::cout << "1. View Current Files\n";
        std::cout << "2. Add Tag To File\n";
        std::cout << "3. Remove Tag from File\n";
        std::cout << "4. Change tag filter\n";
        std::cout << "5. Quit\n";
        std::cin >> input;

        if(input == 1)
        {
            for(const file* f : a.show())
            {
                std::cout << f->get_path() << "\n";
            }
        }
        else if(input == 2)
        {
            std::cout << "Input:\n {file_path} {tag}\n";
            std::string fi;
            std::string ta;
            std::cin >> fi >> ta;
            a.edit_file(a.add_file(fi), a.retrieve_tag(ta), 1);
            std::cout << "Added tag " << ta << " to file " << fi << '\n';
        }
        else if(input == 3)
        {
            std::cout << "Which file?\n";
            std::string fi;
            std::string ta;
            std::cin >> fi;

            std::cout << '\n';

            std::cout << "Included tags in this file: ";
            
            for(tag* t : a.add_file(fi)->get_tags())
            {
                std::cout << t->get_id() << " ";
            }
            
            std::cout << "\nChoose a tag to remove or \"Cancel\" to stop the action.\n";
            
            std::cin >> ta;
            std::cout << '\n';
            if(ta != "Cancel")
            {
                a.edit_file(a.add_file(fi), a.retrieve_tag(ta), 0);
            }
        }
        else if(input == 4)
        {
            std::string ta;
            bool add;
            std::cout << "current filter: ";
            for(tag* t : a.current_filter())
            {
                std::cout << t->get_id() << " ";   
            }
            std::cout << "\ndo you wish to\n";
            std::cout << "1. add a tag\n";
            std::cout << "2. remove a tag\n";
            std::cout << "3. cancel action\n";

            std::cin >> ta;
            std::cout << '\n';

            if(ta == "1")
            {
                std::cout << "tag to be added to filter: ";
                std::cin >> ta;
                a.filter_files(a.retrieve_tag(ta), 1);
            }
            else if(ta == "2")
            {
                std::cout << "tag to be removed from filter: ";
                std::cin >> ta;
                a.filter_files(a.retrieve_tag(ta), 0);
            }
            std::cout << '\n';
        }
        else if(input == 5)
        {
            break;
        }
    }
    save_library(a, lib_file);
    return 0;
}