// Unit tests for the core model: tag queries, search, edits and removal, moved files, reading progress,
// folder import.
//
// Usage: corpus-core-test [work_dir]
// work_dir holds the databases and files used by the write-through and moved file tests (default: the temp folder).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include <sys/mount.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../../include/backup.hpp"
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
        check(r.valid() && r.files.size() == 1, "words around braces each match");
        r = spaced.search("my{image}photos");
        check(r.valid() && r.files.size() == 1, "words touching the braces each match");
        r = spaced.search("my {-image} photos");
        check(r.valid() && r.files.empty(), "words still need the group to match");
    }

    // Words match separately, in any order, each in the path or a tag name.
    r = lib.search("jpg photos");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "every word must match");
    r = lib.search("photos jpg");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "words match in any order");
    r = lib.search("a.jpg cat");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "one word in the path, one in a tag");
    r = lib.search("dog cat");
    check(r.valid() && r.files.empty(), "two words in tags of different files find nothing");
    r = lib.search("ima ima  IMAGE");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "repeated words are one");
    r = lib.search("photos/a");
    check(r.valid() && paths(r.files) == std::set<std::string>{"photos/a.jpg"}, "a word can span path separators");
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

    check(fold_search("Ação ÉTÉ Ñandú") == "acao ete nandu", "fold_search drops Latin accents");
    check(fold_search("ŁÓDŹ Øre đ İ Straße") == "lodz ore d i strasse", "fold_search plain letters for strokes, ß as ss");
    check(fold_search("Tiếng Việt ƠN") == "tieng viet on", "fold_search drops Vietnamese accents");
    check(fold_search("ΣΟΦΊΑ ἈΘῆΝΑΙ") == "σοφια αθηναι", "fold_search drops Greek accents");
    check(fold_search("Ёж") == "еж", "fold_search drops Cyrillic accents");
    check(fold_search("Ǿǿ") == "oo", "fold_search turns ǿ into a plain o");
    check(fold_search("с\xd2\x83вѧтъ") == "свѧтъ", "fold_search drops Cyrillic combining marks (titlo)");
    check(fold_search("Ac\xcc\xa7" "a\xcc\x83" "o") == "acao", "fold_search drops combining marks (decomposed text)");
    check(fold_search("日本 ✓ 😀 ＡＢＣ") == "日本 ✓ 😀 ａｂｃ", "fold_search keeps other characters");
    check(fold_search("A\xff\xc3") == "a\xff\xc3", "fold_search keeps bytes that aren't UTF-8");

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
    r = lib.search("verao");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "text search ignores accents");
    r = lib.search("ACAO");
    check(r.valid() && paths(r.files) == std::set<std::string>{"fotos/Verão.jpg"}, "text search on tags ignores accents");
    r = lib.search("{acao}");
    check(r.valid() && r.files.empty(), "a tag group names one tag: accents count there");

    lib.rename_tag(lib.find_tag("Ação"), "Ơn");
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
    check(lib.find_tag("IMAGE") == t && lib.create_tag("Image") == t && lib.retrieve_tag("iMaGe") == t,
        "tag lookup and creation ignore case");
    check(lib.get_tags().size() == 1 && t->get_id() == "image", "one tag, first spelling kept");
    check(lib.find_tag("imagé") == nullptr, "tag lookup keeps accents");

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
    check(lib.rename_tag(t, "picture") == rename_result::Renamed, "rename to a free name");
    check(t->get_id() == "picture", "rename changes the tag name");
    check(lib.find_tag("picture") == t && lib.find_tag("image") == nullptr, "rename updates lookup");
    check(f->tagged(t), "rename keeps the tag's files");
    check(lib.rename_tag(t, "Picture") == rename_result::Renamed && lib.find_tag("picture") == t &&
        t->get_id() == "Picture", "rename changing only case");
    check(lib.retrieve_tag("image") != t, "the old name is free for a new tag");
    check(!lib.edit_file(nullptr, t, 1) && !lib.edit_file(f, nullptr, 1), "edit_file refuses nullptr");
    check(lib.edit_file(f, t, 1), "edit_file is true when the file already is as asked");

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
    check(paths(lib.search("").files).count("photos/a.jpg") == 0, "removed file leaves searches");
    check(paths(image->get_files()) == std::set<std::string>{"photos/b.png"}, "removed file leaves its tags");
    check(cat->get_files().empty(), "a tag can be left with no files");
    check(lib.find_tag("cat") == cat, "removing a file keeps its tags");
    check(!lib.remove_file(nullptr), "removing nullptr fails");

    file* b = lib.find_file("photos/b.png");
    library other;
    file* foreign = other.add_file("photos/b.png");
    check(!lib.remove_file(foreign), "removing another library's file with the same path fails");
    check(lib.find_file("photos/b.png") == b && other.find_file("photos/b.png") == foreign, "failed removal changes nothing");

    check(lib.remove_file(b), "remove a tagged file");
    check(lib.search("{image}").files.empty(), "a tag search loses the removed file");
    check(lib.add_file("photos/a.jpg") != nullptr && lib.find_file("photos/a.jpg")->get_tags().empty(), "re-adding a removed path makes a new untagged file");

    // Tags
    library tl;
    fill(tl);
    image = tl.find_tag("image");
    tag* dog = tl.find_tag("dog");
    file* pb = tl.find_file("photos/b.png");
    check(tl.delete_tag(dog), "delete a tag");
    check(tl.find_tag("dog") == nullptr && tl.get_tags().size() == 3, "deleted tag leaves the tag list");
    check(tag_names(pb) == std::set<std::string>{"image"}, "deleted tag leaves its files");
    check(tl.get_files().size() == 4, "deleting a tag keeps its files");

    check(tl.delete_tag(image), "delete another tag");
    check(tl.search("{image}").files.empty() && tl.find_tag("image") == nullptr, "a deleted tag matches nothing");

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
        "INSERT INTO file_tags VALUES(1, 1), (1, 2), (2, 1), (3, 1);"
        "INSERT INTO reading_progress VALUES(1, 4, 10, '', 100), (3, 7, 10, 'newer', 200);";
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
        check(a && a->get_progress() && a->get_progress()->position == 7 && a->get_progress()->location == "newer",
            "a merged file keeps the more recent reading progress");
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
    std::string progress;
    sqlite3_prepare_v2(h, "SELECT file_id, position FROM reading_progress ORDER BY file_id;", -1, &st, nullptr);
    while(sqlite3_step(st) == SQLITE_ROW)
        progress += std::to_string(sqlite3_column_int64(st, 0)) + " " + std::to_string(sqlite3_column_int64(st, 1)) + ";";
    sqlite3_finalize(st);
    sqlite3_close(h);
