// Plain text out of an EPUB: container.xml -> the package (.opf) -> the spine's XHTML files. Like the reader in the GUI,
// it scans tags by hand instead of using an XML library.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../include/text.hpp"
#include "../include/text_extract.hpp"
#include "../include/zip_reader.hpp"

namespace
{

const std::size_t max_part_bytes = 16u << 20; // one chapter, uncompressed
const std::size_t max_book_bytes = 256u << 20; // everything inflated for one book

std::string lower_ascii(std::string s)
{
    for(char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool name_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':' || c == '.';
}

/// One tag read by next_tag.
struct tag_view
{
    std::string name;       // lower case, without a namespace prefix
    bool closing = false;
    bool self_closing = false;
    std::string attributes; // the text between the name and '>'
    std::size_t end = 0;    // just after '>'
};

/// Finds the next element tag at or after pos, skipping comments, CDATA, doctypes and processing instructions.
bool next_tag(const std::string &s, std::size_t pos, tag_view &t, std::size_t &start)
{
    while(true)
    {
        std::size_t lt = s.find('<', pos);
        if(lt == std::string::npos)
            return false;
        if(s.compare(lt, 4, "<!--") == 0)
        {
            std::size_t e = s.find("-->", lt + 4);
            if(e == std::string::npos)
                return false;
            pos = e + 3;
            continue;
        }
        if(s.compare(lt, 9, "<![CDATA[") == 0)
        {
            std::size_t e = s.find("]]>", lt + 9);
            if(e == std::string::npos)
                return false;
            pos = e + 3;
            continue;
        }
        if(lt + 1 < s.size() && (s[lt + 1] == '!' || s[lt + 1] == '?'))
        {
            std::size_t e = s.find('>', lt + 2);
            if(e == std::string::npos)
                return false;
            pos = e + 1;
            continue;
        }
        std::size_t i = lt + 1;
        t = tag_view();
        if(i < s.size() && s[i] == '/')
        {
            t.closing = true;
            i++;
        }
        std::size_t name_start = i;
        while(i < s.size() && name_char(s[i]))
            i++;
        if(i == name_start)
        {
            pos = lt + 1; // a stray '<' in text
            continue;
        }
        t.name = lower_ascii(s.substr(name_start, i - name_start));
        std::size_t colon = t.name.find(':');
        if(colon != std::string::npos)
            t.name.erase(0, colon + 1);
        // Attribute values may hold '>', so honor quotes.
        std::size_t attr_start = i;
        char quote = 0;
        for(; i < s.size(); i++)
        {
            if(quote)
            {
                if(s[i] == quote)
                    quote = 0;
            }
            else if(s[i] == '"' || s[i] == '\'')
            {
                quote = s[i];
            }
            else if(s[i] == '>')
            {
                break;
            }
        }
        if(i >= s.size())
            return false;
        t.attributes = s.substr(attr_start, i - attr_start);
        t.self_closing = !t.attributes.empty() && t.attributes.back() == '/';
        t.end = i + 1;
        start = lt;
        return true;
    }
}

/// The value of an attribute in a tag's attribute text ("" if absent), entities left as they are.
std::string attribute(const std::string &attrs, const std::string &name)
{
    std::size_t pos = 0;
    while(true)
    {
        pos = attrs.find(name, pos);
        if(pos == std::string::npos)
            return "";
        bool boundary_before = pos == 0 || !name_char(attrs[pos - 1]) || attrs[pos - 1] == ':';
        std::size_t after = pos + name.size();
        while(after < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[after])))
            after++;
        if(boundary_before && after < attrs.size() && attrs[after] == '=')
        {
            after++;
            while(after < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[after])))
                after++;
            if(after < attrs.size() && (attrs[after] == '"' || attrs[after] == '\''))
            {
                std::size_t close = attrs.find(attrs[after], after + 1);
                if(close == std::string::npos)
                    return "";
                return attrs.substr(after + 1, close - after - 1);
            }
            return "";
        }
        pos += name.size();
    }
}

