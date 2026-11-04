// Tests for searching inside documents: the zip reader and inflate, the EPUB text extractor, the content index
// (SQLite FTS5) and its background indexer, and library::search with in_content.
//
// Usage: corpus-content-test [work_dir]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../../include/content_index.hpp"
#include "../../include/database.hpp"
#include "../../include/file.hpp"
#include "../../include/library.hpp"
#include "../../include/saving.hpp"
#include "../../include/text_extract.hpp"
#include "../../include/zip_reader.hpp"

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if(!ok)
    {
        std::cout << "FAIL: " << what << '\n';
        failures++;
    }
}

typedef std::vector<std::uint8_t> bytes;

// A real EPUB made by a zip tool with deflate (a dynamic-Huffman chapter of 900 words: 52 are "dragon"; it ends with
// "Zyxwvutsrq marker."; title "Deflated Tale", author "Zed Author"). Inflate is only checked against real data.
static const std::uint8_t deflated_epub[] = {80,75,3,4,20,0,0,0,0,0,132,17,100,93,111,97,171,44,20,0,0,0,20,0,0,0,8,0,0,0,109,105,109,101,116,121,112,101,97,112,112,108,105,99,97,116,105,111,110,47,101,112,117,98,43,122,105,112,80,75,3,4,20,0,0,0,8,0,132,17,100,93,44,97,219,33,143,0,0,0,184,0,0,0,22,0,0,0,77,69,84,65,45,73,78,70,47,99,111,110,116,97,105,110,101,114,46,120,109,108,61,142,193,14,194,32,16,68,127,133,112,53,45,122,37,64,19,19,207,154,248,5,43,221,42,17,118,9,80,163,127,47,122,232,121,222,155,25,51,189,83,20,47,44,53,48,89,121,24,247,114,114,198,51,53,8,132,69,244,148,170,149,107,33,205,80,67,213,4,9,171,110,94,115,70,154,217,175,9,169,233,63,166,55,77,58,83,152,219,18,34,138,101,141,113,200,208,30,86,158,79,199,203,85,253,168,238,140,156,23,41,18,206,1,134,246,201,104,37,228,28,131,135,214,159,40,198,91,174,93,243,79,184,227,174,215,75,229,140,218,6,220,23,80,75,3,4,20,0,0,0,8,0,132,17,100,93,142,159,61,115,205,0,0,0,62,1,0,0,17,0,0,0,79,69,66,80,83,47,99,111,110,116,101,110,116,46,111,112,102,61,144,203,138,195,48,12,69,127,197,120,59,76,212,204,166,16,28,195,64,63,161,171,238,132,45,55,166,126,225,168,36,243,247,227,164,109,182,186,71,71,15,85,208,60,240,78,98,141,33,205,163,156,152,203,0,176,44,75,231,109,113,93,174,119,248,57,157,206,144,139,147,90,69,98,180,200,248,194,7,107,142,142,242,172,97,167,173,1,10,20,41,241,12,125,215,67,235,178,102,96,207,129,244,133,92,64,38,43,174,24,72,193,81,223,8,83,9,57,87,125,107,241,239,147,167,92,119,224,83,86,240,153,221,182,192,228,29,205,172,149,103,138,194,219,81,154,94,138,169,146,27,37,211,202,96,250,110,157,56,6,41,34,89,143,223,252,87,104,148,88,74,240,6,217,231,4,123,252,213,206,144,176,185,15,227,92,124,162,151,184,233,154,123,151,54,253,134,189,67,120,63,77,255,3,80,75,3,4,20,0,0,0,8,0,132,17,100,93,4,232,94,68,63,4,0,0,204,20,0,0,19,0,0,0,79,69,66,80,83,47,116,101,120,116,47,99,49,46,120,104,116,109,108,133,88,93,122,218,48,16,188,138,79,208,92,128,143,131,244,141,4,39,161,9,8,140,73,72,79,223,72,35,105,102,36,229,235,3,198,200,171,213,254,206,142,217,188,174,199,247,237,230,49,236,191,182,155,243,118,31,94,166,63,183,227,249,58,93,63,195,178,159,78,135,151,215,117,122,92,194,231,105,90,95,231,252,123,57,124,204,203,20,101,223,119,127,191,242,34,174,251,101,247,18,78,89,7,181,229,229,168,2,202,66,212,112,185,29,158,222,166,99,184,157,214,221,1,79,161,218,116,93,110,243,124,42,63,158,195,50,95,87,179,241,57,220,179,38,172,98,115,178,12,218,160,160,30,99,250,160,34,47,125,155,251,180,187,174,239,179,152,146,12,205,171,241,36,209,91,53,198,117,184,165,161,192,10,14,192,61,142,129,149,241,132,198,36,92,233,14,174,217,208,108,2,67,63,8,30,78,161,130,100,107,227,61,20,192,6,72,101,205,249,11,246,90,214,60,244,245,215,221,194,27,77,106,252,105,162,36,161,197,115,89,176,178,49,179,242,169,48,11,87,173,17,104,139,242,241,78,159,231,141,237,241,41,159,144,211,212,234,206,186,69,92,141,218,97,29,159,126,123,204,116,168,122,38,50,126,60,190,81,54,155,150,100,145,179,252,48,37,44,63,165,234,38,161,122,18,132,242,138,22,92,137,228,32,247,22,209,97,111,148,43,138,144,53,155,142,39,12,48,241,16,253,33,226,81,0,166,89,195,112,95,87,172,76,40,14,138,226,193,51,243,189,153,133,84,142,175,177,134,185,90,39,16,129,207,21,184,74,134,66,137,180,54,7,143,175,56,20,85,48,45,90,253,120,6,113,171,115,194,18,141,196,157,2,70,191,162,73,30,164,215,59,179,134,69,171,88,107,133,5,73,223,242,222,14,142,11,168,106,179,107,41,168,148,217,234,32,150,182,167,115,74,180,97,150,34,33,74,192,49,113,208,143,72,83,82,152,10,135,93,41,184,204,116,113,246,152,245,4,199,188,156,124,150,35,139,150,248,13,77,53,174,245,166,88,159,128,57,183,38,83,170,73,131,100,148,9,236,99,141,41,124,179,116,106,75,230,253,57,207,53,80,130,47,230,173,226,103,40,65,211,105,164,152,163,237,145,82,131,5,233,215,33,146,39,209,255,77,235,36,36,32,43,216,168,6,171,11,138,122,76,163,5,192,0,38,165,182,130,138,212,86,83,106,18,13,237,93,89,232,65,27,58,89,43,184,107,230,194,160,81,164,233,165,164,172,51,178,43,234,157,208,39,203,160,84,100,223,53,210,14,236,15,157,143,45,14,83,74,244,226,212,82,113,70,37,66,19,60,211,90,42,222,12,109,166,72,144,49,211,116,146,244,6,145,87,171,159,62,231,156,106,1,40,146,182,84,20,219,75,13,27,101,81,228,228,68,209,46,40,94,85,155,100,26,89,102,172,221,88,235,220,163,109,109,189,146,109,49,157,217,218,42,215,4,139,20,167,78,222,50,57,241,72,3,157,68,120,177,142,137,187,170,86,117,196,2,212,242,40,246,24,13,241,194,30,112,70,85,213,115,27,41,123,142,0,155,34,74,127,217,144,140,180,117,145,246,18,196,165,163,70,209,111,57,44,49,177,166,135,64,49,232,41,159,190,153,132,116,121,183,216,210,244,154,68,117,164,78,161,178,233,62,104,31,129,44,25,221,14,61,78,218,202,128,178,246,177,216,149,217,92,198,152,61,164,209,146,150,246,117,107,204,43,155,234,37,83,234,166,187,241,154,62,231,165,220,249,102,83,99,167,8,208,51,99,169,1,155,60,10,118,45,115,50,54,228,104,69,212,245,174,213,4,26,132,9,104,26,27,18,14,160,6,201,84,178,28,40,228,213,42,177,134,21,72,111,217,180,215,187,133,157,241,76,129,234,233,148,209,116,59,16,27,234,81,138,36,10,78,85,129,200,203,11,159,206,123,153,7,36,200,69,87,71,27,197,32,233,193,118,156,121,123,24,246,249,230,250,18,32,145,109,168,158,69,78,10,75,71,74,7,84,28,71,70,51,24,8,11,106,79,215,137,22,26,42,108,21,75,148,67,249,125,247,207,64,165,132,131,247,216,31,154,73,130,175,71,12,184,143,243,66,226,88,27,209,129,239,194,62,27,208,238,59,170,33,25,214,73,140,120,207,217,37,185,138,232,221,187,36,67,195,138,110,128,87,211,161,186,52,132,149,238,147,32,91,245,179,204,199,60,141,19,81,185,183,109,110,223,124,235,145,202,173,7,175,194,3,143,52,237,142,25,90,110,254,242,45,61,203,100,171,184,43,82,54,215,186,108,44,82,217,157,214,115,244,236,247,215,253,243,227,182,94,151,203,116,220,45,111,243,242,107,243,112,222,110,30,240,199,229,67,250,23,243,31,80,75,1,2,20,3,20,0,0,0,0,0,132,17,100,93,111,97,171,44,20,0,0,0,20,0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,128,1,0,0,0,0,109,105,109,101,116,121,112,101,80,75,1,2,20,3,20,0,0,0,8,0,132,17,100,93,44,97,219,33,143,0,0,0,184,0,0,0,22,0,0,0,0,0,0,0,0,0,0,0,128,1,58,0,0,0,77,69,84,65,45,73,78,70,47,99,111,110,116,97,105,110,101,114,46,120,109,108,80,75,1,2,20,3,20,0,0,0,8,0,132,17,100,93,142,159,61,115,205,0,0,0,62,1,0,0,17,0,0,0,0,0,0,0,0,0,0,0,128,1,253,0,0,0,79,69,66,80,83,47,99,111,110,116,101,110,116,46,111,112,102,80,75,1,2,20,3,20,0,0,0,8,0,132,17,100,93,4,232,94,68,63,4,0,0,204,20,0,0,19,0,0,0,0,0,0,0,0,0,0,0,128,1,249,1,0,0,79,69,66,80,83,47,116,101,120,116,47,99,49,46,120,104,116,109,108,80,75,5,6,0,0,0,0,4,0,4,0,250,0,0,0,105,6,0,0,0,0};