#ifdef _WIN32
    check(rows == "1 C:\\dup\\a.jpg id-a;2 C:\\only\\b.jpg ;", "the extra row is deleted and drive letters are stored upper case");
    check(links == 2, "links of the extra row are moved, not copied twice");
    check(progress == "1 7;", "the more recent progress moves to the kept row, the older one is deleted");
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
    check(progress == "1 4;3 7;", "reading progress is left as it is");
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

    touch(root / "gone.jpg", "back");
    r = disk.search("", LibraryView::Missing);
    check(paths(r.files) == std::set<std::string>{never}, "Missing sees the disk as it is now");
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
        check(fa && !fa->get_progress(), "files stored before the upgrade have no reading progress");
        reading_progress p;
        p.position = 2;
        check(lib.set_progress(fa, p), "an upgraded database stores reading progress: " + db.last_error());

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
        check(lib.find_file(a) && lib.find_file(a)->get_progress() && lib.find_file(a)->get_progress()->position == 2,
            "progress stored after an upgrade is saved");
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

static int count_rows(const std::filesystem::path &db_path, const char *table)
{
    sqlite3 *h = nullptr;
    int n = -1;
    sqlite3_stmt *st = nullptr;
    std::string sql = std::string("SELECT COUNT(*) FROM ") + table + ";";
    if(sqlite3_open(db_path.u8string().c_str(), &h) == SQLITE_OK &&
       sqlite3_prepare_v2(h, sql.c_str(), -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(h);
    return n;
}

static reading_progress page(std::int64_t position, std::int64_t length, std::int64_t last_read = 0,
    const std::string &location = "")
{
    reading_progress p;
    p.position = position;
    p.length = length;
    p.last_read = last_read;
    p.location = location;
    return p;
}

// Reading progress: checked, listed by recency, saved, kept by a relink and gone with the file.
static void reading_progress_tests(const std::filesystem::path &dir)
{
    library mem;
    file* a = mem.add_file("books/a.pdf");
    file* b = mem.add_file("books/b.cbz");
    file* c = mem.add_file("books/c.epub");
    mem.add_file("books/unread.pdf");
    check(!a->get_progress() && mem.recently_read().empty(), "a new file has no reading progress");

    check(!mem.set_progress(a, page(-1, 10)), "a negative position is refused");
    check(!mem.set_progress(a, page(10, 10)), "a position past the last page is refused");
    check(!mem.set_progress(a, page(0, -1)), "a negative length is refused");
    check(!a->get_progress(), "refused progress isn't recorded");
    check(!mem.set_progress(nullptr, page(0, 1)), "setting the progress of nullptr fails");
    library other;
    file* foreign = other.add_file("books/a.pdf");
    check(!mem.set_progress(foreign, page(0, 1)) && !foreign->get_progress(), "setting another library's file fails");

    std::int64_t before = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    check(mem.set_progress(a, page(9, 10)), "the last page is a valid position");
    check(a->get_progress() && a->get_progress()->position == 9 && a->get_progress()->length == 10,
        "set_progress records the position and length");
    check(a->get_progress() && a->get_progress()->last_read >= before, "a last_read of 0 is stamped with now");
    check(mem.set_progress(b, page(500, 0, 100)), "a file with an unknown length takes any position");
    check(mem.set_progress(c, page(0, 0, 200, "chapter3#p12")), "a location is recorded");
    check(c->get_progress() && c->get_progress()->location == "chapter3#p12" && c->get_progress()->last_read == 200,
        "a given last_read is kept");

    std::vector<file*> recent = mem.recently_read();
    check(recent.size() == 3 && recent[0] == a && recent[1] == c && recent[2] == b, "recently_read lists the most recent first");
    recent = mem.recently_read(2);
    check(recent.size() == 2 && recent[0] == a && recent[1] == c, "recently_read stops at the limit");
    check(mem.set_progress(b, page(3, 0, 200)) && mem.recently_read()[1] == b,
        "files read at the same time are listed by path");

    check(mem.clear_progress(c) && !c->get_progress(), "clear_progress forgets it");
    check(mem.clear_progress(c), "clearing a file with no progress succeeds");
    check(!mem.clear_progress(foreign) && !mem.clear_progress(nullptr), "clearing another library's file fails");
    check(mem.recently_read().size() == 2, "a cleared file isn't listed");
    mem.remove_file(b);
    check(mem.recently_read().size() == 1, "a removed file isn't listed");

    // With a database.
    const std::filesystem::path root = dir / "core_test_progress";
    const std::filesystem::path db_path = dir / "core_test_progress.db";
    std::filesystem::remove_all(root);
    remove_db(db_path);
    const std::string x = (root / "x.pdf").u8string();
    const std::string y = (root / "y.pdf").u8string();
    const std::string x_moved = (root / "sub" / "x.pdf").u8string();
    touch(root / "x.pdf", "x");
    touch(root / "y.pdf", "y");
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "set up a progress database: " + db.last_error());
        check(lib.set_progress(lib.add_file(x), page(4, 12, 300, "loc")), "set progress with a database");
        check(lib.set_progress(lib.add_file(y), page(1, 2, 100)), "set progress of a second file");
        check(lib.set_progress(lib.find_file(x), page(5, 12, 400, "loc")), "set progress again replaces it");
        check(count_rows(db_path, "reading_progress") == 2, "one progress row per file");
        check(db.last_error().empty(), "progress writes without errors: " + db.last_error());
    }
    std::filesystem::create_directories(root / "sub");
    std::filesystem::rename(std::filesystem::u8path(x), std::filesystem::u8path(x_moved));
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "load a progress database: " + db.last_error());
        file* fx = lib.find_file(x);
        const reading_progress *p = fx ? fx->get_progress() : nullptr;
        check(p && p->position == 5 && p->length == 12 && p->location == "loc" && p->last_read == 400,
            "progress is loaded from the database");
        check(lib.recently_read().size() == 2 && lib.recently_read()[0] == fx, "loaded progress is listed by recency");
        std::vector<moved_file> found = lib.find_moved(root.u8string());
        check(found.size() == 1 && lib.relink(found[0].f, found[0].new_path), "relink a read file");
        check(fx->get_progress() && fx->get_progress()->position == 5, "a relinked file keeps its progress");
        check(lib.remove_file(lib.find_file(y)), "remove a read file");
        check(count_rows(db_path, "reading_progress") == 1, "a removed file's progress row is gone");

        db.close();
        check(!lib.set_progress(fx, page(6, 12)), "set_progress fails when the database does");
        check(fx->get_progress()->position == 5, "a failed set_progress leaves the progress");
        check(!lib.clear_progress(fx), "clear_progress fails when the database does");
        check(fx->get_progress() != nullptr, "a failed clear_progress leaves the progress");
        lib.attach(nullptr);
    }
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "reload a progress database: " + db.last_error());
        file* fx = lib.find_file(x_moved);
        check(fx && fx->get_progress() && fx->get_progress()->position == 5, "a relinked file's progress is saved");
        check(lib.get_files().size() == 1, "the removed file stays removed");
        check(fx && lib.clear_progress(fx), "clear progress with a database");
        check(count_rows(db_path, "reading_progress") == 0, "clear_progress deletes the row");
    }
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
        check(paths(lib.search("").files) == expected, "imported files are found");
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

    // In memory only: the new files have no tags.
    library mem;
    mem.edit_file(mem.add_file(a), mem.retrieve_tag("image"), 1);
    check(mem.add_files(scan.files) == 3 && mem.get_files().size() == 4, "add_files without a database");
    check(paths(mem.search("{image}").files) == std::set<std::string>{a}, "imported files are untagged");

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

