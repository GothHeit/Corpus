#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <set>
#include <string>
#include <vector>

#include "../include/file.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"
#include "../include/tag_export.hpp"
#include "../include/text.hpp"

namespace fs = std::filesystem;

namespace
{

/// The last level of a nested name ("ref/ysl-blood-luster" gives "ysl-blood-luster").
std::string leaf(const std::string &name)
{
    std::size_t at = name.rfind(tag_path_separator);
    return at == std::string::npos ? name : name.substr(at + 1);
}

/// Whether name sits strictly under branch (not branch itself); case ignored.
bool strictly_under(const std::string &name, const std::string &branch)
{
    const std::string n = fold_case(name), b = fold_case(branch);
    return n != b && tag_under(n, b);
}

/// A file name for the note: the tag's last level without the characters Windows refuses.
std::string file_stem(const std::string &name)
{
    std::string stem;
    for(unsigned char c : leaf(name))
        stem += (c < ' ' || std::string("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string::npos) ? '_' : static_cast<char>(c);
    while(!stem.empty() && (stem.back() == '.' || stem.back() == ' '))
        stem.pop_back();
    if(stem.empty())
        return "tag";
    // Windows device names ("CON", "nul.txt", "COM1") can't be files.
    std::string base = fold_case(stem.substr(0, stem.find('.')));
    for(const char *device : {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"})
        if(base == device)
            return "_" + stem;
    return stem;
}

std::string spaced(const std::string &level)
{
    std::string out = level;
    std::replace(out.begin(), out.end(), '-', ' ');
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

bool picture_extension(const fs::path &p)
{
    std::string ext = fold_case(p.extension().u8string());
    for(const char *known : {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".avif", ".jxl", ".tif", ".tiff"})
        if(ext == known)
            return true;
    return false;
}

/// The files of a tag in library order (the tag's own list is in the order they were tagged).
std::vector<const file*> files_of(const library &lib, const tag *t)
{
    std::set<const file*> has(t->get_files().begin(), t->get_files().end());
    std::vector<const file*> out;
    for(const file* f : lib.get_files())
        if(has.count(f))
            out.push_back(f);
    return out;
}

/// Everything the note says about the tag's files, worked out from their tags.
struct summary
{
    std::vector<std::string> source, status, rights, used_in, other;
};

summary summarize(const library &lib, const tag *t)
{
    summary s;
    std::vector<const tag*> all;
    std::set<const tag*> seen;
    for(const file* f : files_of(lib, t))
        for(const tag* x : f->get_tags())
            if(seen.insert(x).second)
                all.push_back(x);
    std::sort(all.begin(), all.end(), [](const tag *a, const tag *b) {return a->get_id() < b->get_id();});
    auto leaves = [&](const std::string &branch, bool spaces) {
        std::vector<std::string> out;
        for(const tag* x : all)
        {
            if(!strictly_under(x->get_id(), branch))
                continue;
            bool inner = false;
            for(const tag* y : all)
                inner = inner || (y != x && strictly_under(y->get_id(), x->get_id()));
            if(!inner)
                out.push_back(spaces ? spaced(leaf(x->get_id())) : leaf(x->get_id()));
        }
        return out;
    };
    s.source = leaves("source", false);
    s.status = leaves("status", false);
    s.rights = leaves("rights", false);
    s.used_in = leaves("maison/piece", true);
    for(const tag* x : all)
    {
        if(x == t || strictly_under(t->get_id(), x->get_id()))
            continue;
        bool inner = false;
        for(const tag* y : all)
            inner = inner || (y != x && strictly_under(y->get_id(), x->get_id()));
        bool listed_elsewhere = false;
        for(const char *branch : {"ref", "source", "status", "rights", "maison/piece"})
            listed_elsewhere = listed_elsewhere || strictly_under(x->get_id(), branch);
        if(!inner && !listed_elsewhere)
            s.other.push_back(x->get_id());
    }
    return s;
}

std::string joined(const std::vector<std::string> &items, const char *between)
{
    std::string out;
    for(const std::string &i : items)
        out += (out.empty() ? "" : between) + i;
    return out;
}

/// Newlines in a one-line field would break the line.
std::string one_line(std::string text)
{
    for(char &c : text)
        if(c == '\n' || c == '\r')
            c = ' ';
    return text;
}

/// A link target with the characters Markdown reads as syntax escaped.
std::string link_target(const std::string &name)
{
    std::string out;
    for(char c : name)
    {
        if(c == ' ' || c == '(' || c == ')')
            out += "%" + std::string(c == ' ' ? "20" : c == '(' ? "28" : "29");
        else
            out += c;
    }
    return out;
}

}  // namespace

std::string tag_markdown(const library &lib, const tag *t, const std::vector<std::string> &pictures)
{
    const summary s = summarize(lib, t);
    const tag_attrs &a = t->get_attrs();
    std::string md = "# " + leaf(t->get_id()) + "\n\n";

    std::vector<std::string> header;
    if(!s.source.empty())
        header.push_back("**Source:** " + joined(s.source, ", "));
    if(!a.creator.empty())
        header.push_back("**Creator:** " + one_line(a.creator));
    if(!a.year.empty())
        header.push_back("**Year:** " + one_line(a.year));
    if(!a.url.empty())
        header.push_back("**Link:** " + one_line(a.url));
    if(!s.status.empty())
        header.push_back("**Status:** " + joined(s.status, ", "));
    if(!s.rights.empty())
        header.push_back("**Rights:** " + joined(s.rights, ", "));
    if(!header.empty())
        md += joined(header, " · ") + "\n\n";

    if(!a.note.empty())
        md += a.note + "\n\n";
    if(!a.taken.empty())
        md += "**Taken:** " + a.taken + "\n\n";
    if(!a.refused.empty())
        md += "**Refused:** " + a.refused + "\n\n";
    if(!s.used_in.empty())
    {
        std::vector<std::string> links;
        for(const std::string &piece : s.used_in)
            links.push_back("[[" + piece + "]]");
        md += "**Used in:** " + joined(links, ", ") + "\n\n";
    }
    if(!s.other.empty())
        md += "**Tags:** " + joined(s.other, ", ") + "\n\n";
    if(!pictures.empty())
    {
        std::vector<std::string> images;
        for(const std::string &p : pictures)
            images.push_back("![](" + link_target(p) + ")");
        md += joined(images, " ") + "\n";
    }
    while(md.size() > 1 && md[md.size() - 1] == '\n' && md[md.size() - 2] == '\n')
        md.pop_back();
    return md;
}

tag_export_result export_tag_markdown(const library &lib, const tag *t, const fs::path &folder,
    const tag_export_options &options)
{
    return export_tag_markdown(lib, t, folder, options, t ? file_stem(t->get_id()) : std::string());
}

tag_export_result export_tag_markdown(const library &lib, const tag *t, const fs::path &folder,
    const tag_export_options &options, const std::string &stem)
{
    tag_export_result result;
    if(!t)
    {
        result.error = "no tag";
        return result;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if(ec)
    {
        result.error = "could not make " + folder.u8string() + ": " + ec.message();
        return result;
    }

    // Pictures the previous note of this tag linked to would be left behind: take away those, and only those (a file
    // of the user's with a similar name stays). The link targets are read back from the note, undoing link_target.
    {
        std::ifstream old_note(folder / fs::u8path(stem + ".md"), std::ios::binary);
        const std::string old_text((std::istreambuf_iterator<char>(old_note)), std::istreambuf_iterator<char>());
        for(std::size_t at = old_text.find("![]("); at != std::string::npos; at = old_text.find("![](", at + 1))
        {
            const std::size_t close = old_text.find(')', at + 4);
            if(close == std::string::npos)
                break;
            std::string name, raw = old_text.substr(at + 4, close - at - 4);
            for(std::size_t i = 0; i < raw.size(); ++i)
            {
                if(raw[i] == '%' && i + 2 < raw.size() && raw.compare(i + 1, 2, "20") == 0)
                    name += ' ', i += 2;
                else if(raw[i] == '%' && i + 2 < raw.size() && raw.compare(i + 1, 2, "28") == 0)
                    name += '(', i += 2;
                else if(raw[i] == '%' && i + 2 < raw.size() && raw.compare(i + 1, 2, "29") == 0)
                    name += ')', i += 2;
                else
                    name += raw[i];
            }
            // Only the shape this export writes: "<stem>-<digits>.<picture extension>", no folders.
            const fs::path target = fs::u8path(name);
            const std::size_t dot = name.rfind('.');
            if(dot == std::string::npos || name.find_first_of("/\\") != std::string::npos
                || name.compare(0, stem.size() + 1, stem + "-") != 0 || !picture_extension(target))
                continue;
            const std::string digits = name.substr(stem.size() + 1, dot - stem.size() - 1);
            if(!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos)
                fs::remove(folder / target, ec);
        }
        ec.clear();
    }

    std::vector<std::string> pictures;
    for(const file* f : files_of(lib, t))
    {
        if(pictures.size() >= options.max_pictures)
            break;
        const fs::path source = fs::u8path(f->get_path());
        const fs::path base = folder / fs::u8path(stem + "-" + std::to_string(pictures.size() + 1));
        fs::path written;
        if(options.make_thumbnail)
            written = options.make_thumbnail(f->get_path(), base);
        else if(picture_extension(source))
        {
            written = base;
            written += source.extension();
            fs::copy_file(source, written, fs::copy_options::overwrite_existing, ec);
            if(ec)
                written.clear();
        }
        if(!written.empty())
            pictures.push_back(written.filename().u8string());
        else if(options.make_thumbnail || picture_extension(source))
            result.pictures_failed++;
    }
    result.pictures = pictures.size();

    const std::string md = tag_markdown(lib, t, pictures);
    result.file = folder / fs::u8path(stem + ".md");
    fs::path temp = result.file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(md.data(), static_cast<std::streamsize>(md.size()));
        out.flush();
        if(!out)
        {
            result.error = "could not write " + temp.u8string();
            out.close();
            fs::remove(temp, ec);
            return result;
        }
    }
    fs::rename(temp, result.file, ec);
    if(ec)
    {
        result.error = "could not write " + result.file.u8string() + ": " + ec.message();
        fs::remove(temp, ec);
        return result;
    }
    result.ok = true;
    return result;
}

std::vector<tag_export_result> export_tags_under(const library &lib, const std::string &branch, const fs::path &folder,
    const tag_export_options &options)
{
    std::vector<tag_export_result> results;
    std::vector<const tag*> chosen;
    for(const tag* t : lib.get_tags())
    {
        if(!strictly_under(t->get_id(), branch))
            continue;
        bool inner = false;
        for(const tag* o : lib.get_tags())
            inner = inner || (o != t && strictly_under(o->get_id(), t->get_id()));
        if(!inner)
            chosen.push_back(t);
    }
    std::sort(chosen.begin(), chosen.end(), [](const tag *a, const tag *b) {return a->get_id() < b->get_id();});
    std::set<std::string> used;
    for(const tag* t : chosen)
    {
        std::string stem = file_stem(t->get_id());
        if(!used.insert(fold_case(stem)).second)
        {
            const std::string parent = tag_parent(t->get_id());
            std::string next = parent.empty() ? stem : stem + " (" + file_stem(parent) + ")";
            for(int n = 2; !used.insert(fold_case(next)).second; ++n)
                next = stem + "-" + std::to_string(n);
            stem = next;
        }
        results.push_back(export_tag_markdown(lib, t, folder, options, stem));
        if(!results.back().ok)
            break;
    }
    return results;
}
