// Storage round trip: import a JSON library into a new database, change it, reopen it and compare
// with the same changes made to a library kept in memory only.
//
// Usage: corpus-roundtrip-test [work_dir] [file_count]
// file_count > 0 also times importing, reopening and closing a generated library of that size.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "../../include/database.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/saving.hpp"
#include "../../include/tag.hpp"

typedef std::map<std::string, std::set<std::string>> snapshot;

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if(!ok)
    {
        std::cout << "FAIL: " << what << '\n';
        failures++;
    }
}

static snapshot take(const library &lib)
{
    snapshot out;
    for(const file* f : lib.get_files())
    {
        std::set<std::string> &tags = out[f->get_path()];
        for(const tag* t : f->get_tags())
        {
            tags.insert(t->get_id());
        }
    }
    // Tags with no files show up under an empty key.
    for(const tag* t : lib.get_tags())
    {
        if(t->get_files().empty())
        {
            out[""].insert(t->get_id());
        }
    }
    return out;
}

static std::set<std::string> visible(const library &lib)
{
    std::set<std::string> out;
    for(const file* f : lib.show())
    {
        out.insert(f->get_path());
    }
    return out;
}

static snapshot snapshot_of_json(const std::filesystem::path &path)
{
    library lib;
    load_library(lib, path);
    return take(lib);
}

static void remove_db(const std::string &path)
{
    std::filesystem::remove(path);
    std::filesystem::remove(path + "-wal");
    std::filesystem::remove(path + "-shm");
}

// The same edits, applied to any library.
static void mutate(library &lib)
{
    lib.edit_file(lib.add_file("new file.txt"), lib.retrieve_tag("fresh"), 1);
    lib.edit_file(lib.add_file("a.jpg"), lib.retrieve_tag("image"), 0);
    lib.edit_file(lib.add_file("a.jpg"), lib.retrieve_tag("fresh"), 1);
    lib.edit_tag(lib.retrieve_tag("music"), "songs");
    lib.create_tag("unused");
    lib.add_file("untagged.bin");
}

static void round_trip(const std::string &dir)
{
    const std::string json = dir + "/roundtrip.json";
    const std::string db_path = dir + "/roundtrip.db";
    remove_db(db_path);

    library expected;
    expected.edit_file(expected.add_file("a.jpg"), expected.retrieve_tag("image"), 1);
    expected.edit_file(expected.add_file("a.jpg"), expected.retrieve_tag("wallpaper"), 1);
    expected.edit_file(expected.add_file("b.png"), expected.retrieve_tag("image"), 1);
    expected.edit_file(expected.add_file("song \"live\".mp3"), expected.retrieve_tag("music"), 1);
    expected.edit_file(expected.add_file("C:\\Users\\me\\ação.pdf"), expected.retrieve_tag("document"), 1);
    save_library(expected, json);

    {
        database db;
        check(db.open(db_path), "open new database: " + db.last_error());
        check(db.is_new(), "new database reports is_new");
        library lib;
        check(open_library(lib, db, json), "import JSON: " + db.last_error());
        check(take(lib) == take(expected), "imported library matches the JSON");
        check(visible(lib).size() == expected.get_files().size(), "every imported file is visible");

        mutate(lib);
        mutate(expected);
        check(take(lib) == take(expected), "edits apply in memory");
        check(db.last_error().empty(), "edits write without errors: " + db.last_error());
    }

    {
        database db;
        check(db.open(db_path), "reopen database: " + db.last_error());
        check(!db.is_new(), "reopened database is not new");
        library lib;
        check(open_library(lib, db, json), "load database: " + db.last_error());
        check(take(lib) == take(expected), "reopened library matches the edits");
        check(visible(lib).size() == expected.get_files().size(), "every reopened file is visible");

        lib.filter_files(lib.retrieve_tag("fresh"), 1);
        check(visible(lib) == std::set<std::string>{"a.jpg", "new file.txt"}, "filter by tag");
        lib.edit_file(lib.add_file("b.png"), lib.retrieve_tag("fresh"), 1);
        check(visible(lib).count("b.png") == 1, "tagging updates a filtered view");
        lib.edit_file(lib.add_file("b.png"), lib.retrieve_tag("fresh"), 0);
        check(visible(lib).count("b.png") == 0, "untagging updates a filtered view");
        check(!lib.edit_tag(lib.retrieve_tag("songs"), "image"), "rename to an existing tag is refused");
        size_t tag_count = lib.get_tags().size();
        check(!lib.find_tag("no such tag") && lib.get_tags().size() == tag_count, "find_tag doesn't create tags");
    }

    // A first import that never commits must leave the database new, so it runs again next time.
    remove_db(db_path);
    {
        database db;
        db.open(db_path);
        db.begin();
        db.create_schema();
        db.insert_file("half.txt");
    }
    {
        database db;
        db.open(db_path);
        check(db.is_new(), "uncommitted import leaves the database new");
        library lib;
        check(open_library(lib, db, json), "import after an interrupted one: " + db.last_error());
        check(take(lib).count("half.txt") == 0, "interrupted import left nothing behind");
    }
    // A first import whose JSON can't be opened must fail, commit nothing and leave everything reusable.
    remove_db(db_path);
    {
        database db;
        db.open(db_path);
        library lib;
        check(!open_library(lib, db, dir + "/missing.json"), "import of a missing JSON fails");
        check(db.is_new(), "failed import leaves the database new");
        check(lib.get_files().empty() && lib.get_tags().empty(), "failed import leaves the library empty");
        check(lib.attached() == nullptr, "failed import leaves the library detached");
        check(open_library(lib, db, json), "retry on the same objects: " + db.last_error());
        check(take(lib) == snapshot_of_json(json), "retried import matches the JSON");
    }
    {
        database db;
        db.open(db_path);
        check(!db.is_new(), "retried import was committed");
    }
    remove_db(db_path);
    {
        database db;
        db.open(db_path);
        library lib;
        check(!open_library(lib, db, dir + "/missing.json"), "import of a missing JSON fails again");
    }
    {
        database db;
        db.open(db_path);
        check(db.is_new(), "failed import is still new after reopening");
    }
    remove_db(db_path);
    std::filesystem::remove(json);
}