// remove_files: many files at once, in memory and with a database, all or nothing.
static void bulk_removal(const std::filesystem::path &dir)
{
    library lib;
    fill(lib);
    file* a = lib.find_file("photos/a.jpg");
    file* c = lib.find_file("docs/c.txt");
    library other;
    file* foreign = other.add_file("photos/b.png");
    check(lib.remove_files({a, a, nullptr, foreign, c}) == 2, "remove_files skips repeats, nullptr and other libraries' files");
    check(lib.get_files().size() == 2 && lib.get_files()[0]->get_path() == "photos/b.png" &&
        lib.get_files()[1]->get_path() == "d.bin", "remove_files keeps the order of the files left");
    check(!lib.find_file("photos/a.jpg") && !lib.find_file("docs/c.txt"), "removed files can't be found");
    check(paths(lib.find_tag("image")->get_files()) == std::set<std::string>{"photos/b.png"}, "removed files leave their tags");
    check(lib.find_tag("cat")->get_files().empty() && lib.find_tag("document")->get_files().empty(),
        "remove_files keeps tags left with no file");
    check(lib.remove_files({}) == 0, "removing nothing");
    check(other.find_file("photos/b.png") == foreign, "another library's file is untouched");

    const std::filesystem::path db_path = dir / "core_test_bulk.db";
    remove_db(db_path);
    {
        database db;
        library l2;
        check(db.open(db_path) && open_library(l2, db, ""), "open database for remove_files: " + db.last_error());
        fill(l2);
        check(l2.remove_files({l2.find_file("photos/a.jpg"), l2.find_file("d.bin")}) == 2, "remove_files with a database");
        check(db.last_error().empty(), "remove_files writes without errors: " + db.last_error());
        db.close();
        file* b = l2.find_file("photos/b.png");
        check(l2.remove_files({b, l2.find_file("docs/c.txt")}) == -1, "remove_files fails when the database does");
        check(l2.find_file("photos/b.png") == b && l2.get_files().size() == 2 && b->get_tags().size() == 2,
            "a failed remove_files changes nothing");
        l2.attach(nullptr);
    }
    {
        database db;
        library l3;
        check(db.open(db_path) && open_library(l3, db, ""), "reload after remove_files: " + db.last_error());
        check(paths(l3.get_files()) == std::set<std::string>{"photos/b.png", "docs/c.txt"}, "removed files stay removed");
        check(l3.find_tag("cat") && l3.find_tag("cat")->get_files().empty(), "removed files' links are gone");
    }
    remove_db(db_path);
}

