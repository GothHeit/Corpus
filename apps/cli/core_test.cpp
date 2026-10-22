// Unit tests for the core model: tag queries, filtering and search, edits and removal, moved files, folder import.
//
// Usage: corpus-core-test [work_dir]
// work_dir holds the databases and files used by the write-through and moved file tests (default: the temp folder).

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#ifdef __linux__
#include <sys/mount.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../../include/database.hpp"
#include "../../include/file.hpp"
#include "../../include/file_identity.hpp"
#include "../../include/folder_import.hpp"
#include "../../include/library.hpp"
#include "../../include/saving.hpp"
#include "../../include/tag.hpp"
#include "../../include/tag_query.hpp"
#include "../../include/text.hpp"
#include "../../third_party/sqlite/sqlite3.h"

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
    check(matches("sci-fi", {"sci-fi"}), "a hyphen inside a word is part of the tag");
    check(!matches("sci-fi", {"sci"}), "a hyphenated tag isn't split at the hyphen");
    check(matches("-sci-fi", {"sci"}), "leading minus negates a hyphenated tag");
    check(!matches("-sci-fi", {"sci-fi"}), "leading minus negates a hyphenated tag, tag present");
    check(matches("dog -sci-fi", {"dog"}), "minus before a hyphenated tag inside a term");
    check(matches("(dog)-cat", {"dog"}), "minus right after a parenthesis negates");
    check(matches("cat-", {"cat-"}), "a trailing hyphen is part of the tag");

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
    check(r.valid() && r.files.size() == 4, "empty search returns every visible file");

    r = lib.search("PHOTOS");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "text search on paths ignores case");
    r = lib.search("docu");
    check(r.valid() && paths(r.files) == std::set<std::string>{"docs/c.txt"}, "text search matches part of a tag");
    r = lib.search("zzz");
    check(r.valid() && r.files.empty(), "text search with no match is valid and empty");

    r = lib.search("{image -cat}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/b.png"}, "tag expression search");
    r = lib.search("{NOT image}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"docs/c.txt", "d.bin"}, "negated tag expression search");
    r = lib.search("{dog OR document}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/b.png", "docs/c.txt"}, "OR tag expression search");
    r = lib.search("{nosuchtag}");
    check(r.valid() && r.files.empty(), "unknown tag is a valid, empty search");
    r = lib.search("a.jpg{image}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "text and tag expression together");
    r = lib.search("a.jpg {image}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "spaces before the braces are ignored");
    r = lib.search("  {image}  photos ");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "spaces after the braces are ignored");
    r = lib.search("   {document}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"docs/c.txt"}, "only spaces besides the braces means no text");

    r = lib.search(" photos  ");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "spaces around a text search are ignored");
    r = lib.search("   ");
    check(r.valid() && r.files.size() == 4, "a search of only spaces returns every visible file");
    r = lib.search("{image}{cat}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "every group must match");
    r = lib.search("{image} photos {-dog}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "groups and text are combined");
    r = lib.search("{cat OR dog} {document}");
    check(r.valid() && r.files.empty(), "groups that never match together give nothing");

    check(!lib.search("{image").valid(), "unclosed brace is invalid");
    check(!lib.search("{image} {cat").valid(), "an unclosed second group is invalid");
    check(!lib.search("{image} {}").valid(), "an empty second group is invalid");
    check(!lib.search("{image} {cat OR}").valid(), "a malformed second group is invalid");
    check(!lib.search("{image {cat}").valid(), "a brace opened inside a group is invalid");
    check(!lib.search("{}").valid(), "empty braces are invalid");
    check(!lib.search("{image AND}").valid(), "malformed expression in braces is invalid");
    check(!lib.search("{(image}").valid(), "unbalanced parenthesis in braces is invalid");
    check(lib.search("image}").valid(), "a closing brace alone is plain text");
    check(lib.search("{image}").error == SearchError::None, "a valid search has no error");
    check(lib.search("{image").error == SearchError::UnclosedBrace, "unclosed brace is reported as such");
    check(lib.search("{image {cat}").error == SearchError::UnclosedBrace, "a brace opened inside a group is an unclosed brace");
    check(lib.search("{}").error == SearchError::BadExpression, "empty braces are a bad expression");
    check(lib.search("{image AND}").error == SearchError::BadExpression, "an incomplete expression is a bad expression");
    check(lib.search("{a AND} {b").error == SearchError::BadExpression, "the first problem from the left is reported");
    check(lib.search("{a} {b").error == SearchError::UnclosedBrace, "an unclosed group after a valid one is an unclosed brace");

    lib.edit_file(lib.find_file("d.bin"), lib.retrieve_tag("sci-fi"), 1);
    r = lib.search("{sci-fi}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"d.bin"}, "search for a hyphenated tag");
    r = lib.search("{-sci-fi -image}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"docs/c.txt"}, "search excluding a hyphenated tag");

    {
        library spaced;
        spaced.edit_file(spaced.add_file("my photos/e.jpg"), spaced.retrieve_tag("image"), 1);
        r = spaced.search("my  {image}  photos");
        check(r.valid() && r.files.size() == 1, "text around braces is joined by one space");
        r = spaced.search("my{image}photos");
        check(r.valid() && r.files.size() == 1, "text touching the braces is joined by one space");
        r = spaced.search("my {-image} photos");
        check(r.valid() && r.files.empty(), "joined text still needs the group to match");
    }

    lib.filter_files(lib.find_tag("image"), 1);
    r = lib.search("");
    check(paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "search starts from the filtered view");
    r = lib.search("{NOT cat}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/b.png"}, "tag expression only sees filtered files");
}

// Case-insensitive matching folds letters beyond ASCII too, without dropping accents.
static void case_folding()
{
    check(fold_case("Ação ÉTÉ Ñandú") == "ação été ñandú", "fold_case lowers accented Latin letters");
    check(fold_case("ŁÓDŹ Ǆ ẞ İ") == "łódź ǆ ß i", "fold_case lowers Latin Extended letters");
    check(fold_case("ΣΟΦΊΑ σοφίας") == "σοφία σοφίασ", "fold_case lowers Greek, final sigma included");
    check(fold_case("МОСКВА Ёж") == "москва ёж", "fold_case lowers Cyrillic");
    check(fold_case("ＡＢＣ") == "ａｂｃ", "fold_case lowers fullwidth letters");
    check(fold_case("ƠN ƯỚC Ǵ Ə Ɓ Ȣ") == "ơn ước ǵ ə ɓ ȣ", "fold_case lowers irregular Latin Extended-B letters");
    check(fold_case("ἈΘῆΝΑΙ ᾈ Ῥ Ὼ") == "ἀθῆναι ᾀ ῥ ὼ", "fold_case lowers polytonic Greek");
    check(fold_case("ᲡᲐᲥᲐᲠᲗᲕᲔᲚᲝ Ⴀ ᎠᏍ Ⅻ Ⓐ 𐐀") == "საქართველო ⴀ ꭰꮝ ⅻ ⓐ 𐐨", "fold_case lowers other scripts");
    check(fold_case("日本 ✓ 😀") == "日本 ✓ 😀", "fold_case keeps other characters");
    check(fold_case("A\xff\xc3" "B\xc3") == "a\xff\xc3" "b\xc3", "fold_case keeps bytes that aren't UTF-8");
    check(fold_case("ação") != fold_case("acao"), "fold_case keeps accents");

    check(matches("ação", {"Ação"}) && matches("AÇÃO", {"ação"}), "tag queries ignore case beyond ASCII");
    check(!matches("acao", {"Ação"}), "tag queries don't ignore accents");
    check(matches("ơn", {"Ơn"}) && matches("ἀθῆναι", {"ἈΘῆΝΑΙ"}), "tag queries ignore Vietnamese and polytonic case");

    library lib;
    lib.edit_file(lib.add_file("fotos/Verão.jpg"), lib.retrieve_tag("Ação"), 1);
    lib.add_file("docs/МОСКВА.txt");
    SearchResult r = lib.search("{ação}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "tag search ignores case beyond ASCII");
    r = lib.search("VERÃO");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "text search ignores accented case");
    r = lib.search("AÇÃ");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "text search on tags ignores accented case");
    r = lib.search("москва");
    check(r.valid() && paths(r.files) == std::set<std::string>{"docs/МОСКВА.txt"}, "text search ignores Cyrillic case");

    lib.edit_tag(lib.find_tag("Ação"), "Ơn");
    r = lib.search("{ơn}");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "tag search sees a renamed tag");
    r = lib.search("ƠN");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "text search sees a renamed tag");
    r = lib.search("{ação}");
    check(r.valid() && r.files.empty(), "tag search forgets a tag's old name");
}