void append_utf8(std::string &out, std::uint32_t cp)
{
    if(cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000))
        cp = 0xFFFD;
    if(cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if(cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if(cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// The code point of a named entity; 0 if unknown. XHTML in books uses the HTML names freely, so more than the five
/// XML ones.
std::uint32_t named_entity(const std::string &name)
{
    static const std::unordered_map<std::string, std::uint32_t> table = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}, {"shy", 0xAD},
        {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"sbquo", 0x201A}, {"bdquo", 0x201E}, {"laquo", 0xAB},
        {"raquo", 0xBB}, {"middot", 0xB7}, {"bull", 0x2022}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122},
        {"deg", 0xB0}, {"para", 0xB6}, {"sect", 0xA7}, {"times", 0xD7}, {"divide", 0xF7}, {"eacute", 0xE9},
        {"egrave", 0xE8}, {"ecirc", 0xEA}, {"euml", 0xEB}, {"aacute", 0xE1}, {"agrave", 0xE0}, {"acirc", 0xE2},
        {"atilde", 0xE3}, {"auml", 0xE4}, {"aring", 0xE5}, {"aelig", 0xE6}, {"ccedil", 0xE7}, {"iacute", 0xED},
        {"igrave", 0xEC}, {"icirc", 0xEE}, {"iuml", 0xEF}, {"ntilde", 0xF1}, {"oacute", 0xF3}, {"ograve", 0xF2},
        {"ocirc", 0xF4}, {"otilde", 0xF5}, {"ouml", 0xF6}, {"oslash", 0xF8}, {"uacute", 0xFA}, {"ugrave", 0xF9},
        {"ucirc", 0xFB}, {"uuml", 0xFC}, {"szlig", 0xDF}, {"Eacute", 0xC9}, {"Aacute", 0xC1}, {"Iacute", 0xCD},
        {"Oacute", 0xD3}, {"Uacute", 0xDA}, {"Ccedil", 0xC7}, {"Ntilde", 0xD1}, {"Agrave", 0xC0}, {"Atilde", 0xC3},
        {"Otilde", 0xD5}, {"Acirc", 0xC2}, {"Ecirc", 0xCA}, {"Ocirc", 0xD4}, {"Auml", 0xC4}, {"Ouml", 0xD6},
        {"Uuml", 0xDC}, {"ensp", ' '}, {"emsp", ' '}, {"thinsp", ' '}, {"zwnj", 0x200C}, {"zwj", 0x200D},
        {"iexcl", 0xA1}, {"iquest", 0xBF}, {"cent", 0xA2}, {"pound", 0xA3}, {"euro", 0x20AC}, {"yen", 0xA5},
        {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE}, {"plusmn", 0xB1}, {"larr", 0x2190}, {"rarr", 0x2192}};
    auto it = table.find(name);
    return it != table.end() ? it->second : 0;
}

/// Decodes the entities in text and appends the result. An unknown entity is kept as written.
void append_decoded(std::string &out, const char *p, std::size_t n)
{
    for(std::size_t i = 0; i < n; i++)
    {
        if(p[i] != '&')
        {
            out.push_back(p[i]);
            continue;
        }
        std::size_t semi = i + 1;
        while(semi < n && semi - i <= 10 && p[semi] != ';' && p[semi] != '&' && !std::isspace(static_cast<unsigned char>(p[semi])))
            semi++;
        if(semi >= n || p[semi] != ';' || semi == i + 1)
        {
            out.push_back('&');
            continue;
        }
        std::string name(p + i + 1, semi - i - 1);
        std::uint32_t cp = 0;
        if(name[0] == '#')
        {
            bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            std::size_t digits = hex ? 2 : 1;
            if(digits < name.size() && name.size() - digits <= 8)
            {
                cp = 0;
                bool valid = true;
                for(std::size_t k = digits; k < name.size() && valid; k++)
                {
                    int v = std::isdigit(static_cast<unsigned char>(name[k])) ? name[k] - '0' :
                        (hex && std::isxdigit(static_cast<unsigned char>(name[k]))) ? (std::tolower(name[k]) - 'a' + 10) : -1;
                    if(v < 0)
                        valid = false;
                    else
                        cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(v);
                }
                if(!valid)
                    cp = 0;
            }
        }
        else
        {
            cp = named_entity(name);
        }
        if(cp == 0)
        {
            out.push_back('&');
            continue;
        }
        append_utf8(out, cp);
        i = semi;
    }
}