// snapshot_files and restore_files: removed files come back with their tags, progress and row ids.
static void restoring(const std::filesystem::path &dir)
{
    library lib;
    fill(lib);
    library other;
    file* foreign = other.add_file("x.jpg");
    std::vector<file_snapshot> snaps = lib.snapshot_files({lib.find_file("photos/a.jpg"), lib.find_file("docs/c.txt"), foreign});
    check(snaps.size() == 2 && snaps[0].path == "photos/a.jpg" && std::set<std::string>(snaps[0].tags.begin(), snaps[0].tags.end()) ==
        std::set<std::string>{"image", "cat"}, "snapshot_files takes down paths and tags, skipping other libraries' files");
    lib.remove_files({lib.find_file("photos/a.jpg"), lib.find_file("docs/c.txt")});
    lib.delete_tag(lib.find_tag("cat"));
    lib.create_tag("DOCUMENT");  // still there: keeps its own spelling
    check(lib.restore_files(snaps) == 2, "restore_files puts files back");
    file* a = lib.find_file("photos/a.jpg");
    check(a && tag_names(a) == std::set<std::string>{"image", "cat"}, "a restored file has its tags, a deleted one made again");
    check(lib.find_tag("cat") && paths(lib.find_tag("cat")->get_files()) == std::set<std::string>{"photos/a.jpg"},
        "a tag made again links back to the file");
    check(paths(lib.find_tag("image")->get_files()) == std::set<std::string>{"photos/a.jpg", "photos/b.png"}, "an existing tag gets the file back");
    file* c = lib.find_file("docs/c.txt");
    check(c && tag_names(c) == std::set<std::string>{"document"}, "a tag that is there keeps its spelling");
    check(lib.restore_files(snaps) == 0 && lib.get_files().size() == 4, "files already there are skipped");

    const std::filesystem::path db_path = dir / "core_test_restore.db";
    remove_db(db_path);
    {
        database db;
        library l2;
        check(db.open(db_path) && open_library(l2, db, ""), "open database for restore_files: " + db.last_error());
        fill(l2);
        file* b = l2.find_file("photos/b.png");
        reading_progress p;
        p.position = 3;
        p.length = 10;
        p.last_read = 1700000000;
        l2.set_progress(b, p);
        std::int64_t b_id = b->get_db_id();
        std::int64_t d_id = l2.find_file("d.bin")->get_db_id();
        std::vector<file_snapshot> s2 = l2.snapshot_files({b, l2.find_file("d.bin")});
        check(l2.remove_files({b, l2.find_file("d.bin")}) == 2, "remove before restoring");
        l2.delete_tag(l2.find_tag("dog"));
        // d.bin had the highest row id: a new file takes it.
        file* e = l2.add_file("e.txt");
        check(e && e->get_db_id() == d_id, "a new file reuses the last row id");
        check(l2.restore_files(s2) == 2, "restore_files with a database");
        check(db.last_error().empty(), "restore_files writes without errors: " + db.last_error());
        b = l2.find_file("photos/b.png");
        check(b && b->get_db_id() == b_id, "a restored file gets its row id back when it is free");
        check(l2.find_file("d.bin") && l2.find_file("d.bin")->get_db_id() != d_id && l2.find_file("d.bin")->get_db_id() != 0,
            "a restored file whose row id was taken gets a new one");
        check(b && b->get_progress() && b->get_progress()->position == 3 && b->get_progress()->last_read == 1700000000,
            "a restored file keeps its reading progress");

        std::vector<file_snapshot> s3 = l2.snapshot_files({l2.find_file("docs/c.txt")});
        l2.remove_files({l2.find_file("docs/c.txt")});
        db.close();
        check(l2.restore_files(s3) == -1 && !l2.find_file("docs/c.txt"), "a failed restore_files changes nothing");
        l2.attach(nullptr);
    }
    {
        database db;
        library l3;
        check(db.open(db_path) && open_library(l3, db, ""), "reload after restore_files: " + db.last_error());
        file* b = l3.find_file("photos/b.png");
        check(b && tag_names(b) == std::set<std::string>{"image", "dog"}, "restored links are saved");
        check(b && b->get_progress() && b->get_progress()->length == 10, "restored progress is saved");
        check(paths(l3.get_files()) == std::set<std::string>{"photos/a.jpg", "photos/b.png", "d.bin", "e.txt"},
            "restored files are saved");
    }
    remove_db(db_path);
}

// Mutators report a database failure and leave memory as it was.
static void write_failures(const std::filesystem::path &dir)
{
    const std::filesystem::path db_path = dir / "core_test_write_failures.db";
    remove_db(db_path);
    database db;
    library lib;
    check(db.open(db_path) && open_library(lib, db, ""), "open database for write failures: " + db.last_error());
    fill(lib);
    file* a = lib.find_file("photos/a.jpg");
    tag* dog = lib.find_tag("dog");
    db.close();

    check(lib.add_file("new.jpg") == nullptr && !lib.find_file("new.jpg") && lib.get_files().size() == 4,
        "add_file fails when the database does, adding nothing");
    check(!db.last_error().empty(), "a failed write leaves an error for the caller");
    db.clear_error();
    check(lib.add_file("photos/a.jpg") == a, "add_file of a path already there needs no write");
    check(lib.create_tag("new") == nullptr && lib.retrieve_tag("new") == nullptr && !lib.find_tag("new") &&
        lib.get_tags().size() == 4, "create_tag and retrieve_tag fail when the database does");
    check(lib.retrieve_tag("DOG") == dog, "retrieve_tag of a tag already there needs no write");
    check(!lib.edit_file(a, dog, 1) && !a->tagged(dog) && dog->get_files().size() == 1, "a failed tagging changes nothing");
    check(!lib.edit_file(a, lib.find_tag("cat"), 0) && a->tagged(lib.find_tag("cat")), "a failed untagging changes nothing");
    check(lib.edit_file(a, dog, 0), "untagging a tag the file doesn't have needs no write");
    lib.attach(nullptr);
    remove_db(db_path);
}

// Tag rows whose names differ only in case, from older builds, are merged into the oldest one when loading.
static void tag_case_duplicates(const std::filesystem::path &dir)
{
    const std::filesystem::path db_path = dir / "core_test_tag_case.db";
    remove_db(db_path);
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "set up tag case database: " + db.last_error());
        std::int64_t x = db.insert_file("x.jpg");
        std::int64_t y = db.insert_file("y.jpg");
        std::int64_t beach = db.insert_tag("Beach");
        std::int64_t lower = db.insert_tag("beach");
        std::int64_t upper = db.insert_tag("BEACH");
        db.insert_tag("Praia");
        check(beach && lower && upper && beach != lower && lower != upper, "the old schema allowed case variants");
        db.link(x, beach);
        db.link(y, lower);
        db.link(x, upper);
        check(db.last_error().empty(), "write case variants: " + db.last_error());
        lib.attach(nullptr);
    }
    for(int pass = 0; pass < 2; ++pass)
    {
        const std::string label = pass ? " (reloaded)" : "";
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "load tag case variants: " + db.last_error());
        check(lib.get_tags().size() == 2, "case variants load as one tag" + label);
        tag* t = lib.find_tag("bEaCh");
        check(t && t->get_id() == "Beach", "the oldest spelling stays" + label);
        check(t && paths(t->get_files()) == std::set<std::string>{"x.jpg", "y.jpg"}, "the merged tag has every file once" + label);
        check(lib.find_file("x.jpg")->get_tags().size() == 1, "a file with two variants has the tag once" + label);
        check(lib.retrieve_tag("BEACH") == t && lib.get_tags().size() == 2, "retrieve_tag finds the merged tag" + label);
        lib.attach(nullptr);
    }
    check(count_rows(db_path, "tags") == 2 && count_rows(db_path, "file_tags") == 2, "the extra rows are gone from the database");
    remove_db(db_path);
}

