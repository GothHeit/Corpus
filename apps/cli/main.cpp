#include <iostream>
#include <filesystem>

#include "../../include/database.hpp"
#include "../../include/saving.hpp"
#include "../../include/tag.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"


int main(int argc, char* argv[])
{
    std::string existing_libs = "libs/";
    std::string db_file = argc > 1 ? argv[1] : existing_libs + "corpus.db";

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(db_file).parent_path(), ec);

    // Declared before the library so it is closed after it.
    database db;
    library a;
    if(!db.open(db_file))
    {
        std::cout << "Could not open " << db_file << ": " << db.last_error() << '\n';
        return 1;
    }

    std::string import_file;
    if(db.is_new())
    {
        std::cout << db_file << " is new. Import a JSON library into it?\n";
        std::cout << "0. Start empty\n";
        int count=0;
        std::vector<std::string> files;
        for(const std::filesystem::directory_entry &e : std::filesystem::directory_iterator(existing_libs, ec))
        {
            if(e.path().extension() != ".json")
                continue;
            count++;
            std::cout << count << ". " << e.path() << '\n';
            files.push_back(e.path().string());
        }
        std::cout << "Input the index: ";
        std::string inp;
        std::cin >> inp;
        int idx = 0;
        try { idx = std::stoi(inp); } catch(...) {}
        if(idx >= 1 && idx <= static_cast<int>(files.size()))
        {
            import_file = files[idx-1];
        }
        else if(idx != 0)
        {
            std::cout << "Invalid Index, starting empty.\n";
        }
    }

    if(!open_library(a, db, import_file))
    {
        std::cout << "Could not load " << db_file << ": " << db.last_error() << '\n';
        return 1;
    }
    std::cout << a.get_files().size() << " files, " << a.get_tags().size() << " tags.\n";

    int input;
    while (true)
    {
        std::cout << "1. View Current Files\n";
        std::cout << "2. Add Tag To File\n";
        std::cout << "3. Remove Tag from File\n";
        std::cout << "4. Change tag filter\n";
        std::cout << "5. Rename tag\n";
        std::cout << "6. Search\n";
        std::cout << "7. Export to JSON\n";
        std::cout << "8. Quit\n";
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
            std::cout << "Input:\n {tag} {new_name}\n";
            std::string ta;
            std::string name;
            std::cin >> ta >> name;
            if(a.edit_tag(a.retrieve_tag(ta), name))
                std::cout << "Renamed " << ta << " to " << name << '\n';
            else
                std::cout << "A tag called " << name << " already exists.\n";
        }
        else if(input == 6)
        {
            std::cout << "Query (e.g. jpg{image AND -icon}): ";
            std::string query;
            std::getline(std::cin >> std::ws, query);
            SearchResult result = a.search(query);
            if(!result.valid)
                std::cout << "Malformed query.\n";
            for(const file* f : result.files)
            {
                std::cout << f->get_path() << "\n";
            }
        }
        else if(input == 7)
        {
            std::cout << "Export to which file? ";
            std::string out;
            std::cin >> out;
            save_library(a, out);
            std::cout << "Exported to " << out << '\n';
        }
        else if(input == 8 || !std::cin)
        {
            break;
        }
        if(!db.last_error().empty())
        {
            std::cout << "Database error: " << db.last_error() << '\n';
            db.clear_error();
        }
    }
    return 0;
}