static void edits()
{
    library lib;
    file* f = lib.add_file("a.jpg");
    check(lib.add_file("a.jpg") == f, "adding a path twice returns the same file");
    check(lib.get_files().size() == 1, "adding a path twice keeps one file");
    check(lib.find_file("a.jpg") == f, "find_file finds an added file");
    check(lib.find_file("b.jpg") == nullptr, "find_file doesn't find a missing file");
#ifdef _WIN32
    file* upper = lib.add_file("C:\\photos\\a.jpg");
    check(lib.add_file("c:\\photos\\a.jpg") == upper && lib.find_file("c:\\photos\\a.jpg") == upper,
        "a lower case drive letter is the same drive");
    check(lib.add_files({{"c:\\photos\\a.jpg", ""}}) == 0, "add_files sees a lower case drive letter as the same drive");
    check(lib.remove_file(upper), "remove a file with a drive letter");
#endif
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

    // Linking from the tag side updates both sides, like tag::remove_file.
    tag loose("loose");
    file h("h.jpg");
    loose.add_file(&h);
    loose.add_file(&h);
    check(h.tagged(&loose) && h.get_tags().size() == 1, "tag::add_file links the file back once");
    check(loose.get_files().size() == 1, "tag::add_file adds a file once");
    loose.remove_file(&h);
    check(!h.tagged(&loose) && loose.get_files().empty(), "tag::remove_file unlinks both sides");
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

// Older builds stored "c:\x" and "C:\x" as two files. On Windows loading merges them into the first row, keeping
// the tags of both, so a tag removed later doesn't come back from the other row. Elsewhere case matters.
static void drive_letter_duplicates(const std::filesystem::path &dir)
{
    const std::filesystem::path db_path = dir / "core_test_drive.db";
    remove_db(db_path);
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "set up a database: " + db.last_error());
    }
    sqlite3 *h = nullptr;
    sqlite3_open(db_path.u8string().c_str(), &h);
    const char *sql =
        "INSERT INTO files(id, path, identity) VALUES(1, 'c:\\dup\\a.jpg', NULL), (2, 'c:\\only\\b.jpg', NULL),"
        " (3, 'C:\\dup\\a.jpg', 'id-a');"
        "INSERT INTO tags(id, name) VALUES(1, 'cat'), (2, 'dog');"
        "INSERT INTO file_tags VALUES(1, 1), (1, 2), (2, 1), (3, 1);";
    check(sqlite3_exec(h, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "store drive letter duplicates");
    sqlite3_close(h);

    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "load drive letter duplicates: " + db.last_error());