// Tag names, renaming and merging, bulk edits, and folder names as tags.
static void tagging_core(const std::filesystem::path &dir)
{
    check(is_valid_tag_name("cat") && is_valid_tag_name("Ação") && is_valid_tag_name("a-b"), "ordinary names are valid");
    check(!is_valid_tag_name("") && !is_valid_tag_name("a b") && !is_valid_tag_name("-x") && !is_valid_tag_name("a{b}")
        && !is_valid_tag_name("AND") && !is_valid_tag_name("a\tb"), "names the search can't filter by are invalid");
    check(normalize_tag_name("  Holiday  Photos ") == "Holiday_Photos", "normalize turns spaces into _");
    check(normalize_tag_name("(2019)") == "2019" && normalize_tag_name("--x--") == "x", "normalize trims edges");
    check(normalize_tag_name("   ").empty() && normalize_tag_name("NOT").empty() && normalize_tag_name("{}").empty(),
        "normalize gives nothing for unusable text");

    for(int with_db = 0; with_db < 2; ++with_db)
    {
        const std::string label = with_db ? " (database)" : "";
        const std::filesystem::path db_path = dir / "core_test_tagging.db";
        remove_db(db_path);
        database db;
        library lib;
        if(with_db)
        {
            check(db.open(db_path) && open_library(lib, db, ""), "open database for tagging: " + db.last_error());
        }
        fill(lib);
        file* a = lib.find_file("photos/a.jpg");
        file* b = lib.find_file("photos/b.png");
        file* c = lib.find_file("docs/c.txt");
        file* d = lib.find_file("d.bin");
        tag* image = lib.find_tag("image");
        tag* cat = lib.find_tag("cat");
        tag* dog = lib.find_tag("dog");

        // Renaming.
        tag* got = nullptr;
        check(lib.rename_tag(cat, "kitten", &got) == rename_result::Renamed && got == cat, "rename to a free name" + label);
        check(lib.find_tag("kitten") == cat && !lib.find_tag("cat") && a->tagged(cat), "rename updates lookup" + label);
        check(lib.rename_tag(cat, "kitten", &got) == rename_result::Unchanged && got == cat, "same name is unchanged" + label);
        check(lib.rename_tag(cat, "", &got) == rename_result::Invalid && !got, "empty name is invalid" + label);
        check(lib.rename_tag(cat, "two words") == rename_result::Invalid && cat->get_id() == "kitten", "name with space is invalid" + label);
        check(lib.rename_tag(nullptr, "x") == rename_result::Invalid, "no tag is invalid" + label);
        check(lib.rename_tag(cat, "Kitten") == rename_result::Renamed && cat->get_id() == "Kitten", "case-only change renames" + label);

        // Merge: dog -> IMAGE (case aside) keeps image's spelling and gets dog's files.
        lib.edit_file(c, dog, 1);
        check(lib.rename_tag(dog, "IMAGE", &got) == rename_result::Merged && got == image, "rename onto an existing tag merges" + label);
        check(image->get_id() == "image" && !lib.find_tag("dog"), "merged tag keeps the existing spelling, old one is gone" + label);
        check(paths(image->get_files()) == std::set<std::string>{"photos/a.jpg", "photos/b.png", "docs/c.txt"}, "merge takes the union" + label);
        check(c->get_tags().size() == 2, "merge leaves files with the target" + label);

        // Bulk edits.
        tag* t1 = lib.create_tag("t1");
        tag* t2 = lib.create_tag("t2");
        check(lib.edit_files({a, b, c, a, nullptr}, {t1, t2, t1}, true) == 6, "bulk add counts each new link once" + label);
        check(paths(lib.search("{t1}").files) == std::set<std::string>{"photos/a.jpg", "photos/b.png", "docs/c.txt"}, "bulk add shows in searches" + label);
        check(lib.edit_files({a, b}, {t1}, true) == 0, "bulk add of existing links changes nothing" + label);
        check(lib.edit_files({a, d}, {t2}, true) == 1 && d->tagged(t2), "bulk add skips files that have it" + label);
        check(lib.edit_files({a, b, d}, {t1, t2}, false) == 5 && !a->tagged(t1) && !d->tagged(t2), "bulk remove" + label);
        check(paths(lib.search("{t1}").files) == std::set<std::string>{"docs/c.txt"}, "bulk remove shows in searches" + label);
        check(lib.edit_files({}, {t1}, true) == 0, "bulk edit of nothing" + label);

        if(with_db)
        {
            check(db.last_error().empty(), "tagging writes without errors: " + db.last_error());
            // Reload: everything written, merge and rename included.
            library again;
            database db2;
            check(db2.open(db_path) && open_library(again, db2, ""), "reload tagging database");
            check(again.find_tag("Kitten") && again.find_tag("Kitten")->get_files().size() == 1, "rename persisted");
            check(!again.find_tag("dog") && again.find_tag("image")->get_files().size() == 3, "merge persisted");
            check(paths(again.find_tag("t1")->get_files()) == std::set<std::string>{"docs/c.txt"}, "bulk edits persisted");
            again.attach(nullptr);
            db2.close();

            // A closed database fails bulk edits and renames without changing memory.
            db.close();
            check(lib.edit_files({a}, {t1}, true) == -1 && !a->tagged(t1), "bulk edit fails with the database" + label);
            check(lib.rename_tag(t2, "t3") == rename_result::Failed && t2->get_id() == "t2" && lib.find_tag("t2") == t2, "rename fails with the database");
            check(lib.rename_tag(t2, "T1") == rename_result::Failed && lib.find_tag("t2") == t2, "merge fails with the database");
            lib.attach(nullptr);
        }
        lib.attach(nullptr);
        db.close();
        remove_db(db_path);
    }

    // Inside the caller's transaction a bulk edit joins it; a failure undoes only the bulk edit.
    {
        const std::filesystem::path db_path = dir / "core_test_tagging2.db";
        remove_db(db_path);
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "open database for transaction test");
        fill(lib);
        check(db.begin(), "begin");
        check(lib.edit_files(lib.get_files(), {lib.retrieve_tag("all")}, true) == 4, "bulk edit inside a transaction");
        db.rollback();
        library again;
        database db2;
        check(db2.open(db_path) && open_library(again, db2, ""), "reload after rollback");
        check(!again.find_tag("all") || again.find_tag("all")->get_files().empty(), "rolled back transaction takes the bulk edit with it");
        again.attach(nullptr);
        db2.close();
        lib.attach(nullptr);
        db.close(); // Windows can't delete a database that is still open
        remove_db(db_path);
    }

    // Folder names.
    namespace fs = std::filesystem;
    const std::string r = fs::absolute(dir / "Music Library").lexically_normal().u8string();
    auto in = [&](const std::string &rel) { return (fs::u8path(r) / fs::u8path(rel)).u8string(); };
    using names = std::vector<std::string>;
    check(folder_tags(r, in("a.mp3")) == names{"Music_Library"}, "a file in the root gets the root's name");
    check(folder_tags(r, in("Rock/Queen/a.mp3")) == (names{"Music_Library", "Rock", "Queen"}), "folders outermost first");
    check(folder_tags(r, in("rock/Rock/a.mp3")) == (names{"Music_Library", "rock"}), "repeated names appear once, case aside");
    check(folder_tags(r, in("(1)/--/a.mp3")) == (names{"Music_Library", "1"}), "unusable folder names are dropped");
    check(folder_tags(r, in("A/B/C/D/E/F/a.mp3"), 4) == (names{"Music_Library", "D", "E", "F"}), "deep nesting keeps the root and the nearest folders");
    check(folder_tags(r, in("A/a.mp3"), 1) == names{"Music_Library"}, "a limit of one keeps the root");
    check(folder_tags(r, (dir / "elsewhere" / "a.mp3").u8string()).empty(), "a file outside the root gets none");
    check(folder_tags(r + (r.back() == '/' || r.back() == '\\' ? "" : "/"), in("X/a.mp3")) == (names{"Music_Library", "X"}), "a trailing slash on the root is fine");
    check(folder_tags(fs::path(r).root_path().u8string(), (fs::path(r).root_path() / "Photos" / "a.jpg").u8string())
        == names{"Photos"}, "a drive root has no name");

    std::vector<scanned_file> scanned = {{in("Rock/a.mp3"), ""}, {in("Rock/b.mp3"), ""}, {in("rock/c.mp3"), ""}, {in("d.mp3"), ""}};
    auto sug = suggest_folder_tags(r, scanned);
    check(sug.size() == 2 && sug[0].name == "Music_Library" && sug[0].files == (std::vector<std::size_t>{0, 1, 2, 3}),
        "suggestions: root name on every file");
    check(sug.size() == 2 && sug[1].name == "Rock" && sug[1].files == (std::vector<std::size_t>{0, 1, 2}),
        "suggestions group folders that differ in case");
}