bool block_element(const std::string &name)
{
    static const char *const blocks[] = { "p", "div", "br", "li", "ul", "ol", "tr", "td", "th", "table", "h1", "h2", "h3",
        "h4", "h5", "h6", "section", "article", "blockquote", "pre", "hr", "dt", "dd", "figure", "figcaption", "header",
        "footer", "aside", "nav", "body", "title" };
    for(const char *b : blocks)
    {
        if(name == b)
            return true;
    }
    return false;
}

/// Drops comments, doctypes and processing instructions, and turns CDATA into escaped text, so what is left is only
/// tags and text.
std::string strip_markup(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while(pos < s.size())
    {
        // One scan over the '<' characters: each is looked at once, so a flood of comments stays linear.
        std::size_t lt = s.find('<', pos);
        while(lt != std::string::npos && !(lt + 1 < s.size() && (s[lt + 1] == '!' || s[lt + 1] == '?')))
            lt = s.find('<', lt + 1);
        if(lt == std::string::npos)
        {
            out.append(s, pos, std::string::npos);
            break;
        }
        out.append(s, pos, lt - pos);
        if(s.compare(lt, 9, "<![CDATA[") == 0)
        {
            std::size_t e = s.find("]]>", lt + 9);
            std::size_t stop = e == std::string::npos ? s.size() : e;
            for(std::size_t i = lt + 9; i < stop; i++)
            {
                if(s[i] == '&') out += "&amp;";
                else if(s[i] == '<') out += "&lt;";
                else out.push_back(s[i]);
            }
            pos = e == std::string::npos ? s.size() : e + 3;
        }
        else if(s.compare(lt, 4, "<!--") == 0)
        {
            std::size_t e = s.find("-->", lt + 4);
            pos = e == std::string::npos ? s.size() : e + 3;
        }
        else
        {
            std::size_t e = s.find('>', lt + 2);
            pos = e == std::string::npos ? s.size() : e + 1;
        }
    }
    return out;
}

