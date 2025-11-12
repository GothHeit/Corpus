#include <bits/stdc++.h>
#include "include/saving.hpp"
#include "include/tag.hpp"
#include "include/file.hpp"
#include "include/library.hpp"


int main(int argc, char* argv[])
{
    library a;

    if(argc > 1)
    {
        std::string file = std::string(argv[1]);
        std::cout << file << '\n';
        load_library(a, file);
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
            a.show();
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
            
            std::cout << "Included tags in this file: ";
            
            for(tag* t : a.add_file(fi)->get_tags())
            {
                std::cout << t->id << " ";
            }
            
            std::cout << "\nChoose a tag to remove or \"Cancel\" to stop the action.\n";
            
            std::cin >> ta;
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
                std::cout << t->id << " ";   
            }
            std::cout << "do you wish to\n";
            std::cout << "1. add a tag\n";
            std::cout << "2. remove a tag\n";
            std::cout << "3. cancel action\n";

            std::cin >> ta;

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
        }
        else if(input == 5)
        {
            break;
        }

    }

    save_library(a, "teste.json");


}