// Watched folders: stored in the database, rescans add what is new (not temporary or removed files) and relink
// moved files by identity.
static void watched_folders(const std::filesystem::path &dir)
{
    namespace fs = std::filesystem;
    check(temporary_file("a/b/movie.mp4.crdownload") && temporary_file("x/~$report.docx") && temporary_file("x/.~lock.a.odt#") &&
        temporary_file("x/A.TMP") && temporary_file("x/song.part"), "temporary_file knows downloads, temp and lock files");
    check(!temporary_file("x/part") && !temporary_file("x/photo.jpg") && !temporary_file("x/.tmp") && !temporary_file("x/temp/a.txt"),
        "temporary_file leaves other files alone");

    const fs::path root = dir / "core_test_watch";
    fs::remove_all(root);
    touch(root / "a.txt", "a");
    touch(root / "sub" / "b.txt", "b");
    const fs::path db_path = dir / "core_test_watch.db";
    remove_db(db_path);
    const std::string root_s = root.u8string();
    auto known_paths = [](const library &lib)
    {
        std::unordered_set<std::string> known;
        for(const file* f : lib.get_files())
            known.insert(f->get_path());
        return known;
    };
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "open database for watched folders: " + db.last_error());
        check(lib.add_files(scan_folder(root_s).files) == 2, "import the folder to watch");
        check(lib.watch_folder(root_s + "/") && lib.watched_folders() == std::vector<std::string>{root_s},
            "watch_folder keeps the folder without a trailing separator");
        check(lib.watch_folder((root / "sub").u8string()) && lib.watched_folders().size() == 1, "a folder under a watched one is covered");
        check(lib.is_watched((root / "sub" / "b.txt").u8string()) && !lib.is_watched((dir / "elsewhere.txt").u8string()),
            "is_watched tells what is under a watched folder");

        touch(root / "c.txt", "c");
        touch(root / "c.tmp", "t");
        touch(root / "sub" / "d.jpg.crdownload", "d");
        std::unordered_set<std::string> known = known_paths(lib);
        folder_scan scan = scan_folder(root_s, nullptr, nullptr, &known);
        check(scan.files.size() == 3, "scan_folder leaves known files out");
        watch_sync sync = lib.sync_watched(scan.files);
        check(!sync.failed && sync.added.size() == 1 && sync.added[0]->get_path() == (root / "c.txt").u8string() &&
            sync.relinked.empty(), "sync_watched adds the new file, not temporary ones");
        check(lib.sync_watched(scan_folder(root_s).files).added.empty(), "a second sync adds nothing");

        check(lib.remove_file(lib.find_file((root / "c.txt").u8string())), "remove a watched file");
        check(lib.sync_watched(scan_folder(root_s).files).added.empty(), "a file removed from a watched folder isn't added back");

        file* b = lib.find_file((root / "sub" / "b.txt").u8string());
        lib.edit_file(b, lib.retrieve_tag("bee"), true);
        fs::create_directories(root / "sub2");
        fs::rename(root / "sub" / "b.txt", root / "sub2" / "b2.txt");
        sync = lib.sync_watched(scan_folder(root_s, nullptr, nullptr, &known).files);
        file* moved = lib.find_file((root / "sub2" / "b2.txt").u8string());
        check(!sync.failed && sync.relinked.size() == 1 && sync.added.empty() && moved == b &&
            sync.relinked_from == std::vector<std::string>{(root / "sub" / "b.txt").u8string()} && tag_names(moved) == std::set<std::string>{"bee"},
            "a missing file found by identity is relinked with its tags");
        check(db.last_error().empty(), "watched folder writes without errors: " + db.last_error());
        lib.attach(nullptr);
    }
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "reopen database for watched folders: " + db.last_error());
        check(lib.watched_folders() == std::vector<std::string>{root_s}, "watched folders are saved");
        check(lib.find_file((root / "sub2" / "b2.txt").u8string()) && !lib.find_file((root / "sub" / "b.txt").u8string()),
            "a relink by a sync is saved");
        check(lib.sync_watched(scan_folder(root_s).files).added.empty(), "a removed file stays out after a reopen");
        check(lib.ignore_file((root / "c.txt").u8string(), "another:file") &&
            lib.sync_watched(scan_folder(root_s).files).added.size() == 1, "a file of another identity at an ignored path is added");
        lib.remove_file(lib.find_file((root / "c.txt").u8string()));
        const std::string c_path = (root / "c.txt").u8string();
        check(lib.add_files({{c_path, file_identity(c_path)}}) == 1 && lib.remove_file(lib.find_file((root / "c.txt").u8string())),
            "adding a removed file by hand brings it back");
        std::vector<file_snapshot> snaps = lib.snapshot_files({lib.find_file((root / "a.txt").u8string())});
        lib.remove_files({lib.find_file((root / "a.txt").u8string())});
        check(lib.restore_files(snaps) == 1, "restore a removed watched file");

        check(lib.watch_folder((root / "sub2").u8string()) && lib.unwatch_folder(root_s + "/") && lib.watched_folders().empty(),
            "unwatch_folder stops watching");
        check(lib.watch_folder((root / "sub2").u8string()) && lib.watch_folder(root_s) && lib.watched_folders() == std::vector<std::string>{root_s},
            "a folder above a watched one takes its place");
        check(lib.unwatch_folder(root_s) && !lib.unwatch_folder(root_s), "a folder no longer watched can't be unwatched");
        check(lib.find_file((root / "a.txt").u8string()) && lib.get_files().size() == 2, "unwatching keeps the files");
        lib.attach(nullptr);
    }
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, "") && lib.watched_folders().empty(), "unwatching is saved");
        check(lib.watch_folder(root_s) && lib.sync_watched(scan_folder(root_s).files).added.empty(),
            "a sync after reopening adds nothing new");
        db.close();
        check(!lib.watch_folder((dir / "other").u8string()) && lib.watched_folders().size() == 1, "a failed watch_folder changes nothing");
        check(!lib.unwatch_folder(root_s) && lib.watched_folders().size() == 1, "a failed unwatch_folder changes nothing");
        touch(root / "e.txt", "e");
        watch_sync failed = lib.sync_watched(scan_folder(root_s).files);
        check(failed.failed && !lib.find_file((root / "e.txt").u8string()), "a failed sync_watched changes nothing");
        lib.attach(nullptr);
    }
    remove_db(db_path);
    fs::remove_all(root);
}