/// Appends the text of one XHTML document to out, stopping at max_bytes in total.
void html_to_text(const std::string &raw, std::string &out, std::size_t max_bytes)
{
    const std::string html = strip_markup(raw);
    std::size_t pos = 0;
    std::size_t skip_depth = 0; // inside script, style or head
    std::string skip_name;
    // A byte order mark would sit in front of the first word.
    if(html.compare(0, 3, "\xEF\xBB\xBF") == 0)
        pos = 3;
    while(pos < html.size() && out.size() < max_bytes)
    {
        tag_view t;
        std::size_t start = 0;
        bool found = next_tag(html, pos, t, start);
        std::size_t text_end = found ? start : html.size();
        if(!skip_depth && text_end > pos)
            append_decoded(out, html.data() + pos, text_end - pos);
        if(!found)
            break;
        pos = t.end;
        if(skip_depth)
        {
            if(t.name == skip_name && !t.self_closing)
            {
                if(t.closing)
                    skip_depth--;
                else
                    skip_depth++;
            }
            continue;
        }
        if(!t.closing && !t.self_closing && (t.name == "script" || t.name == "style" || t.name == "head"))
        {
            skip_name = t.name;
            skip_depth = 1;
            continue;
        }
        if(block_element(t.name) && !out.empty() && out.back() != '\n')
            out.push_back('\n');
        // Inline tags join the words around them: "<b>Th</b>e" is one word.
    }
    if(out.size() > max_bytes)
    {
        out.resize(max_bytes);
        // Not in the middle of a UTF-8 character.
        while(!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if(!out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0)
            out.pop_back();
    }
    if(!out.empty() && out.back() != '\n')
        out.push_back('\n');
}

/// Percent-decodes an href and resolves it against the folder of the file that holds it ("../x" and "./x" too).
std::string resolve(const std::string &base_folder, const std::string &href)
{
    std::string decoded;
    for(std::size_t i = 0; i < href.size(); i++)
    {
        if(href[i] == '%' && i + 2 < href.size() + 0 && std::isxdigit(static_cast<unsigned char>(href[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(href[i + 2])))
        {
            decoded.push_back(static_cast<char>(std::stoi(href.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else if(href[i] == '#')
        {
            break;
        }
        else
        {
            decoded.push_back(href[i]);
        }
    }
    std::string joined = decoded.empty() || decoded[0] == '/' ? decoded.substr(decoded.empty() ? 0 : 1) : base_folder + decoded;
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while(pos <= joined.size())
    {
        std::size_t slash = joined.find('/', pos);
        std::string part = joined.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if(part == "..")
        {
            if(!parts.empty())
                parts.pop_back();
        }
        else if(!part.empty() && part != ".")
        {
            parts.push_back(part);
        }
        if(slash == std::string::npos)
            break;
        pos = slash + 1;
    }
    std::string out;
    for(const std::string &p : parts)
        out += (out.empty() ? "" : "/") + p;
    return out;
}

/// The text between <name> and </name> (first occurrence, any prefix), entities decoded.
std::string element_text(const std::string &xml, const std::string &name)
{
    std::size_t pos = 0;
    tag_view t;
    std::size_t start = 0;
    while(next_tag(xml, pos, t, start))
    {
        pos = t.end;
        if(t.name == name && !t.closing && !t.self_closing)
        {
            std::size_t close = xml.find("</", t.end);
            std::string out;
            if(close != std::string::npos)
                append_decoded(out, xml.data() + t.end, close - t.end);
            return out;
        }
    }
    return "";
}

/// Reads parts of one archive, with the limits that keep a hostile book cheap: a part is read once, and the bytes
/// inflated for the whole book are capped.
struct part_reader
{
    const zip_archive &zip;
    std::unordered_map<std::string, const zip_archive::entry*> by_lower_name; // built on the first miss
    bool lower_built = false;
    std::unordered_set<std::string> done;
    std::size_t inflated = 0;

    explicit part_reader(const zip_archive &z) : zip(z) {}

    /// 1: read into out. 0: not there, or damaged. -1: already read, or the book's byte budget is spent.
    int read(const std::string &name, std::string &out)
    {
        const zip_archive::entry *e = zip.find(name);
        if(!e)
        {
            // Some tools write different case than the manifest uses.
            if(!lower_built)
            {
                lower_built = true;
                for(const zip_archive::entry &candidate : zip.entries())
                    by_lower_name.emplace(lower_ascii(candidate.name), &candidate);
            }
            auto it = by_lower_name.find(lower_ascii(name));
            if(it != by_lower_name.end())
                e = it->second;
        }
        if(!e)
            return 0;
        if(!done.insert(e->name).second || inflated >= max_book_bytes)
            return -1;
        if(!zip.read(*e, out, max_part_bytes))
            return 0;
        inflated += out.size();
        return 1;
    }
};

class epub_extractor : public text_extractor
{
public:
    bool handles(const std::string &path) const override
    {
        return path.size() > 5 && lower_ascii(path.substr(path.size() - 5)) == ".epub";
    }
    int version() const override { return 1; }
    extract_status extract(const std::string &path, std::string &text, std::size_t max_bytes) const override
    {
        return extract_epub_text(path, text, max_bytes);
    }
};

} // namespace

extract_status extract_epub_text(const std::string &utf8_path, std::string &text, std::size_t max_bytes)
{
    text.clear();
    zip_archive zip;
    if(!zip.open(utf8_path))
        return extract_status::failed;

    part_reader reader(zip);
    std::string container;
    if(reader.read("META-INF/container.xml", container) != 1)
        return extract_status::failed;
    std::string opf_path;
    tag_view t;
    std::size_t start = 0, pos = 0;
    while(next_tag(container, pos, t, start))
    {
        pos = t.end;
        if(t.name == "rootfile" && !t.closing)
        {
            opf_path = resolve("", attribute(t.attributes, "full-path"));
            break;
        }
    }
    std::string opf;
    if(opf_path.empty() || reader.read(opf_path, opf) != 1)
        return extract_status::failed;
    std::string folder = opf_path.find('/') == std::string::npos ? "" : opf_path.substr(0, opf_path.rfind('/') + 1);

    // Metadata first, so a search for a title or an author finds the book too.
    std::string title = element_text(opf, "title"), author = element_text(opf, "creator");
    if(!title.empty())
        text += title + "\n";
    if(!author.empty())
        text += author + "\n";

    std::unordered_map<std::string, std::string> manifest; // id -> path inside the archive
    std::vector<std::string> spine;
    pos = 0;
    while(next_tag(opf, pos, t, start))
    {
        pos = t.end;
        if(t.closing)
            continue;
        if(t.name == "item")
            manifest[attribute(t.attributes, "id")] = resolve(folder, attribute(t.attributes, "href"));
        else if(t.name == "itemref")
            spine.push_back(attribute(t.attributes, "idref"));
    }

    std::string part;
    for(const std::string &id : spine)
    {
        if(text.size() >= max_bytes)
            break;
        auto it = manifest.find(id);
        if(it == manifest.end() || reader.read(it->second, part) != 1)
            continue; // a missing or repeated chapter doesn't hide the rest
        html_to_text(part, text, max_bytes);
    }
    return extract_status::ok;
}

std::string text_snippet(const std::string &text, const std::vector<std::string> &words, std::size_t radius)
{
    // Each word is cut at its first letter-or-digit run, as the index's tokenizer would.
    std::vector<std::string> wanted;
    for(const std::string &w : words)
    {
        std::string token;
        for(char c : fold_search(w))
        {
            if(std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80)
                token.push_back(c);
            else if(!token.empty())
                break;
        }
        if(!token.empty())
            wanted.push_back(token);
    }
    if(wanted.empty())
        return "";
    std::size_t i = 0;
    while(i < text.size())
    {
        auto is_word = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80;
        };
        if(!is_word(text[i]))
        {
            i++;
            continue;
        }
        std::size_t end = i;
        while(end < text.size() && is_word(text[end]))
            end++;
        std::string folded = fold_search(text.substr(i, end - i));
        for(const std::string &w : wanted)
        {
            if(folded.compare(0, w.size(), w) == 0)
            {
                std::size_t from = i > radius ? i - radius : 0;
                std::size_t to = std::min(text.size(), end + radius);
                // Do not cut a UTF-8 character in half.
                while(from > 0 && (static_cast<unsigned char>(text[from]) & 0xC0) == 0x80)
                    from--;
                while(to < text.size() && (static_cast<unsigned char>(text[to]) & 0xC0) == 0x80)
                    to++;
                std::string line;
                bool space = true;
                for(std::size_t k = from; k < to; k++) // one line: whitespace runs become one space
                {
                    if(std::isspace(static_cast<unsigned char>(text[k])))
                    {
                        if(!space)
                            line.push_back(' ');
                        space = true;
                    }
                    else
                    {
                        line.push_back(text[k]);
                        space = false;
                    }
                }
                while(!line.empty() && line.back() == ' ')
                    line.pop_back();
                return (from > 0 ? "\xE2\x80\xA6" : "") + line + (to < text.size() ? "\xE2\x80\xA6" : "");
            }
        }
        i = end;
    }
    return "";
}

namespace
{
const epub_extractor builtin_epub;
std::vector<const text_extractor*>& extractor_list()
{
    static std::vector<const text_extractor*> list = { &builtin_epub };
    return list;
}
}

const text_extractor* find_text_extractor(const std::string &utf8_path)
{
    const std::vector<const text_extractor*> &list = extractor_list();
    for(auto it = list.rbegin(); it != list.rend(); ++it)
    {
        if((*it)->handles(utf8_path))
            return *it;
    }
    return nullptr;
}

void register_text_extractor(const text_extractor *extractor)
{
    if(extractor)
        extractor_list().push_back(extractor);
}