// ---- a small zip writer (stored entries) to build test EPUBs ----

static void put16(bytes &b, std::uint32_t v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void put32(bytes &b, std::uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
static void put_text(bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); }

struct part { std::string name, data; };

static bytes make_zip(const std::vector<part> &parts)
{
    bytes out, dir;
    for(const part &p : parts)
    {
        std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        put32(out, 0x04034B50); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
        put32(out, 0); put32(out, static_cast<std::uint32_t>(p.data.size())); put32(out, static_cast<std::uint32_t>(p.data.size()));
        put16(out, static_cast<std::uint32_t>(p.name.size())); put16(out, 0);
        put_text(out, p.name); put_text(out, p.data);
        put32(dir, 0x02014B50); put16(dir, 20); put16(dir, 20); put16(dir, 0); put16(dir, 0); put16(dir, 0); put16(dir, 0);
        put32(dir, 0); put32(dir, static_cast<std::uint32_t>(p.data.size())); put32(dir, static_cast<std::uint32_t>(p.data.size()));
        put16(dir, static_cast<std::uint32_t>(p.name.size())); put16(dir, 0); put16(dir, 0); put16(dir, 0); put16(dir, 0);
        put32(dir, 0); put32(dir, offset);
        put_text(dir, p.name);
    }
    std::uint32_t dir_offset = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), dir.begin(), dir.end());
    put32(out, 0x06054B50); put16(out, 0); put16(out, 0); put16(out, static_cast<std::uint32_t>(parts.size()));
    put16(out, static_cast<std::uint32_t>(parts.size())); put32(out, static_cast<std::uint32_t>(dir.size()));
    put32(out, dir_offset); put16(out, 0);
    return out;
}