static void catalog_backup(const std::filesystem::path &dir)
{
    namespace fs = std::filesystem;
    fs::path work = dir / "backup-test";
    fs::remove_all(work);
    fs::create_directories(work);
    fs::path db_path = work / "a.db";
    std::string odd = "/music/caf\xC3\xA9 \"quoted\" \\ back\tslash.mp3";

    // Export a catalog with odd paths, tags, progress, a watched folder and an empty tag.
    std::string exported = (work / "export.json").u8string();
    {
        database db;
        library lib;
        check(db.open(db_path) && open_library(lib, db, ""), "backup: open");
        file* f1 = lib.add_file(odd);
        file* f2 = lib.add_file("/books/novel.epub");
        file* f3 = lib.add_file("/pics/plain.png");
        tag* t1 = lib.retrieve_tag("Music");
        tag* t2 = lib.retrieve_tag("Caf\xC3\xA9");
        lib.edit_file(f1, t1, true);
        lib.edit_file(f1, t2, true);
        lib.edit_file(f2, t1, true);
        lib.retrieve_tag("empty");
        reading_progress p;
        p.position = 41;
        p.length = 300;
        p.location = "epubcfi(/6/4)";
        lib.set_progress(f2, p);
        lib.watch_folder((work / "watched").u8string());
        (void)f3;
        std::string error;
        check(export_catalog(lib, exported, error), "backup: export " + error);
        check(!fs::exists(exported + ".tmp"), "backup: no temporary export left");
        lib.attach(nullptr);
    }

    // Import into an empty library: everything comes back.
    {
        library lib;
        catalog_import r = import_catalog(lib, exported);
        check(r.ok && r.files_added == 3 && r.skipped == 0, "backup: import into empty");
        file* f1 = lib.find_file(odd);
        check(f1 && tag_names(f1) == std::set<std::string>{"Music", "Caf\xC3\xA9"}, "backup: odd path and its tags");
        file* f2 = lib.find_file("/books/novel.epub");
        check(f2 && f2->get_progress() && f2->get_progress()->position == 41 && f2->get_progress()->length == 300
            && f2->get_progress()->location == "epubcfi(/6/4)", "backup: progress");
        check(lib.find_tag("empty") && lib.get_tags().size() == 3, "backup: empty tag kept");
        check(lib.watched_folders().size() == 1, "backup: watched folder");
    }

    // Import into a library that already has some of it: merge, never remove or replace.
    {
        database db;
        library lib;
        fs::path db2 = work / "b.db";
        check(db.open(db2) && open_library(lib, db, ""), "backup: open second");
        file* f1 = lib.add_file(odd);
        lib.edit_file(f1, lib.retrieve_tag("MUSIC"), true);
        file* mine = lib.add_file("/mine/only.txt");
        lib.edit_file(mine, lib.retrieve_tag("mine"), true);
        catalog_import r = import_catalog(lib, exported);
        check(r.ok && r.files_added == 2 && r.files_updated == 1, "backup: merge counts");
        check(tag_names(lib.find_file(odd)) == std::set<std::string>{"MUSIC", "Caf\xC3\xA9"}, "backup: tags merged case-insensitively");
        check(lib.find_file("/mine/only.txt") && tag_names(lib.find_file("/mine/only.txt")) == std::set<std::string>{"mine"}, "backup: own files untouched");
        check(lib.get_files().size() == 4, "backup: no duplicates");
        check(import_catalog(lib, exported).files_added == 0, "backup: importing twice adds nothing");
        lib.attach(nullptr);
        db.close();
        database again;
        library reloaded;
        check(again.open(db2) && open_library(reloaded, again, "") && reloaded.get_files().size() == 4
            && reloaded.find_file("/books/novel.epub") && reloaded.find_file("/books/novel.epub")->get_progress(), "backup: import is saved");
    }

    // Bad files change nothing.
    {
        library lib;
        touch(work / "bad.json", "{\"format\": \"other\", \"files\": []}");
        touch(work / "broken.json", "{\"format\": \"corpus-catalog\", \"version\": 1, \"files\": [");
        touch(work / "newer.json", "{\"format\": \"corpus-catalog\", \"version\": 99, \"files\": []}");
        check(!import_catalog(lib, work / "bad.json").ok && !import_catalog(lib, work / "broken.json").ok
            && !import_catalog(lib, work / "newer.json").ok && !import_catalog(lib, work / "missing.json").ok, "backup: bad files refused");
        touch(work / "partial.json", "{\"format\": \"corpus-catalog\", \"version\": 1, \"files\": [{\"path\": \"/x\", \"tags\": [\"ok\", \"bad tag\", 5]}, 7, {\"path\": \"\"}]}");
        catalog_import r = import_catalog(lib, work / "partial.json");
        check(r.ok && r.files_added == 1 && r.skipped == 4 && tag_names(lib.find_file("/x")) == std::set<std::string>{"ok"}, "backup: invalid entries skipped");
        touch(work / "unicode.json", "{\"format\": \"corpus-catalog\", \"version\": 1, \"files\": [{\"path\": \"/\\u00e9\\ud83d\\ude00\", \"tags\": []}]}");
        check(import_catalog(lib, work / "unicode.json").ok && lib.find_file("/\xC3\xA9\xF0\x9F\x98\x80"), "backup: \\u escapes and surrogates");
    }

    // A failed database write rolls the import back.
    {
        database db;
        library lib;
        fs::path db3 = work / "c.db";
        check(db.open(db3) && open_library(lib, db, ""), "backup: open third");
        db.close();
        check(!import_catalog(lib, exported).ok, "backup: import fails when the database does");
        lib.attach(nullptr);
    }

    // Rolling database backups.
    {
        database db;
        library lib;
        fs::path folder = work / "backups";
        check(db.open(work / "d.db") && open_library(lib, db, ""), "backup: open fourth");
        backup_result empty = backup_database(db, folder, 3, 0);
        check(empty.ok && !empty.made && list_backups(folder).empty(), "backup: an empty catalog is not backed up");
        file* f = lib.add_file("/a/b.txt");
        lib.edit_file(f, lib.retrieve_tag("t"), true);
        backup_result first = backup_database(db, folder, 3, 3600);
        check(first.ok && first.made && fs::exists(first.file), "backup: first backup made");
        backup_result soon = backup_database(db, folder, 3, 3600);
        check(soon.ok && !soon.made, "backup: not again within the interval");
        std::string error;
        check(database::verify_file(first.file, error), "backup: backup verifies " + error);
        for(int i = 0; i < 4; i++)
            check(backup_database(db, folder, 3, 0).made, "backup: forced backup");
        std::vector<backup_info> list = backup_database(db, folder, 3, 0).ok ? list_backups(folder) : std::vector<backup_info>{};
        check(list.size() == 3 && list[0].when > list[1].when && list[1].when > list[2].when, "backup: only the newest are kept");
        touch(folder / "notes.txt", "keep me");
        touch(folder / "corpus-xx.db", "not a backup");
        backup_database(db, folder, 1, 0);
        check(list_backups(folder).size() == 1 && fs::exists(folder / "notes.txt") && fs::exists(folder / "corpus-xx.db"), "backup: other files left alone");

        // Restore it over a damaged catalog.
        fs::path newest = list_backups(folder).front().file;
        lib.attach(nullptr);
        db.close();
        touch(work / "d.db", "garbage");
        touch(work / "d.db-wal", "stale");
        check(restore_database(newest, work / "d.db", error), "backup: restore " + error);
        check(fs::exists(work / "d.db.before-restore") && !fs::exists(work / "d.db-wal"), "backup: old file kept aside, wal removed");
        database restored;
        library back;
        check(restored.open(work / "d.db") && open_library(back, restored, "") && back.find_file("/a/b.txt")
            && tag_names(back.find_file("/a/b.txt")) == std::set<std::string>{"t"}, "backup: restored catalog opens with its data");
        back.attach(nullptr);
        restored.close();
        check(!restore_database(work / "d.db.before-restore", work / "e.db", error) && !fs::exists(work / "e.db"), "backup: garbage refused");
        check(!database::verify_file(work / "missing.db", error), "backup: missing file refused");
    }
    fs::remove_all(work);
}

int main(int argc, char* argv[])
{
    std::filesystem::path dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
    std::filesystem::create_directories(dir);

    tag_query_parsing();
    tag_query_malformed();
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
    reading_progress_tests(dir);
    folder_import(dir);
    long_paths(dir);
    tagging_core(dir);
    bulk_removal(dir);
    restoring(dir);
    write_failures(dir);
    tag_case_duplicates(dir);
    watched_folders(dir);
    catalog_backup(dir);

    std::cout << (failures ? "FAILED" : "OK") << '\n';
    return failures ? 1 : 0;
}