#ifdef _WIN32
        file* a = lib.find_file("C:\\dup\\a.jpg");
        check(lib.get_files().size() == 2 && a, "drive letter duplicates load as one file");
        check(a && a->get_db_id() == 1, "the first row of a duplicate stays");
        check(a && tag_names(a) == std::set<std::string>{"cat", "dog"}, "a merged file keeps the tags of both rows");
        check(a && a->get_identity() == "id-a", "a merged file keeps an identity from either row");
        lib.edit_file(a, lib.find_tag("dog"), 0);
#else
        check(lib.get_files().size() == 3, "paths differing in case are different files");
#endif
    }

    std::string rows;
    int links = -1;
    sqlite3_open(db_path.u8string().c_str(), &h);
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(h, "SELECT id, path, COALESCE(identity, '') FROM files ORDER BY id;", -1, &st, nullptr);
    while(sqlite3_step(st) == SQLITE_ROW)
        rows += std::to_string(sqlite3_column_int64(st, 0)) + " " + reinterpret_cast<const char*>(sqlite3_column_text(st, 1)) +
            " " + reinterpret_cast<const char*>(sqlite3_column_text(st, 2)) + ";";
    sqlite3_finalize(st);
    sqlite3_prepare_v2(h, "SELECT COUNT(*) FROM file_tags;", -1, &st, nullptr);
    if(sqlite3_step(st) == SQLITE_ROW)
        links = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(h);
#ifdef _WIN32
    check(rows == "1 C:\\dup\\a.jpg id-a;2 C:\\only\\b.jpg ;", "the extra row is deleted and drive letters are stored upper case");
    check(links == 2, "links of the extra row are moved, not copied twice");
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "reload merged database: " + db.last_error());
        file* a = lib.find_file("C:\\dup\\a.jpg");
        check(a && tag_names(a) == std::set<std::string>{"cat"}, "a tag removed from a merged file stays removed");
    }
#else
    check(rows == "1 c:\\dup\\a.jpg ;2 c:\\only\\b.jpg ;3 C:\\dup\\a.jpg id-a;", "rows are left as they are");
    check(links == 4, "links are left as they are");
#endif
    remove_db(db_path);
}

