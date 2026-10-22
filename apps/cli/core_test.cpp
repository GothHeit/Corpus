// Unit tests for the core model: tag queries, filtering and search, edits and removal.
//
// Usage: corpus-core-test [work_dir]
// work_dir holds the database used by the removal write-through tests (default: the temp folder).

#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../../include/database.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/saving.hpp"
#include "../../include/tag.hpp"
#include "../../include/tag_query.hpp"

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if(!ok)
    {
        std::cout << "FAIL: " << what << '\n';
        failures++;
    }
}

static std::set<std::string> paths(const std::vector<file*> &files)
{
    std::set<std::string> out;
    for(const file* f : files)
    {
        out.insert(f->get_path());
    }
    return out;
}

static std::set<std::string> tag_names(const file* f)
{
    std::set<std::string> out;
    for(const tag* t : f->get_tags())
    {
        out.insert(t->get_id());
    }
    return out;
}

static bool contains(const std::vector<file*> &files, const file* f)
{
    for(const file* x : files)
    {
        if(x == f)
            return true;
    }
    return false;
}

// Parses the query and evaluates it against a file with the given tags.
static bool matches(const std::string &query, std::initializer_list<const char*> names)
{
    std::vector<tag> storage;
    storage.reserve(names.size());
    std::vector<tag*> tags;
    for(const char* n : names)
    {
        storage.emplace_back(n);
        tags.push_back(&storage.back());
    }
    std::unique_ptr<QueryNode> ast = ParseQuery(query);
    if(!ast)
    {
        std::cout << "FAIL: \"" << query << "\" should parse\n";
        failures++;
        return false;
    }
    return ast->Evaluate(tags);
}

static void tag_query_parsing()
{
    check(matches("cat", {"cat"}), "single tag matches");
    check(!matches("cat", {"dog"}), "single tag doesn't match another");
    check(!matches("cat", {}), "single tag doesn't match an untagged file");
    check(matches("CAT", {"Cat"}), "tag names match ignoring case");

    check(matches("cat AND dog", {"cat", "dog"}), "AND with both tags");
    check(!matches("cat AND dog", {"cat"}), "AND with one tag");
    check(matches("cat dog", {"dog", "cat"}), "juxtaposition means AND");
    check(!matches("cat dog", {"dog"}), "juxtaposition needs both tags");

    check(matches("cat OR dog", {"dog"}), "OR with one tag");
    check(!matches("cat OR dog", {"bird"}), "OR with neither tag");

    check(matches("NOT cat", {"dog"}), "NOT without the tag");
    check(!matches("NOT cat", {"cat"}), "NOT with the tag");
    check(matches("-cat", {}), "minus negates");
    check(!matches("dog -cat", {"dog", "cat"}), "minus inside a term");
    check(matches("dog -cat", {"dog"}), "minus inside a term, tag absent");
    check(matches("NOT NOT cat", {"cat"}), "double NOT");
    check(matches("--cat", {"cat"}), "double minus");

    // AND binds tighter than OR.
    check(matches("cat OR dog bird", {"cat"}), "a OR b c is a OR (b AND c), left side");
    check(!matches("cat OR dog bird", {"dog"}), "a OR b c is a OR (b AND c), right side partial");
    check(matches("cat dog OR bird", {"bird"}), "a b OR c is (a AND b) OR c");
    check(!matches("(cat OR dog) bird", {"cat"}), "parentheses group OR first");
    check(matches("(cat OR dog) bird", {"dog", "bird"}), "parentheses group OR first, match");
    check(matches("-(cat OR dog)", {"bird"}), "minus negates a group");
    check(!matches("-(cat OR dog)", {"dog"}), "minus negates a group, tag present");
    check(matches("((cat))", {"cat"}), "nested parentheses");
    check(matches("(cat)(dog)", {"cat", "dog"}), "groups without spaces are ANDed");
    check(matches("  cat   OR   dog  ", {"dog"}), "extra spaces are ignored");

    // Operators are uppercase only; anything else is a tag name.
    check(matches("cat and dog", {"cat", "and", "dog"}), "lowercase 'and' is a tag");
    check(!matches("cat and dog", {"cat", "dog"}), "lowercase 'and' is a required tag");
}