// JSON and database files inside a folder with an accented name. On Windows the narrow file APIs
// would read the name in the ANSI code page and fail to find it.
static void accented_paths(const std::string &dir)
{
    const std::filesystem::path folder = std::filesystem::path(dir) / std::filesystem::u8path("pastas ação");
    std::filesystem::create_directories(folder);

    library expected;
    expected.edit_file(expected.add_file("a.jpg"), expected.retrieve_tag("image"), 1);
    save_library(expected, folder / "library.json");
    check(snapshot_of_json(folder / "library.json") == take(expected), "JSON in an accented folder");

    {
        database db;
        check(db.open(folder / "library.db"), "open database in an accented folder: " + db.last_error());
        library lib;
        check(open_library(lib, db, folder / "library.json"), "import from an accented folder: " + db.last_error());
        check(take(lib) == take(expected), "library imported from an accented folder");
    }
    check(std::filesystem::exists(folder / "library.db"), "database created under the accented name");

    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
}

static double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

static void bench(const std::string &dir, int count)
{
    const std::string json = dir + "/bench.json";
    const std::string db_path = dir + "/bench.db";
    remove_db(db_path);

    {
        library lib;
        for(int i = 0; i < count; ++i)
        {
            file* f = lib.add_file("photos/" + std::to_string(i) + ".jpg");
            lib.edit_file(f, lib.retrieve_tag("image"), 1);
            lib.edit_file(f, lib.retrieve_tag("year" + std::to_string(2000 + i % 25)), 1);
            lib.edit_file(f, lib.retrieve_tag("album" + std::to_string(i % 500)), 1);
        }
        save_library(lib, json);
    }

    auto start = std::chrono::steady_clock::now();
    {
        database db;
        db.open(db_path);
        library lib;
        check(open_library(lib, db, json), "bench import: " + db.last_error());
        std::cout << "import " << count << " files: " << seconds_since(start) << " s\n";
        start = std::chrono::steady_clock::now();
    }
    std::cout << "close: " << seconds_since(start) << " s\n";

    start = std::chrono::steady_clock::now();
    {
        database db;
        db.open(db_path);
        library lib;
        check(open_library(lib, db, ""), "bench load: " + db.last_error());
        check(lib.get_files().size() == static_cast<size_t>(count), "bench file count");
        std::cout << "reopen: " << seconds_since(start) << " s\n";

        start = std::chrono::steady_clock::now();
        for(int i = 0; i < 1000; ++i)
        {
            lib.edit_file(lib.get_files()[i % count], lib.retrieve_tag("edited"), 1);
        }
        std::cout << "1000 single edits: " << seconds_since(start) << " s\n";
        start = std::chrono::steady_clock::now();
    }
    std::cout << "close: " << seconds_since(start) << " s\n";

    remove_db(db_path);
    std::filesystem::remove(json);
}

int main(int argc, char* argv[])
{
    std::string dir = argc > 1 ? argv[1] : std::filesystem::temp_directory_path().string();
    int count = argc > 2 ? std::stoi(argv[2]) : 0;

    round_trip(dir);
    accented_paths(dir);
    if(count > 0)
    {
        bench(dir, count);
    }

    std::cout << (failures ? "FAILED" : "OK") << '\n';
    return failures ? 1 : 0;
}