static void touch(const std::filesystem::path &path, const std::string &text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

// The sidebar's Library views narrow a search to every file, the untagged ones or the missing ones.
static void library_views(const std::filesystem::path &dir)
{
    library lib;
    fill(lib);
    SearchResult r = lib.search("", LibraryView::All);
    check(r.valid() && r.files.size() == 4, "All files is every visible file");
    r = lib.search("", LibraryView::Untagged);
    check(r.valid() && paths(r.files) == std::set<std::string>{"d.bin"}, "Untagged is the files with no tag");
    r = lib.search("bin", LibraryView::Untagged);
    check(r.valid() && r.files.size() == 1, "text narrows Untagged");
    r = lib.search("photos", LibraryView::Untagged);
    check(r.valid() && r.files.empty(), "Untagged leaves tagged files out of a text search");
    r = lib.search("{-image}", LibraryView::Untagged);
    check(r.valid() && paths(r.files) == std::set<std::string>{"d.bin"}, "a tag group works in Untagged");
    r = lib.search("{image}", LibraryView::Untagged);
    check(r.valid() && r.files.empty(), "a required tag finds nothing in Untagged");
    check(lib.search("{image", LibraryView::Untagged).error == SearchError::UnclosedBrace, "a view keeps query errors");
    check(lib.search("{}", LibraryView::Missing).error == SearchError::BadExpression, "Missing keeps query errors");

    lib.edit_file(lib.find_file("photos/a.jpg"), lib.find_tag("cat"), 0);
    lib.edit_file(lib.find_file("photos/a.jpg"), lib.find_tag("image"), 0);
    r = lib.search("", LibraryView::Untagged);
    check(paths(r.files) == std::set<std::string>{"photos/a.jpg", "d.bin"}, "a file with its last tag removed is untagged");

    const std::filesystem::path root = dir / "core_test_views";
    std::filesystem::remove_all(root);
    const std::string here = (root / "here.jpg").u8string();
    const std::string gone = (root / "gone.jpg").u8string();
    const std::string never = (root / "never existed.txt").u8string();
    touch(root / "here.jpg", "here");
    touch(root / "gone.jpg", "gone");

    library disk;
    disk.edit_file(disk.add_file(here), disk.retrieve_tag("image"), 1);
    disk.edit_file(disk.add_file(gone), disk.retrieve_tag("image"), 1);
    disk.add_file(never);
    std::filesystem::remove(root / "gone.jpg");

    r = disk.search("", LibraryView::Missing);
    check(r.valid() && paths(r.files) == std::set<std::string>{gone, never}, "Missing is the files no longer at their path");
    check(paths(r.files) == paths(disk.missing_files()), "Missing agrees with missing_files");
    r = disk.search("{image}", LibraryView::Missing);
    check(paths(r.files) == std::set<std::string>{gone}, "a tag group narrows Missing");
    r = disk.search("never", LibraryView::Missing);
    check(paths(r.files) == std::set<std::string>{never}, "text narrows Missing");
    r = disk.search("here", LibraryView::Missing);
    check(r.valid() && r.files.empty(), "a file in place isn't missing");
    r = disk.search("", LibraryView::All);
    check(r.files.size() == 3, "All files includes the missing ones");

    disk.filter_files(disk.find_tag("image"), 1);
    r = disk.search("", LibraryView::Missing);
    check(paths(r.files) == std::set<std::string>{gone}, "Missing only sees the filtered files");
    std::filesystem::remove_all(root);
}

static int stored_version(const std::filesystem::path &db_path)
{
    sqlite3 *h = nullptr;
    int v = -1;
    sqlite3_stmt *st = nullptr;
    if(sqlite3_open(db_path.u8string().c_str(), &h) == SQLITE_OK &&
       sqlite3_prepare_v2(h, "PRAGMA user_version;", -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(h);
    return v;
}

static void identities(const std::filesystem::path &dir)
{
    const std::filesystem::path root = dir / "core_test_identity";
    std::filesystem::remove_all(root);
    const std::string a = (root / "a.txt").u8string();
    const std::string b = (root / "b.txt").u8string();
    const std::string moved = (root / "sub" / "a renamed.txt").u8string();
    touch(root / "a.txt", "a");
    touch(root / "b.txt", "b");

    std::string id = file_identity(a);
    check(!id.empty(), "an existing file has an identity");
    check(file_identity(a) == id, "an identity doesn't change between reads");
    check(file_identity(b) != id, "two files have different identities");
    check(!file_identity(root.u8string()).empty(), "a folder has an identity");
    check(file_identity((root / "none.txt").u8string()).empty(), "a missing file has no identity");
    check(!file_missing(a) && !file_missing(root.u8string()), "existing paths aren't missing");
    check(file_missing((root / "none.txt").u8string()), "a path with nothing is missing");
    check(file_missing((root / "no dir" / "none.txt").u8string()), "a path in a missing folder is missing");
    fat_cache cache;
    check(file_identity(a, &cache) == id && file_identity(b, &cache) == file_identity(b) && cache.fat.size() == 1,
        "identities read with a cache are the same, and the volume is checked once");
    check(!real_path(a).empty() && real_path((root / "." / "a.txt").u8string()) == real_path(a),
        "real_path writes a path in full");
    check(std::filesystem::equivalent(std::filesystem::u8path(real_path(a)), std::filesystem::u8path(a)),
        "real_path leads to the same file");
    check(real_path((root / "none.txt").u8string()).empty(), "a missing file has no real path");
#ifndef _WIN32
    std::filesystem::create_directory_symlink(root, root.parent_path() / "core_test_identity link");
    check(real_path((root.parent_path() / "core_test_identity link" / "a.txt").u8string()) == real_path(a),
        "real_path resolves links");
    std::filesystem::remove(root.parent_path() / "core_test_identity link");
#endif

    std::filesystem::create_directories(root / "sub");
    std::filesystem::rename(root / "a.txt", std::filesystem::u8path(moved));
    check(file_identity(moved) == id, "a renamed and moved file keeps its identity");
    check(file_missing(a), "the old path of a moved file is missing");
    std::filesystem::remove_all(root);
}

// Moved files with a database: identities are stored, missing files are listed, found again and relinked.
static void moved_files(const std::filesystem::path &dir)
{
    const std::filesystem::path root = dir / "core_test_moved";
    const std::filesystem::path db_path = dir / "core_test_moved.db";
    std::filesystem::remove_all(root);
    remove_db(db_path);
    const std::string a = (root / "a.txt").u8string();
    const std::string b = (root / "b.txt").u8string();
    const std::string c = (root / "c.txt").u8string();
    const std::string gone = (root / "gone.txt").u8string();
    const std::string a_moved = (root / "sub" / "a moved.txt").u8string();
    const std::string c_moved = (root / "c moved.txt").u8string();
    touch(root / "a.txt", "a");
    touch(root / "b.txt", "b");
    touch(root / "c.txt", "c");
    touch(root / "gone.txt", "gone");

    {
        database db;
        check(db.open(db_path), "open moved files database: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "set up moved files database: " + db.last_error());
        lib.edit_file(lib.add_file(a), lib.retrieve_tag("image"), 1);
        lib.add_file(b);
        lib.add_file(c);
        lib.add_file(gone);
        lib.add_file((root / "never existed.txt").u8string());
        check(lib.find_file(a)->get_identity() == file_identity(a), "adding a file with a database records its identity");
        check(lib.find_file((root / "never existed.txt").u8string())->get_identity().empty(), "a file that isn't there gets no identity");
        check(lib.missing_files().size() == 1, "only the file that never existed is missing");
        check(db.last_error().empty(), "adding files writes without errors: " + db.last_error());
    }

    std::filesystem::create_directories(root / "sub");
    std::filesystem::rename(std::filesystem::u8path(a), std::filesystem::u8path(a_moved));
    std::filesystem::rename(std::filesystem::u8path(c), std::filesystem::u8path(c_moved));
    std::filesystem::remove(std::filesystem::u8path(gone));

    {
        database db;
        check(db.open(db_path), "reopen moved files database: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "load moved files database: " + db.last_error());
        file* fa = lib.find_file(a);
        file* fc = lib.find_file(c);
        check(fa && fa->get_identity() == file_identity(a_moved), "identities are loaded from the database");
        check(paths(lib.missing_files()) == std::set<std::string>{a, c, gone, (root / "never existed.txt").u8string()},
            "moved and deleted files are missing");

        // c's new path is already in the library, so only a is offered.
        lib.add_file(c_moved);
        std::vector<moved_file> found = lib.find_moved(root.u8string());
        check(found.size() == 1 && found[0].f == fa && found[0].new_path == a_moved, "find_moved finds a moved file");
        check(lib.find_file(a) == fa, "find_moved changes nothing");
        check(lib.find_moved((root / "sub").u8string()).size() == 1, "find_moved searches a subfolder");
        std::error_code scan_error;
        check(lib.find_moved((root / "no such folder").u8string(), &scan_error).empty(), "find_moved in a missing folder finds nothing");
        check(bool(scan_error), "find_moved reports a folder it can't read");
        check(lib.find_moved(root.u8string(), &scan_error).size() == 1 && !scan_error, "a complete search reports no error");
#ifdef __linux__
        std::vector<std::string> mounts;
        check(mount_points(mounts) && std::count(mounts.begin(), mounts.end(), "/") > 0, "mount_points lists /");
        // /proc is its own volume, with no missing file on it.
        auto volume = [](const std::string &path) {std::string id = file_identity(path); return id.substr(0, id.find(':'));};
        if(volume("/proc") != volume(a_moved))
        {
            check(lib.find_moved("/proc", &scan_error).empty() && scan_error == std::errc::cross_device_link,
                "find_moved on another volume reports it without searching");
        }
#endif

        check(!lib.relink(fc, c_moved), "relinking to a path already in the library fails");
        check(lib.find_file(c) == fc, "a refused relink changes nothing");
        check(!lib.relink(nullptr, a_moved), "relinking nullptr fails");
        library other;
        file* foreign = other.add_file(a);
        check(!lib.relink(foreign, a_moved), "relinking another library's file fails");

        check(lib.relink(fa, a_moved), "relink a moved file");
        check(lib.find_file(a) == nullptr && lib.find_file(a_moved) == fa, "relink updates lookup");
        check(fa->get_path() == a_moved && fa->tagged(lib.find_tag("image")), "relink changes the path and keeps the tags");
        check(lib.find_moved(root.u8string()).empty(), "a relinked file isn't offered again");
        check(db.last_error().empty(), "relink writes without errors: " + db.last_error());
    }
    {
        database db;
        check(db.open(db_path), "reopen relinked database: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "load relinked database: " + db.last_error());
        file* fa = lib.find_file(a_moved);
        check(fa && lib.find_file(a) == nullptr, "a relink is saved");
        check(fa && fa->tagged(lib.find_tag("image")) && fa->get_identity() == file_identity(a_moved), "a saved relink keeps tags and identity");

        db.close();
        file* fb = lib.find_file(b);
        check(!lib.relink(fb, c), "relink fails when the database does");
        check(lib.find_file(b) == fb && fb->get_path() == b, "a failed relink leaves the file");
        lib.attach(nullptr);
    }
    remove_db(db_path);
    std::filesystem::remove_all(root);
}

// A volume mounted inside a folder on another volume is searched by find_moved. Mounting needs root on Linux,
// so the test is skipped elsewhere.
static void mounted_inside(const std::filesystem::path &dir)
{
#ifdef __linux__
    namespace fs = std::filesystem;
    const fs::path root = dir / "core_test_mounts";
    fs::remove_all(root);
    fs::create_directories(root / "vol");
    fs::create_directories(root / "empty");
    if(mount("corpus-test", (root / "vol").c_str(), "tmpfs", 0, nullptr) != 0)
    {
        std::cout << "skipped: mounted volume test (mounting needs root)\n";
        fs::remove_all(root);
        return;
    }
    const std::string a = (root / "vol" / "a.txt").u8string();
    const std::string moved = (root / "vol" / "sub" / "a.txt").u8string();
    touch(root / "vol" / "a.txt", "a");
    fs::create_directory_symlink(root, dir / "core_test_mounts link");

    library lib;
    file* fa = lib.add_file(a);
    lib.record_identities();
    fs::create_directories(root / "vol" / "sub");
    fs::rename(fs::u8path(a), fs::u8path(moved));

    auto volume = [](const std::string &path) {std::string id = file_identity(path); return id.substr(0, id.find(':'));};
    check(volume(root.u8string()) != volume(moved), "a tmpfs mount is its own volume");
    std::error_code scan_error;
    std::vector<moved_file> found = lib.find_moved(root.u8string(), &scan_error);
    check(found.size() == 1 && found[0].f == fa && found[0].new_path == moved && !scan_error,
        "find_moved searches a volume mounted inside the folder");
    found = lib.find_moved((dir / "core_test_mounts link").u8string(), &scan_error);
    check(found.size() == 1 && found[0].f == fa && !scan_error, "find_moved finds mounts inside a linked folder");
    check(lib.find_moved((root / "empty").u8string(), &scan_error).empty() && scan_error == std::errc::cross_device_link,
        "find_moved on a folder with no matching mount reports the wrong volume");

    umount2((root / "vol").c_str(), MNT_DETACH);
    fs::remove(dir / "core_test_mounts link");
    fs::remove_all(root);
#else
    (void)dir;
#endif
}

// A version 1 database (no identities) is upgraded in place when opened, keeping everything in it.
static void migration(const std::filesystem::path &dir)
{
    const std::filesystem::path root = dir / "core_test_migration";
    const std::filesystem::path db_path = dir / "core_test_v1.db";
    std::filesystem::remove_all(root);
    remove_db(db_path);
    const std::string a = (root / "a.txt").u8string();
    touch(root / "a.txt", "a");

    sqlite3 *h = nullptr;
    sqlite3_open(db_path.u8string().c_str(), &h);
    const char *sql =
        "CREATE TABLE files(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE);"
        "CREATE TABLE tags(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);"
        "CREATE TABLE file_tags(file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
        " tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE, PRIMARY KEY(file_id, tag_id)) WITHOUT ROWID;"
        "CREATE INDEX file_tags_tag ON file_tags(tag_id);"
        "INSERT INTO files(id, path) VALUES(2, 'gone.txt');"
        "INSERT INTO tags(id, name) VALUES(1, 'image');"
        "PRAGMA user_version = 1;";
    check(sqlite3_exec(h, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "create a version 1 database");
    // The path is bound, not pasted into the SQL: it may hold a quote (a user folder like O'Brien).
    sqlite3_stmt *st = nullptr;
    sqlite3_prepare_v2(h, "INSERT INTO files(id, path) VALUES(1, ?1);", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, a.c_str(), -1, SQLITE_TRANSIENT);
    check(sqlite3_step(st) == SQLITE_DONE, "store a file in the version 1 database");
    sqlite3_finalize(st);
    check(sqlite3_exec(h, "INSERT INTO file_tags VALUES(1, 1);", nullptr, nullptr, nullptr) == SQLITE_OK,
        "tag a file in the version 1 database");
    sqlite3_close(h);

    {
        database db;
        check(db.open(db_path), "open a version 1 database: " + db.last_error());
        check(!db.is_new(), "an upgraded database isn't new");
        check(stored_version(db_path) == database::schema_version, "opening upgrades the schema version");
        library lib;
        check(open_library(lib, db, ""), "load an upgraded database: " + db.last_error());
        file* fa = lib.find_file(a);
        check(fa && fa->tagged(lib.find_tag("image")) && lib.get_files().size() == 2, "the upgrade keeps files, tags and links");
        check(fa && fa->get_identity().empty(), "files stored before the upgrade have no identity");

        check(db.begin(), "begin recording identities: " + db.last_error());
        check(lib.record_identities() == 1, "record_identities records files still in place");
        check(db.commit(), "commit identities: " + db.last_error());
        check(fa && fa->get_identity() == file_identity(a), "record_identities sets the identity");
        check(lib.record_identities() == 0, "record_identities skips files whose identity is up to date");

        // Saving through a temporary file renamed over the original gives the path a new identity.
        touch(root / "a.tmp", "a, saved again");
        std::filesystem::rename(root / "a.tmp", root / "a.txt");
        check(fa && file_identity(a) != fa->get_identity(), "a file renamed over another has its own identity");
        check(lib.record_identities() == 1, "record_identities records the identity of a replaced file");
        check(fa && fa->get_identity() == file_identity(a), "record_identities refreshes a changed identity");
    }
    {
        database db;
        check(db.open(db_path), "reopen an upgraded database: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "load an upgraded database again: " + db.last_error());
        check(lib.find_file(a) && lib.find_file(a)->get_identity() == file_identity(a), "recorded identities are saved");
    }
    check(stored_version(db_path) == database::schema_version, "an upgraded database stays upgraded");

    // In memory only, record_identities works the same.
    library mem;
    file* m = mem.add_file(a);
    check(m->get_identity().empty(), "without a database, add_file doesn't read the identity");
    check(mem.record_identities() == 1 && m->get_identity() == file_identity(a), "record_identities without a database");

    remove_db(db_path);
    std::filesystem::remove_all(root);
}

static std::set<std::string> paths(const std::vector<scanned_file> &files)
{
    std::set<std::string> out;
    for(const scanned_file &s : files)
    {
        out.insert(s.path);
    }
    return out;
}

// scan_folder lists nested files once, and add_files saves the new ones all or none.
static void folder_import(const std::filesystem::path &dir)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute(dir / "core_test_import").lexically_normal();
    const fs::path db_path = dir / "core_test_import.db";
    fs::remove_all(root);
    remove_db(db_path);
    const std::string a = (root / "a.txt").u8string();
    const std::string b = (root / "sub" / "b.txt").u8string();
    const std::string c = (root / "sub" / "deep" / "c.txt").u8string();
    const std::string d = (root / fs::u8path(u8"fotos ação") / fs::u8path(u8"日本.jpg")).u8string();
    touch(root / "a.txt", "a");
    touch(root / "sub" / "b.txt", "b");
    touch(root / "sub" / "deep" / "c.txt", "c");
    touch(fs::u8path(d), "d");
    fs::create_directories(root / "empty");
    std::set<std::string> expected{a, b, c, d};
    // Files the system keeps for itself aren't imported.
    touch(root / "desktop.ini", "s");
    touch(root / "sub" / "thumbs.DB", "s");
    touch(root / "sub" / "deep" / ".DS_Store", "s");
    int system_files = 3;
#ifdef _WIN32
    touch(root / "hidden.dat", "s");
    touch(root / "$RECYCLE.BIN" / "deleted.txt", "s");
    const DWORD hidden_system = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    SetFileAttributesW((root / "hidden.dat").c_str(), hidden_system);
    SetFileAttributesW((root / "$RECYCLE.BIN").c_str(), hidden_system | FILE_ATTRIBUTE_DIRECTORY);
    system_files += 2;
#endif

    // A link back to the folder would loop forever if it were followed.
    std::error_code link_error;
    fs::create_directory_symlink(root, root / "sub" / "loop", link_error);

    std::size_t calls = 0;
    std::size_t last = 0;
    folder_scan scan = scan_folder(root.u8string(), nullptr, [&](std::size_t found, const std::string &current)
    {
        calls++;
        last = found;
        check(expected.count(current) == 1, "progress names a file being imported: " + current);
    });
    check(!scan.error && !scan.cancelled && scan.unreadable == 0, "a readable folder scans without errors");
    check(paths(scan.files) == expected, "scan_folder finds nested files once, and no folders");
    check(scan.skipped == system_files, "scan_folder skips and counts system files");
    check(std::is_sorted(scan.files.begin(), scan.files.end(),
        [](const scanned_file &x, const scanned_file &y) {return x.path < y.path;}), "scanned files are sorted");
    check(calls == 4 && last == 4, "progress is reported for every file");
    bool identities = true;
    for(const scanned_file &s : scan.files)
    {
        identities = identities && s.identity == file_identity(s.path);
    }
    check(identities, "scan_folder reads identities");
    check(paths(scan_folder((root / "sub" / ".." / ".").u8string()).files) == expected,
        "scanned paths are normalized");

    folder_scan missing = scan_folder((root / "nothing here").u8string());
    check(missing.error && missing.files.empty(), "a missing folder reports an error");

    // Cancelling stops the scan, before it starts or as it goes.
    std::atomic<bool> cancel{true};
    folder_scan cancelled = scan_folder(root.u8string(), &cancel);
    check(cancelled.cancelled && cancelled.files.empty(), "a scan cancelled up front finds nothing");
    cancel = false;
    cancelled = scan_folder(root.u8string(), &cancel, [&](std::size_t, const std::string &) {cancel = true;});
    check(cancelled.cancelled && cancelled.files.size() == 1, "a scan stops as soon as it is cancelled");

#ifndef _WIN32
    // A folder that can't be listed is skipped (root reads everything, so this only runs as a normal user).
    fs::create_directories(root / "locked");
    touch(root / "locked" / "e.txt", "e");
    fs::permissions(root / "locked", fs::perms::none);
    std::error_code locked_error;
    fs::directory_iterator probe(root / "locked", locked_error);
    if(locked_error)
    {
        folder_scan partial = scan_folder(root.u8string());
        check(partial.unreadable == 1 && !partial.error && paths(partial.files) == expected,
            "an unreadable subfolder is skipped and counted");
    }
    fs::permissions(root / "locked", fs::perms::owner_all);
    fs::remove_all(root / "locked");
#endif

    {
        database db;
        check(db.open(db_path), "open import database: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "set up import database: " + db.last_error());
        file* fa = lib.add_file(a);
        check(lib.add_files(scan.files) == 3, "add_files adds only files not in the library yet");
        check(lib.find_file(a) == fa && paths(lib.get_files()) == expected, "add_files keeps the files already there");
        check(paths(lib.show()) == expected, "imported files are visible with no filter");
        check(lib.add_files(scan.files) == 0, "importing again adds nothing");
        check(db.last_error().empty(), "add_files writes without errors: " + db.last_error());
    }
    {
        database db;
        check(db.open(db_path), "reopen import database: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "load import database: " + db.last_error());
        check(paths(lib.get_files()) == expected, "imported files are saved");
        file* fd = lib.find_file(d);
        check(fd && !fd->get_identity().empty() && fd->get_identity() == file_identity(d), "imported identities are saved");

        // Any failed insert rolls the whole import back.
        lib.remove_file(lib.find_file(b));
        lib.remove_file(lib.find_file(c));
        sqlite3 *h = nullptr;
        sqlite3_open(db_path.u8string().c_str(), &h);
        check(sqlite3_exec(h, "CREATE TRIGGER no_c BEFORE INSERT ON files WHEN NEW.path LIKE '%c.txt'"
            " BEGIN SELECT RAISE(ABORT, 'no c'); END;", nullptr, nullptr, nullptr) == SQLITE_OK, "add a failing trigger");
        check(lib.add_files(scan.files) == -1 && !db.last_error().empty(), "add_files fails when an insert does");
        check(lib.find_file(b) == nullptr && lib.find_file(c) == nullptr && lib.get_files().size() == 2,
            "a failed import leaves the library as it was");
        db.clear_error();
        sqlite3_exec(h, "DROP TRIGGER no_c;", nullptr, nullptr, nullptr);
        sqlite3_close(h);

        db.close();
        check(lib.add_files(scan.files) == -1 && lib.get_files().size() == 2, "add_files fails when the database is closed");
        lib.attach(nullptr);
    }
    {
        database db;
        check(db.open(db_path), "reopen after a failed import: " + db.last_error());
        library lib;
        check(open_library(lib, db, ""), "load after a failed import: " + db.last_error());
        check(paths(lib.get_files()) == std::set<std::string>{a, d}, "a failed import saves nothing");
    }

    // In memory only, with a filter applied: the new files have no tags, so they aren't shown.
    library mem;
    mem.edit_file(mem.add_file(a), mem.retrieve_tag("image"), 1);
    mem.filter_files(mem.find_tag("image"), 1);
    check(mem.add_files(scan.files) == 3 && mem.get_files().size() == 4, "add_files without a database");
    check(paths(mem.show()) == std::set<std::string>{a}, "imported files are hidden by a tag filter");

    remove_db(db_path);
    fs::remove_all(root);
}

// Windows only takes paths past MAX_PATH (260 characters) with the \\?\ prefix.
static std::filesystem::path long_name(const std::filesystem::path &path)
{
#ifdef _WIN32
    return std::filesystem::path(L"\\\\?\\" + path.wstring());
#else
    return path;
#endif
}

// Folders and files past MAX_PATH are scanned too, with their identities.
static void long_paths(const std::filesystem::path &dir)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::absolute(dir / "core_test_long").lexically_normal();
    std::vector<fs::path> levels{root};
    while(levels.back().native().size() < 300)
    {
        levels.push_back(levels.back() / (std::string(40, 'd') + std::to_string(levels.size())));
    }
    const fs::path deep = levels.back() / "long.txt";
    std::error_code ec;
    bool made = true;
    for(const fs::path &level : levels)
    {
        made = (fs::create_directory(long_name(level), ec) || !ec) && made;
    }
    std::ofstream(long_name(deep)) << "x";
    check(made && file_identity(deep.u8string()) != "", "create a file past MAX_PATH");

    folder_scan scan = scan_folder(root.u8string());
    check(!scan.error && scan.unreadable == 0 && paths(scan.files) == std::set<std::string>{deep.u8string()},
        "scan_folder lists folders past MAX_PATH");
    check(!scan.files.empty() && !scan.files[0].identity.empty(), "scan_folder reads identities past MAX_PATH");

    // std::filesystem can't list them on Windows, so remove_all can't either.
    fs::remove(long_name(deep), ec);
    for(auto level = levels.rbegin(); level != levels.rend(); ++level)
    {
        fs::remove(long_name(*level), ec);
    }
}

int main(int argc, char* argv[])
{
    std::filesystem::path dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
    std::filesystem::create_directories(dir);

    tag_query_parsing();
    tag_query_malformed();
    filtering();
    searching();
    library_views(dir);
    case_folding();
    edits();
    removal();
    removal_write_through(dir);
    drive_letter_duplicates(dir);
    identities(dir);
    moved_files(dir);
    mounted_inside(dir);
    migration(dir);
    folder_import(dir);
    long_paths(dir);

    std::cout << (failures ? "FAILED" : "OK") << '\n';
    return failures ? 1 : 0;
}