static void tag_query_malformed()
{
    const char* bad[] = {
        "", "   ", "(", ")", "()", "-", "NOT", "AND", "OR",
        "cat AND", "AND cat", "cat OR", "OR cat", "cat OR OR dog", "cat AND AND dog",
        "(cat", "cat)", "((cat)", "(cat))", "cat (dog", "cat -", "cat NOT", "cat OR ()",
    };
    for(const char* q : bad)
    {
        check(ParseQuery(q) == nullptr, std::string("\"") + q + "\" is rejected");
    }
}

// Files: a.jpg {image, cat}, b.png {image, dog}, c.txt {document}, d.bin {}.
static void fill(library &lib)
{
    lib.edit_file(lib.add_file("photos/a.jpg"), lib.retrieve_tag("image"), 1);
    lib.edit_file(lib.add_file("photos/a.jpg"), lib.retrieve_tag("cat"), 1);
    lib.edit_file(lib.add_file("photos/b.png"), lib.retrieve_tag("image"), 1);
    lib.edit_file(lib.add_file("photos/b.png"), lib.retrieve_tag("dog"), 1);
    lib.edit_file(lib.add_file("docs/c.txt"), lib.retrieve_tag("document"), 1);
    lib.add_file("d.bin");
}

static void filtering()
{
    library lib;
    fill(lib);
    const std::set<std::string> all = {"photos/a.jpg", "photos/b.png", "docs/c.txt", "d.bin"};
    check(paths(lib.show()) == all, "no filter shows every file");

    tag* image = lib.find_tag("image");
    tag* cat = lib.find_tag("cat");
    lib.filter_files(image, 1);
    check(paths(lib.show()) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "filter by one tag");
    lib.filter_files(cat, 1);
    check(paths(lib.show()) == std::set<std::string>{"photos/a.jpg"}, "filter by two tags needs both");
    check(lib.current_filter().size() == 2, "filter lists its tags");
    lib.filter_files(cat, 1);
    check(lib.current_filter().size() == 2, "adding a filtered tag again does nothing");

    lib.filter_files(lib.find_tag("document"), 1);
    check(lib.show().empty(), "a filter no file matches shows nothing");
    lib.filter_files(lib.find_tag("document"), 0);
    check(paths(lib.show()) == std::set<std::string>{"photos/a.jpg"}, "removing a tag widens the filter");

    lib.add_file("new.jpg");
    check(!contains(lib.show(), lib.find_file("new.jpg")), "a new untagged file is hidden by a filter");

    lib.filter_files(lib.find_tag("dog"), 0);
    check(lib.current_filter().size() == 2, "removing a tag that isn't filtered does nothing");
    lib.filter_files(cat, 0);
    lib.filter_files(image, 0);
    check(lib.current_filter().empty(), "filter is empty again");
    check(lib.show().size() == lib.get_files().size(), "empty filter shows every file again");

    lib.add_file("newer.jpg");
    check(contains(lib.show(), lib.find_file("newer.jpg")), "a new file is visible without a filter");
}

static void searching()
{
    library lib;
    fill(lib);

    SearchResult r = lib.search("");
    check(r.valid && r.files.size() == 4, "empty search returns every visible file");

    r = lib.search("PHOTOS");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "text search on paths ignores case");
    r = lib.search("docu");
    check(r.valid && paths(r.files) == std::set<std::string>{"docs/c.txt"}, "text search matches part of a tag");
    r = lib.search("zzz");
    check(r.valid && r.files.empty(), "text search with no match is valid and empty");

    r = lib.search("{image -cat}");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/b.png"}, "tag expression search");
    r = lib.search("{NOT image}");
    check(r.valid && paths(r.files) == std::set<std::string>{"docs/c.txt", "d.bin"}, "negated tag expression search");
    r = lib.search("{dog OR document}");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/b.png", "docs/c.txt"}, "OR tag expression search");
    r = lib.search("{nosuchtag}");
    check(r.valid && r.files.empty(), "unknown tag is a valid, empty search");
    r = lib.search("a.jpg{image}");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "text and tag expression together");
    r = lib.search("a.jpg {image}");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "spaces before the braces are ignored");
    r = lib.search("  {image}  photos ");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "spaces after the braces are ignored");
    r = lib.search("   {document}");
    check(r.valid && paths(r.files) == std::set<std::string>{"docs/c.txt"}, "only spaces besides the braces means no text");

    check(!lib.search("{image").valid, "unclosed brace is invalid");
    check(!lib.search("{}").valid, "empty braces are invalid");
    check(!lib.search("{image AND}").valid, "malformed expression in braces is invalid");
    check(!lib.search("{(image}").valid, "unbalanced parenthesis in braces is invalid");
    check(lib.search("image}").valid, "a closing brace alone is plain text");

    lib.filter_files(lib.find_tag("image"), 1);
    r = lib.search("");
    check(paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "search starts from the filtered view");
    r = lib.search("{NOT cat}");
    check(r.valid && paths(r.files) == std::set<std::string>{"photos/b.png"}, "tag expression only sees filtered files");
}