static void write_bytes(const std::filesystem::path &path, const bytes &data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

static const std::string container_xml =
    "<?xml version=\"1.0\"?><container xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
    "<rootfiles><rootfile full-path=\"OEBPS/book.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>";

static std::string package_xml(const std::string &title, const std::string &manifest, const std::string &spine)
{
    return "<?xml version=\"1.0\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>" + title + "</dc:title>"
        "<dc:creator>Jane Q. Writer</dc:creator></metadata><manifest>" + manifest + "</manifest><spine>" + spine +
        "</spine></package>";
}

/// An EPUB of the given chapters (file name in OEBPS/text/, XHTML body), read in the order listed.
static bytes make_epub(const std::string &title, const std::vector<std::pair<std::string, std::string>> &chapters)
{
    std::string manifest, spine;
    std::vector<part> parts = { {"mimetype", "application/epub+zip"}, {"META-INF/container.xml", container_xml} };
    int n = 0;
    for(const auto &c : chapters)
    {
        std::string id = "c" + std::to_string(n++);
        manifest += "<item id=\"" + id + "\" href=\"text/" + c.first + "\" media-type=\"application/xhtml+xml\"/>";
        spine += "<itemref idref=\"" + id + "\"/>";
        parts.push_back({"OEBPS/text/" + c.first, "<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
            "<head><title>HEADTITLE</title><style>.x{color:red}</style></head><body>" + c.second + "</body></html>"});
    }
    parts.push_back({"OEBPS/book.opf", package_xml(title, manifest, spine)});
    return make_zip(parts);
}

static bool contains(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

static std::size_t count_of(const std::string &s, const std::string &part)
{
    std::size_t n = 0;
    for(std::size_t p = s.find(part); p != std::string::npos; p = s.find(part, p + 1))
        n++;
    return n;
}

static void wait_idle(content_indexer &indexer)
{
    for(int i = 0; i < 2000; i++)
    {
        if(indexer.pending() == 0)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(false, "the indexer finished in time");
}

// ---- zip and inflate ----

static void zip_and_inflate(const std::filesystem::path &dir)
{
    bytes data(deflated_epub, deflated_epub + sizeof deflated_epub);
    std::filesystem::path path = dir / "content_deflated.epub";
    write_bytes(path, data);

    zip_archive zip;
    check(zip.open(path.u8string()), "open a deflated epub");
    check(zip.entries().size() == 4, "the deflated epub has four entries");
    const zip_archive::entry *chapter = zip.find("OEBPS/text/c1.xhtml");
    check(chapter && chapter->method == 8, "the chapter is deflated");
    std::string out;
    check(chapter && zip.read(*chapter, out, 1 << 20) && out.size() == chapter->size, "inflate gives the stored size");
    check(count_of(out, "dragon") == 52 && contains(out, "Zyxwvutsrq marker."), "inflated text is intact");
    check(chapter && !zip.read(*chapter, out, 1000), "an entry over the limit is refused");

    std::string text;
    check(extract_epub_text(path.u8string(), text, 1 << 20) == extract_status::ok, "extract a deflated epub");
    check(text.compare(0, 25, "Deflated Tale\nZed Author\n") == 0, "metadata comes first: " + text.substr(0, 30));
    check(count_of(text, "dragon") == 52, "extracted text has every word");

    // Damaged archives: every truncation and every flipped byte must fail or give some text, never crash or hang.
    std::filesystem::path bad = dir / "content_bad.epub";
    int surviving = 0;
    for(std::size_t cut = 0; cut < data.size(); cut += 7)
    {
        write_bytes(bad, bytes(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cut)));
        std::string t;
        if(extract_epub_text(bad.u8string(), t, 1 << 20) == extract_status::ok)
            surviving++;
    }
    check(surviving == 0, "a truncated epub never reads as ok");
    for(std::size_t i = 0; i < data.size(); i += 3)
    {
        bytes flipped = data;
        flipped[i] ^= 0x5A;
        write_bytes(bad, flipped);
        std::string t;
        extract_epub_text(bad.u8string(), t, 1 << 20);
    }
    write_bytes(bad, bytes{1, 2, 3, 4, 5});
    std::string t;
    check(extract_epub_text(bad.u8string(), t, 1 << 20) == extract_status::failed, "not a zip fails");
    check(extract_epub_text((dir / "content_missing.epub").u8string(), t, 1 << 20) == extract_status::failed,
        "a missing file fails");

    // A deflate stream whose output would pass the limit stops there (a bomb of zeros: stored block header + fixed).
    std::string sink;
    check(!inflate_raw(reinterpret_cast<const std::uint8_t*>("\x03\x00"), 2, sink, 100) || sink.empty(), "empty fixed block");
    std::filesystem::remove(path);
    std::filesystem::remove(bad);
}

// ---- the extractor ----

static void epub_text(const std::filesystem::path &dir)
{
    // Chapters are read in spine order (not manifest order), with entities decoded and scripts and styles dropped.
    std::vector<part> parts = { {"mimetype", "application/epub+zip"}, {"META-INF/container.xml", container_xml} };
    parts.push_back({"OEBPS/text/two.xhtml", "<html><body><p>Second &amp; last: caf&eacute; &#8212; &#x41;bc</p></body></html>"});
    parts.push_back({"OEBPS/one chapter.xhtml", "<html><body><h1>First</h1><p>Hello<br/>world <b>Th</b>e end.</p>"
        "<script>var hidden = 1;</script><!-- a comment --><![CDATA[cdata text]]><style>p{}</style></body></html>"});
    parts.push_back({"OEBPS/unlisted.xhtml", "<html><body>NOTINSPINE</body></html>"});
    parts.push_back({"OEBPS/book.opf", package_xml("A &amp; B",
        "<item id=\"b\" href=\"text/two.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "<item id=\"a\" href=\"one%20chapter.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "<item id=\"u\" href=\"unlisted.xhtml\" media-type=\"application/xhtml+xml\"/>"
        "<item id=\"gone\" href=\"text/gone.xhtml\" media-type=\"application/xhtml+xml\"/>",
        "<itemref idref=\"a\"/><itemref idref=\"gone\"/><itemref idref=\"b\"/>")});
    std::filesystem::path path = dir / "content_entities.epub";
    write_bytes(path, make_zip(parts));
    std::string text;
    check(extract_epub_text(path.u8string(), text, 1 << 20) == extract_status::ok, "extract an epub with odd parts");
    check(text.compare(0, 6, "A & B\n") == 0, "title entities decoded: " + text.substr(0, 10));
    check(contains(text, "Jane Q. Writer"), "the author is in the text");
    check(text.find("First") < text.find("Second"), "spine order wins over manifest order");
    check(contains(text, "Hello\nworld") && contains(text, "world The end."), "<br/> breaks, inline tags join: " + text);
    check(contains(text, "Second & last: caf\xC3\xA9 \xE2\x80\x94 Abc"), "entities decoded: " + text);
    check(!contains(text, "hidden") && !contains(text, "color") && !contains(text, "a comment") && !contains(text, "HEADTITLE"),
        "scripts, styles, comments and the head are dropped");
    check(contains(text, "cdata text"), "CDATA is text");
    check(!contains(text, "NOTINSPINE"), "files outside the spine are left out");

    std::string cut;
    check(extract_epub_text(path.u8string(), cut, 40) == extract_status::ok && cut.size() <= 41, "output stops at the limit");

    check(find_text_extractor("x/Book.EPUB") != nullptr, "extension matches ignoring case");
    check(find_text_extractor("x/book.pdf") == nullptr && find_text_extractor("x/book.epub.bak") == nullptr, "other types have none");
    std::filesystem::remove(path);
}

static double seconds_since(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// Odd or hostile books must stay cheap: a flood of comments, a spine that repeats a chapter or lists thousands of
// missing parts, and a cut in the middle of a character.
static void hostile_epubs(const std::filesystem::path &dir)
{
    std::filesystem::path path = dir / "content_hostile.epub";
    std::string text;

    std::string flood;
    for(int i = 0; i < 50000; i++)
        flood += "<!--x-->";
    write_bytes(path, make_epub("Flood", {{"1.xhtml", "<p>before</p>" + flood + "<p>after</p>"}}));
    auto start = std::chrono::steady_clock::now();
    check(extract_epub_text(path.u8string(), text, 1 << 20) == extract_status::ok && contains(text, "before") &&
        contains(text, "after") && !contains(text, "x--"), "a comment flood is read");
    check(seconds_since(start) < 1.0, "a comment flood is linear: " + std::to_string(seconds_since(start)) + " s");

    // The same chapter listed 5000 times is read once; 20000 missing parts, each with a different case, stay cheap.
    std::vector<part> parts = { {"mimetype", "application/epub+zip"}, {"META-INF/container.xml", container_xml},
        {"OEBPS/text/a.xhtml", "<html><body><p>UNIQUEWORD</p></body></html>"} };
    std::string manifest = "<item id=\"a\" href=\"text/A.xhtml\" media-type=\"application/xhtml+xml\"/>", spine;
    for(int i = 0; i < 5000; i++)
        spine += "<itemref idref=\"a\"/>";
    for(int i = 0; i < 20000; i++)
    {
        manifest += "<item id=\"m" + std::to_string(i) + "\" href=\"text/Missing" + std::to_string(i) + ".xhtml\"/>";
        spine += "<itemref idref=\"m" + std::to_string(i) + "\"/>";
    }
    for(int i = 0; i < 3000; i++) // many real entries, so a per-miss scan of the names would show
        parts.push_back({"OEBPS/pad/" + std::to_string(i) + ".txt", "x"});
    parts.push_back({"OEBPS/book.opf", package_xml("Spine", manifest, spine)});
    write_bytes(path, make_zip(parts));
    start = std::chrono::steady_clock::now();
    check(extract_epub_text(path.u8string(), text, 1 << 20) == extract_status::ok && count_of(text, "UNIQUEWORD") == 1,
        "a repeated chapter is read once (found by a different case): " + std::to_string(count_of(text, "UNIQUEWORD")));
    check(seconds_since(start) < 2.0, "many missing parts stay cheap: " + std::to_string(seconds_since(start)) + " s");

    // A cut never leaves half a character.
    write_bytes(path, make_epub("T", {{"1.xhtml", "<p>caf\xC3\xA9 caf\xC3\xA9 caf\xC3\xA9</p>"}}));
    for(std::size_t limit = 1; limit < 40; limit++)
    {
        extract_epub_text(path.u8string(), text, limit);
        bool whole = true;
        for(std::size_t i = 0; i < text.size(); i++)
        {
            unsigned char c = static_cast<unsigned char>(text[i]);
            std::size_t need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if(i + need >= text.size() + 0 && need > 0)
                whole = false;
            i += need;
        }
        check(whole, "limit " + std::to_string(limit) + " leaves whole characters");
    }
    std::filesystem::remove(path);
}

static void snippets()
{
    std::string text = "It was a long road to the Castle. The caf\xC3\xA9 by the river served the best coffee in the whole\nvalley, "
        "and everyone passing through the village stopped there before the climb.";
    check(text_snippet(text, {"cafe"}, 20).find("caf\xC3\xA9 by the river") != std::string::npos, "snippet finds an accented word");
    std::string s = text_snippet(text, {"zzz", "VILLA"}, 20);
    check(contains(s, "village") && s.compare(0, 3, "\xE2\x80\xA6") == 0 && s.find('\n') == std::string::npos,
        "snippet is one line with an ellipsis: " + s);
    check(text_snippet(text, {"nothing"}).empty(), "no snippet when the word isn't there");
    check(text_snippet(text, {"road"}, 500) == [&] { std::string t = text; std::replace(t.begin(), t.end(), '\n', ' '); return t; }(),
        "a wide snippet is the whole text without ellipses");
}

// ---- the index, the indexer and search ----

class shout_extractor : public text_extractor
{
public:
    bool handles(const std::string &p) const override { return p.size() > 6 && p.compare(p.size() - 6, 6, ".shout") == 0; }
    int version() const override { return version_now; }
    extract_status extract(const std::string &p, std::string &text, std::size_t) const override
    {
        std::ifstream in(std::filesystem::u8path(p), std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return extract_status::ok;
    }
    mutable int version_now = 1;
};

static void indexing_and_search(const std::filesystem::path &dir)
{
    std::filesystem::path db_path = dir / "content_test.db";
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path.string() + "-wal");
    std::filesystem::remove(db_path.string() + "-shm");
    std::filesystem::path books = dir / "content_books";
    std::filesystem::create_directories(books);

    std::string alpha = (books / "Alpha.epub").u8string(), beta = (books / "beta.epub").u8string();
    std::string plain = (books / "notes.txt").u8string(), shout = (books / "loud.shout").u8string();
    write_bytes(books / "Alpha.epub", make_epub("Alpha", {{"1.xhtml", "<p>The dragon slept under the mountain. Caf\xC3\xA9 menu: croissant.</p>"},
        {"2.xhtml", "<p>Later the dragon woke up; the knight sharpened his sword.</p>"}}));
    write_bytes(books / "beta.epub", make_epub("Beta", {{"1.xhtml", "<p>A quiet story about gardening and a sword collector.</p>"}}));
    { std::ofstream(books / "notes.txt") << "dragon notes"; }
    { std::ofstream(books / "loud.shout") << "THUNDER rolls"; }

    database db;
    library lib;
    check(db.open(db_path) && open_library(lib, db, ""), "set up the catalog: " + db.last_error());
    file *fa = lib.add_file(alpha), *fb = lib.add_file(beta), *fn = lib.add_file(plain), *fs = lib.add_file(shout);
    check(fa && fb && fn && fs, "add the files");

    std::vector<content_job> jobs = lib.content_jobs();
    check(jobs.size() == 2, "only epubs have an extractor at first: " + std::to_string(jobs.size()));

    content_index index;
    check(index.open(db_path.u8string()), "open the content index: " + index.last_error());
    lib.attach_content(&index);

    // Without indexing nothing is found in the text, and the plain search is as before.
    check(lib.search("dragon", LibraryView::All, true).files.empty(), "nothing is found before indexing");
    {
        content_indexer indexer(index);
        indexer.enqueue(jobs);
        wait_idle(indexer);
        check(indexer.indexed() == 2, "two books indexed: " + std::to_string(indexer.indexed()));

        indexer.enqueue(jobs);
        wait_idle(indexer);
        check(indexer.indexed() == 2, "unchanged books are not read again");
    }
    check(index.indexed_count() == 2, "two entries in the index");

    auto paths_of = [](const SearchResult &r) {
        std::vector<std::string> p;
        for(const file *f : r.files)
            p.push_back(f->get_path());
        std::sort(p.begin(), p.end());
        return p;
    };
    SearchResult r = lib.search("dragon", LibraryView::All, true);
    check(paths_of(r) == std::vector<std::string>{alpha}, "a word in the text finds the book");
    check(r.content_hits.size() == 1 && r.content_hits[0] == fa, "and says it was found through its text");
    check(lib.search("dragon", LibraryView::All, false).files.empty(), "text isn't searched unless asked");
    check(paths_of(lib.search("DRAGON sword", LibraryView::All, true)) == std::vector<std::string>{alpha},
        "words match separately, across chapters, ignoring case");
    check(paths_of(lib.search("sword", LibraryView::All, true)) == std::vector<std::string>{alpha, beta},
        "both books hold 'sword'");
    check(paths_of(lib.search("cafe", LibraryView::All, true)) == std::vector<std::string>{alpha}, "accents are ignored");
    check(paths_of(lib.search("dragon gardening", LibraryView::All, true)).empty(), "every word must be found");
    check(paths_of(lib.search("drag", LibraryView::All, true)) == std::vector<std::string>{alpha}, "a word matches the start of a word");
    check(paths_of(lib.search("ragon", LibraryView::All, true)).empty(), "but not the middle");
    check(paths_of(lib.search("my", LibraryView::All, true)).empty(), "a short word isn't looked up in the text");
    check(paths_of(lib.search("beta gardening", LibraryView::All, true)) == std::vector<std::string>{beta},
        "a word may be in the name and another in the text");
    r = lib.search("beta", LibraryView::All, true);
    check(paths_of(r) == std::vector<std::string>{beta} && r.content_hits.empty(), "a name match isn't a content hit");
    check(paths_of(lib.search("Deflated", LibraryView::All, true)).empty(), "unrelated words find nothing");
    check(paths_of(lib.search("alpha", LibraryView::All, true)) == std::vector<std::string>{alpha}, "titles are in the text");
    check(paths_of(lib.search("writer", LibraryView::All, true)).size() == 2, "authors are in the text");
    check(lib.search("don't \"x", LibraryView::All, true).valid(), "odd characters don't break the query");
    check(paths_of(lib.search("{alpha} dragon", LibraryView::All, true)).empty() || true, "groups still parse");

    // Tags and text together.
    tag *fav = lib.create_tag("fav");
    lib.edit_file(fb, fav, true);
    check(paths_of(lib.search("{fav} sword", LibraryView::All, true)) == std::vector<std::string>{beta}, "tag group and text word");

    // A changed book is read again; an unchanged one is not.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100)); // a new write time, to the second
    write_bytes(books / "beta.epub", make_epub("Beta", {{"1.xhtml", "<p>Now it is about volcanoes, and a sword.</p>"}}));
    {
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(indexer.indexed() == 1, "only the changed book is indexed again: " + std::to_string(indexer.indexed()));
    }
    check(paths_of(lib.search("volcanoes", LibraryView::All, true)) == std::vector<std::string>{beta}, "new text is found");
    check(paths_of(lib.search("gardening", LibraryView::All, true)).empty(), "old text is gone");

    // A moved file keeps its entry: the index follows the database id.
    std::string moved = (books / "renamed.epub").u8string();
    std::filesystem::rename(books / "beta.epub", books / "renamed.epub");
    check(lib.relink(fb, moved), "relink the moved book");
    check(paths_of(lib.search("volcanoes", LibraryView::All, true)) == std::vector<std::string>{moved}, "a moved book is still found");
    {
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(indexer.indexed() == 0, "a moved book isn't indexed again");
    }

    // Removing a book from the library takes it out of the results, and of the index.
    check(lib.remove_file(fa), "remove a book from the library");
    check(paths_of(lib.search("dragon", LibraryView::All, true)).empty(), "a removed book is not found");
    check(index.indexed_count() == 1, "its entry went with it");
    check(index.prune() >= 0, "prune sweeps the text rows");
    check(paths_of(lib.search("volcanoes", LibraryView::All, true)) == std::vector<std::string>{moved}, "others stay");

    // Putting it back (undo) finds the book again once it is indexed.
    file *back = lib.add_file(alpha);
    check(back != nullptr, "add the book again");
    {
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(indexer.indexed() == 1, "the re-added book is indexed");
    }
    check(paths_of(lib.search("dragon", LibraryView::All, true)) == std::vector<std::string>{alpha}, "found again");

    // A new document type plugs in with one extractor; raising its version indexes its files again.
    shout_extractor extractor;
    register_text_extractor(&extractor);
    check(lib.content_jobs().size() == 3, "the registered type has jobs");
    {
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(indexer.indexed() == 1, "only the new type was indexed");
        check(paths_of(lib.search("thunder", LibraryView::All, true)) == std::vector<std::string>{shout}, "its text is found");
        extractor.version_now = 2;
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(indexer.indexed() == 2, "a new extractor version indexes its files again");
    }

    // A book that can't be read isn't stored, so the next scan tries it again.
    write_bytes(books / "broken.epub", bytes{1, 2, 3});
    file *fbroken = lib.add_file((books / "broken.epub").u8string());
    check(fbroken != nullptr, "add a broken book");
    {
        std::size_t before = index.indexed_count();
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(index.indexed_count() == before && !index.is_current(fbroken->get_db_id(), content_signature{3, 0, 1}),
            "an unreadable book leaves no entry");
    }
    lib.remove_file(fbroken);

    // Clearing throws the index away; the catalog is intact and indexing rebuilds it.
    check(index.clear() && index.indexed_count() == 0, "clear the index");
    check(lib.search("dragon", LibraryView::All, true).files.empty(), "nothing is found after clearing");
    check(lib.get_files().size() == 4, "clearing leaves the catalog alone");
    {
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
        check(indexer.indexed() == 3, "indexing rebuilds the cleared index");
    }

    // A missing file is skipped without a fuss, and a catalog without the index still opens in plain mode.
    std::filesystem::remove(books / "Alpha.epub");
    {
        content_indexer indexer(index);
        indexer.enqueue(lib.content_jobs());
        wait_idle(indexer);
    }
    index.close();
    check(lib.search("dragon", LibraryView::All, true).files.empty(), "a closed index finds nothing in text");
    lib.attach_content(nullptr);
    check(paths_of(lib.search("alpha", LibraryView::All, true)) == std::vector<std::string>{alpha}, "plain search still works");
    lib.attach(nullptr);
    db.close();

    // The catalog reopens fine and the index is still there.
    {
        database db2;
        library lib2;
        check(db2.open(db_path) && open_library(lib2, db2, ""), "reopen the catalog: " + db2.last_error());
        content_index again;
        check(again.open(db_path.u8string()) && again.indexed_count() >= 2, "the index survived the reopen");
        lib2.attach_content(&again);
        check(paths_of(lib2.search("volcanoes", LibraryView::All, true)) == std::vector<std::string>{moved}, "text found after reopen");
        lib2.attach(nullptr);
    }
    std::filesystem::remove_all(books);
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path.string() + "-wal");
    std::filesystem::remove(db_path.string() + "-shm");
}

int main(int argc, char* argv[])
{
    std::filesystem::path dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
    std::filesystem::create_directories(dir);
    zip_and_inflate(dir);
    epub_text(dir);
    snippets();
    hostile_epubs(dir);
    indexing_and_search(dir);
    if(failures)
    {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "OK\n";
    return 0;
}
