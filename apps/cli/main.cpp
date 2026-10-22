#include <iostream>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "../../include/database.hpp"
#include "../../include/saving.hpp"
#include "../../include/tag.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"

#ifdef _WIN32
/// @brief The command line arguments in UTF-8. Windows gives main() its arguments in the ANSI code page,
/// which can't hold every file name, so this reads the UTF-16 command line instead.
static std::vector<std::string> utf8_arguments()
{
    std::vector<std::string> args;
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if(!wide)
        return args;
    for(int i = 0; i < count; i++)
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size > 0 ? size - 1 : 0, '\0');
        if(size > 1)
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, &arg[0], size, nullptr, nullptr);
        args.push_back(arg);
    }
    LocalFree(wide);
    return args;
}

/// @brief Switches the console to UTF-8 and puts its code pages back on exit. The console window is shared,
/// so without that the programs run after the CLI in the same window would print garbled text.
struct utf8_console
{
    UINT input = GetConsoleCP();
    UINT output = GetConsoleOutputCP();

    utf8_console()
    {
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }
    ~utf8_console()
    {
        std::cout.flush();
        SetConsoleCP(input);
        SetConsoleOutputCP(output);
    }
};
#endif

int main(int argc, char* argv[])
{
    // Every string in the CLI is UTF-8, like the paths and tag names the library stores.
#ifdef _WIN32
    // The console otherwise reads and writes the OEM code page.
    utf8_console console;
    std::vector<std::string> args = utf8_arguments();
    (void)argc;
    (void)argv;
#else
    std::vector<std::string> args(argv, argv + argc);
#endif

    std::filesystem::path existing_libs = "libs/";
    std::string db_file = args.size() > 1 ? args[1] : (existing_libs / "corpus.db").u8string();
    // u8path reads the string as UTF-8. path(std::string) would use the native narrow encoding.
    std::filesystem::path db_path = std::filesystem::u8path(db_file);

    std::error_code ec;
    std::filesystem::create_directories(db_path.parent_path(), ec);

    // Declared before the library so it is closed after it.
    database db;
    library a;
    if(!db.open(db_path))
    {
        std::cout << "Could not open " << db_file << ": " << db.last_error() << '\n';
        return 1;
    }

    std::filesystem::path import_file;
    if(db.is_new())
    {
        std::cout << db_file << " is new. Import a JSON library into it?\n";
        std::cout << "0. Start empty\n";
        int count=0;
        std::vector<std::filesystem::path> files;
        for(const std::filesystem::directory_entry &e : std::filesystem::directory_iterator(existing_libs, ec))
        {
            if(e.path().extension() != ".json")
                continue;
            count++;
            std::cout << count << ". " << e.path().u8string() << '\n';
            files.push_back(e.path());
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
        std::cout << "8. Remove file\n";
        std::cout << "9. Delete tag\n";
        std::cout << "10. Quit\n";
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
            tag* t = a.find_tag(ta);
            if(!t)
                std::cout << "No such tag.\n";
            else if(a.edit_tag(t, name))
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
            save_library(a, std::filesystem::u8path(out));
            std::cout << "Exported to " << out << '\n';
        }
        else if(input == 8)
        {
            std::cout << "File to remove: ";
            std::string fi;
            std::getline(std::cin >> std::ws, fi);
            file* f = a.find_file(fi);
            if(!f)
                std::cout << "No such file.\n";
            else if(a.remove_file(f))
                std::cout << "Removed " << fi << '\n';
            else
            {
                std::cout << "Could not remove " << fi << ": " << db.last_error() << '\n';
                db.clear_error();
            }
        }
        else if(input == 9)
        {
            std::cout << "Tag to delete: ";
            std::string ta;
            std::cin >> ta;
            tag* t = a.find_tag(ta);
            if(!t)
                std::cout << "No such tag.\n";
            else if(a.delete_tag(t))
                std::cout << "Deleted " << ta << '\n';
            else
            {
                std::cout << "Could not delete " << ta << ": " << db.last_error() << '\n';
                db.clear_error();
            }
        }
        else if(input == 10 || !std::cin)
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