static void edits()
{
    library lib;
    file* f = lib.add_file("a.jpg");
    check(lib.add_file("a.jpg") == f, "adding a path twice returns the same file");
    check(lib.get_files().size() == 1, "adding a path twice keeps one file");
    check(lib.find_file("a.jpg") == f, "find_file finds an added file");
    check(lib.find_file("b.jpg") == nullptr, "find_file doesn't find a missing file");
    check(lib.get_files().size() == 1, "find_file doesn't add files");

    tag* t = lib.create_tag("image");
    check(lib.create_tag("image") == t, "creating a tag twice returns the same tag");
    check(lib.retrieve_tag("image") == t, "retrieve_tag returns an existing tag");
    check(lib.find_tag("Image") == nullptr, "tag lookup is case sensitive");
    check(lib.get_tags().size() == 1, "one tag after repeated creation");

    lib.edit_file(f, t, 1);
    lib.edit_file(f, t, 1);
    check(f->get_tags().size() == 1 && t->get_files().size() == 1, "tagging twice links once");
    check(f->tagged(t) && contains(t->get_files(), f), "tagging links both ways");

    tag* other = lib.create_tag("other");
    lib.edit_file(f, other, 0);
    check(f->get_tags().size() == 1, "untagging a tag the file doesn't have does nothing");
    lib.edit_file(f, t, 0);
    check(!f->tagged(t) && t->get_files().empty(), "untagging unlinks both ways");

    lib.edit_file(f, t, 1);
    check(lib.edit_tag(t, "picture"), "rename to a free name");
    check(t->get_id() == "picture", "rename changes the tag name");
    check(lib.find_tag("picture") == t && lib.find_tag("image") == nullptr, "rename updates lookup");
    check(f->tagged(t), "rename keeps the tag's files");
    check(!lib.edit_tag(t, "other"), "rename to an existing name is refused");
    check(t->get_id() == "picture" && lib.find_tag("other") == other, "refused rename changes nothing");
    check(lib.edit_tag(t, "Picture") && lib.find_tag("Picture") == t, "rename changing only case");
    check(lib.retrieve_tag("image") != t, "the old name is free for a new tag");

    lib.filter_files(t, 1);
    file* g = lib.add_file("b.jpg");
    lib.edit_file(g, t, 1);
    check(contains(lib.show(), g), "tagging with a filtered tag shows the file");
    lib.edit_file(f, other, 1);
    check(contains(lib.show(), f), "tagging with another tag keeps the view");
    lib.edit_file(g, t, 0);
    check(!contains(lib.show(), g), "untagging a filtered tag hides the file");
}

