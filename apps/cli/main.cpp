#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
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
#include "../../include/folder_import.hpp"
#include "../../include/library.hpp"
#include "../../include/text.hpp"

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

/// @brief Loads the library again from the database, after a rollback left memory out of step with it.
static bool reload(library &a, database &db)
{
    a.attach(nullptr);
    a.clear();
    return open_library(a, db, "");
}

/// @brief Prints where a file was left: "page 13 of 200, read 2026-10-22 23:40".
static void print_progress(const file* f)
{
    const reading_progress *p = f->get_progress();
    std::cout << f->get_path() << ": page " << p->position + 1;
    if(p->length > 0)
        std::cout << " of " << p->length;
    if(!p->location.empty())
        std::cout << " (" << p->location << ")";
    std::time_t when = static_cast<std::time_t>(p->last_read);
    std::tm *local = std::localtime(&when);
    if(local)
        std::cout << ", read " << std::put_time(local, "%Y-%m-%d %H:%M");
    std::cout << '\n';
}

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
        std::cout << "1. View all files\n";
        std::cout << "2. Add Tag To File\n";
        std::cout << "3. Remove Tag from File\n";
        std::cout << "4. Rename tag\n";
        std::cout << "5. Search\n";
        std::cout << "6. Remove file\n";
        std::cout << "7. Delete tag\n";
        std::cout << "8. List missing files\n";
        std::cout << "9. Find moved files in a folder\n";
        std::cout << "10. Relink a file to a new path\n";
        std::cout << "11. Import a folder\n";
        std::cout << "12. Reading progress\n";
        std::cout << "13. Tag or untag every file a search finds\n";
        std::cout << "14. Quit\n";
        std::cin >> input;

        if(input == 1)
        {
            for(const file* f : a.get_files())
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
            file* f = a.add_file(fi);
            tag* t = f ? a.retrieve_tag(ta) : nullptr;
            if(t && a.edit_file(f, t, 1))
                std::cout << "Added tag " << t->get_id() << " to file " << fi << '\n';
        }
        else if(input == 3)
        {
            std::cout << "Which file?\n";
            std::string fi;
            std::string ta;
            std::cin >> fi;

            std::cout << '\n';
            file* f = a.find_file(fi);
            if(!f)
            {
                std::cout << "No such file.\n";
                continue;
            }

            std::cout << "Included tags in this file: ";
            
            for(tag* t : f->get_tags())
            {
                std::cout << t->get_id() << " ";
            }
            
            std::cout << "\nChoose a tag to remove or \"Cancel\" to stop the action.\n";
            
            std::cin >> ta;
            std::cout << '\n';
            tag* t = a.find_tag(ta);
            if(ta != "Cancel" && t)
            {
                a.edit_file(f, t, 0);
            }
        }
        else if(input == 4)
        {
            std::cout << "Input:\n {tag} {new_name}\n";
            std::string ta;
            std::string name;
            std::cin >> ta >> name;
            tag* t = a.find_tag(ta);
            tag* now = nullptr;
            if(!t)
                std::cout << "No such tag.\n";
            else switch(a.rename_tag(t, name, &now))
            {
                case rename_result::Renamed:
                    std::cout << "Renamed " << ta << " to " << name << '\n';
                    break;
                case rename_result::Merged:
                    std::cout << ta << " merged into " << now->get_id() << ", which has " << now->get_files().size() << " files now.\n";
                    break;
                case rename_result::Unchanged:
                    std::cout << "The tag is already called " << name << ".\n";
                    break;
                case rename_result::Invalid:
                    std::cout << name << " is not a valid tag name (no spaces or {}(), not starting with -).\n";
                    break;
                case rename_result::Failed:
                    std::cout << "Could not rename: " << db.last_error() << '\n';
                    db.clear_error();
                    break;
            }
        }
        else if(input == 5)
        {
            std::cout << "Query (e.g. jpg{image AND -icon}): ";
            std::string query;
            std::getline(std::cin >> std::ws, query);
            std::cout << "Look in (all, untagged, missing) [all]: ";
            std::string in;
            std::getline(std::cin, in);
            in = fold_case(in);
            LibraryView view = in == "untagged" ? LibraryView::Untagged : in == "missing" ? LibraryView::Missing : LibraryView::All;
            if(view == LibraryView::All && !in.empty() && in != "all")
                std::cout << "Unknown view, searching all.\n";
            SearchResult result = a.search(query, view);
            if(result.error == SearchError::UnclosedBrace)
                std::cout << "Malformed query: unclosed brace.\n";
            else if(result.error == SearchError::BadExpression)
                std::cout << "Malformed query: incomplete tag expression.\n";
            for(const file* f : result.files)
            {
                std::cout << f->get_path() << "\n";
            }
        }
        else if(input == 6)
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
        else if(input == 7)
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
        else if(input == 14 || !std::cin)
        {
            break;
        }
        else if(input == 8)
        {
            // Files stored before identities were kept get one now, while they are still where they were.
            int recorded = 0;
            if(db.begin())
            {
                recorded = a.record_identities();
                if(!db.last_error().empty() || !db.commit())
                {
                    std::cout << "Could not record identities: " << db.last_error() << '\n';
                    db.clear_error();
                    db.rollback();
                    recorded = 0;
                    // The identities set before the failure are gone from the database, so drop them from memory too.
                    if(!reload(a, db))
                    {
                        std::cout << "Could not reload " << db_file << ": " << db.last_error() << '\n';
                        return 1;
                    }
                }
            }
            if(recorded)
                std::cout << "Updated the identity of " << recorded << " files.\n";
            std::vector<file*> missing = a.missing_files();
            for(const file* f : missing)
            {
                std::cout << f->get_path() << (f->get_identity().empty() ? " (can't be found again)" : "") << '\n';
            }
            std::cout << missing.size() << " missing files.\n";
        }
        else if(input == 9)
        {
            std::cout << "Folder to search: ";
            std::string folder;
            std::getline(std::cin >> std::ws, folder);
            std::error_code scan_error;
            std::vector<moved_file> moved = a.find_moved(folder, &scan_error);
            if(scan_error == std::errc::cross_device_link)
                std::cout << "None of the missing files can be on that drive.\n";
            else if(scan_error)
                std::cout << "The search stopped early: " << scan_error.message() << '\n';
            for(const moved_file &m : moved)
            {
                std::cout << m.f->get_path() << " -> " << m.new_path << '\n';
            }
            std::cout << moved.size() << " moved files found.\n";
            if(!moved.empty())
            {
                std::cout << "Relink them? (y/n) ";
                std::string answer;
                std::cin >> answer;
                if(answer == "y" && db.begin())
                {
                    // All or nothing, like a drop in the GUI.
                    for(const moved_file &m : moved)
                        a.relink(m.f, m.new_path);
                    if(db.last_error().empty() && db.commit())
                        std::cout << "Relinked " << moved.size() << " files.\n";
                    else
                    {
                        std::cout << "Could not relink: " << db.last_error() << '\n';
                        db.clear_error();
                        db.rollback();
                        if(!reload(a, db))
                        {
                            std::cout << "Could not reload " << db_file << ": " << db.last_error() << '\n';
                            return 1;
                        }
                    }
                }
            }
        }
        else if(input == 10)
        {
            std::cout << "File to relink: ";
            std::string fi;
            std::getline(std::cin >> std::ws, fi);
            std::cout << "Its new path: ";
            std::string to;
            std::getline(std::cin >> std::ws, to);
            file* f = a.find_file(fi);
            if(!f)
                std::cout << "No such file.\n";
            else if(a.find_file(to))
                std::cout << to << " is already in the library.\n";
            else if(a.relink(f, to))
                std::cout << "Relinked " << fi << " to " << to << '\n';
            else
            {
                std::cout << "Could not relink " << fi << ": " << db.last_error() << '\n';
                db.clear_error();
            }
        }
        else if(input == 12)
        {
            std::vector<file*> read = a.recently_read();
            for(const file* f : read)
                print_progress(f);
            std::cout << read.size() << " files with reading progress.\n";
            std::cout << "1. Set a file's page\n";
            std::cout << "2. Clear a file's progress\n";
            std::cout << "3. Back\n";
            std::string choice;
            std::cin >> choice;
            if(choice == "1" || choice == "2")
            {
                std::cout << "File: ";
                std::string fi;
                std::getline(std::cin >> std::ws, fi);
                file* f = a.find_file(fi);
                if(!f)
                    std::cout << "No such file.\n";
                else if(choice == "2")
                {
                    if(a.clear_progress(f))
                        std::cout << "Cleared the progress of " << fi << '\n';
                    else
                    {
                        std::cout << "Could not clear the progress of " << fi << ": " << db.last_error() << '\n';
                        db.clear_error();
                    }
                }
                else
                {
                    std::cout << "Page (from 1) and page count (0 if unknown): ";
                    long long page = 0, count = 0;
                    std::cin >> page >> count;
                    // Checked before page - 1, which would overflow on the smallest long long.
                    bool in_range = std::cin && page >= 1;
                    reading_progress p;
                    p.position = in_range ? page - 1 : 0;
                    p.length = count;
                    // An EPUB resumes at its location, which a page number can't replace: it stays.
                    if(f->get_progress())
                        p.location = f->get_progress()->location;
                    if(!std::cin)
                    {
                        std::cin.clear();
                        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                        std::cout << "Not a number.\n";
                    }
                    else if(!in_range)
                        std::cout << "The page must be from 1 to the page count.\n";
                    else if(a.set_progress(f, p))
                        print_progress(f);
                    else if(!db.last_error().empty())
                    {
                        std::cout << "Could not save the progress of " << fi << ": " << db.last_error() << '\n';
                        db.clear_error();
                    }
                    else
                        std::cout << "The page must be from 1 to the page count.\n";
                }
            }
        }
        else if(input == 11)
        {
            std::cout << "Folder to import: ";
            std::string folder;
            std::getline(std::cin >> std::ws, folder);
            folder_scan scan = scan_folder(folder, nullptr, [](std::size_t found, const std::string &)
            {
                if(found % 100 == 0)
                    std::cout << '\r' << found << " files found" << std::flush;
            });
            std::cout << '\r' << scan.files.size() << " files found\n";
            if(scan.error)
                std::cout << "Could not read " << folder << ": " << scan.error.message() << '\n';
            if(scan.unreadable)
                std::cout << "Skipped " << scan.unreadable << " folders that could not be read.\n";
            if(scan.skipped)
                std::cout << "Skipped " << scan.skipped << " system files (desktop.ini, Thumbs.db and the like).\n";
            int added = a.add_files(scan.files);
            if(added < 0)
            {
                std::cout << "Could not import: " << db.last_error() << '\n';
                db.clear_error();
            }
            else
            {
                std::cout << "Added " << added << " files, " << scan.files.size() - added << " were already in the library.\n";
                // Folder names as tags, each one confirmed.
                for(const folder_tag_suggestion &s : suggest_folder_tags(folder, scan.files))
                {
                    std::cout << "Tag " << s.files.size() << " files \"" << s.name << "\"? (y/N) ";
                    std::string yes;
                    std::getline(std::cin >> std::ws, yes);
                    if(yes != "y" && yes != "Y")
                        continue;
                    std::vector<file*> targets;
                    for(std::size_t i : s.files)
                        if(file* f = a.find_file(scan.files[i].path))
                            targets.push_back(f);
                    // No tag is made for no file.
                    if(targets.empty())
                        continue;
                    tag* t = a.retrieve_tag(s.name);
                    if(!t || a.edit_files(targets, {t}, true) < 0)
                    {
                        std::cout << "Could not tag: " << db.last_error() << '\n';
                        db.clear_error();
                    }
                }
            }
        }
        else if(input == 13)
        {
            std::cout << "Search (e.g. jpg{holiday}): ";
            std::string query;
            std::getline(std::cin >> std::ws, query);
            SearchResult found = a.search(query);
            if(!found.valid())
                std::cout << "That search is not valid.\n";
            else
            {
                std::cout << found.files.size() << " files found. {tag} {add|remove}: ";
                std::string name, what;
                std::cin >> name >> what;
                if(!is_valid_tag_name(name) || (what != "add" && what != "remove"))
                    std::cout << "Need a valid tag name and add or remove.\n";
                else
                {
                    tag* t = what == "add" ? a.retrieve_tag(name) : a.find_tag(name);
                    int changed = t ? a.edit_files(found.files, {t}, what == "add") : 0;
                    if(changed < 0)
                    {
                        std::cout << "Could not save: " << db.last_error() << '\n';
                        db.clear_error();
                    }
                    else
                        std::cout << changed << " files changed.\n";
                }
            }
        }
        if(!db.last_error().empty())
        {
            std::cout << "Database error: " << db.last_error() << '\n';
            db.clear_error();
        }
    }
    return 0;
}