static void removal()
{
    library lib;
    fill(lib);
    file* a = lib.find_file("photos/a.jpg");
    tag* image = lib.find_tag("image");
    tag* cat = lib.find_tag("cat");

    check(lib.remove_file(a), "remove a file");
    check(lib.find_file("photos/a.jpg") == nullptr, "removed file can't be found");
    check(lib.get_files().size() == 3, "removed file leaves the file list");
    check(paths(lib.show()).count("photos/a.jpg") == 0, "removed file leaves the view");
    check(paths(image->get_files()) == std::set<std::string>{"photos/b.png"}, "removed file leaves its tags");
    check(cat->get_files().empty(), "a tag can be left with no files");
    check(lib.find_tag("cat") == cat, "removing a file keeps its tags");
    check(!lib.remove_file(nullptr), "removing nullptr fails");

    file* b = lib.find_file("photos/b.png");
    library other;
    file* foreign = other.add_file("photos/b.png");
    check(!lib.remove_file(foreign), "removing another library's file with the same path fails");
    check(lib.find_file("photos/b.png") == b && other.find_file("photos/b.png") == foreign, "failed removal changes nothing");

    lib.filter_files(image, 1);
    check(lib.remove_file(b), "remove a visible filtered file");
    check(lib.show().empty(), "filtered view loses the removed file");
    check(lib.add_file("photos/a.jpg") != nullptr && lib.find_file("photos/a.jpg")->get_tags().empty(), "re-adding a removed path makes a new untagged file");

    // Tags
    library tl;
    fill(tl);
    image = tl.find_tag("image");
    tag* dog = tl.find_tag("dog");
    file* pb = tl.find_file("photos/b.png");
    tl.filter_files(image, 1);
    tl.filter_files(dog, 1);
    check(paths(tl.show()) == std::set<std::string>{"photos/b.png"}, "two-tag filter before deleting");
    check(tl.delete_tag(dog), "delete a filtered tag");
    check(tl.find_tag("dog") == nullptr && tl.get_tags().size() == 3, "deleted tag leaves the tag list");
    check(tag_names(pb) == std::set<std::string>{"image"}, "deleted tag leaves its files");
    check(tl.current_filter().size() == 1 && tl.current_filter()[0] == image, "deleted tag leaves the filter");
    check(paths(tl.show()) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "deleting a filtered tag widens the view");
    check(tl.get_files().size() == 4, "deleting a tag keeps its files");

    check(tl.delete_tag(image), "delete the last filtered tag");
    check(tl.current_filter().empty() && tl.show().size() == 4, "deleting the whole filter shows every file");

    check(!tl.delete_tag(nullptr), "deleting nullptr fails");
    library ol;
    tag* foreign_tag = ol.create_tag("cat");
    check(!tl.delete_tag(foreign_tag), "deleting another library's tag with the same name fails");
    check(tl.find_tag("cat") != nullptr && ol.find_tag("cat") == foreign_tag, "failed delete changes nothing");
    check(tl.retrieve_tag("dog") != nullptr && tl.find_tag("dog")->get_files().empty(), "re-creating a deleted tag makes a new empty tag");
}

static void remove_db(const std::filesystem::path &path)
{
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
}

// remove_file and delete_tag with a database attached: the rows (and their links) go, and a failed
// delete leaves memory untouched.
static void removal_write_through(const std::filesystem::path &dir)
{
    const std::filesystem::path db_path = dir / "core_test.db";
    remove_db(db_path);

    {
        database db;
        bool ok = db.open(db_path);
        check(ok, "open database: " + db.last_error());
        library lib;
        ok = open_library(lib, db, "");
        check(ok, "set up an empty database: " + db.last_error());
        fill(lib);
        check(lib.remove_file(lib.find_file("photos/a.jpg")), "remove a file with a database");
        check(lib.delete_tag(lib.find_tag("dog")), "delete a tag with a database");
        check(db.last_error().empty(), "removals write without errors: " + db.last_error());
    }
    {
        database db;
        bool ok = db.open(db_path);
        check(ok, "reopen database: " + db.last_error());
        library lib;
        ok = open_library(lib, db, "");
        check(ok, "load database: " + db.last_error());
        check(paths(lib.get_files()) == std::set<std::string>{"photos/b.png", "docs/c.txt", "d.bin"}, "removed file stays removed");
        check(lib.find_tag("dog") == nullptr, "deleted tag stays deleted");
        check(lib.find_tag("cat") && lib.find_tag("cat")->get_files().empty(), "removed file's links are gone");
        check(tag_names(lib.find_file("photos/b.png")) == std::set<std::string>{"image"}, "deleted tag's links are gone");

        // A closed database makes every delete fail.
        db.close();
        file* b = lib.find_file("photos/b.png");
        tag* image = lib.find_tag("image");
        check(!lib.remove_file(b), "remove_file fails when the database does");
        check(lib.find_file("photos/b.png") == b && b->tagged(image), "failed remove_file leaves the file");
        check(!lib.delete_tag(image), "delete_tag fails when the database does");
        check(lib.find_tag("image") == image && b->tagged(image), "failed delete_tag leaves the tag");
        lib.attach(nullptr);
    }
    remove_db(db_path);
}

int main(int argc, char* argv[])
{
    std::filesystem::path dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
    std::filesystem::create_directories(dir);

    tag_query_parsing();
    tag_query_malformed();
    filtering();
    searching();
    edits();
    removal();
    removal_write_through(dir);

    std::cout << (failures ? "FAILED" : "OK") << '\n';
    return failures ? 1 : 0;
}
