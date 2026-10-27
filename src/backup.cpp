#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>

#include "../include/backup.hpp"
#include "../include/database.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"
#include "../include/tag.hpp"

namespace fs = std::filesystem;

namespace
{

// ---- A small JSON writer and reader, enough for the export file ----

void write_string(std::string &out, const std::string &s)
{
    out += '"';
    for(unsigned char c : s)
    {
        switch(c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if(c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                    out += static_cast<char>(c);
        }
    }
    out += '"';
}

struct json
{
    enum kind { null, number, string, array, object } type = null;
    double num = 0;
    std::string text;
    std::vector<json> items;                           // array
    std::vector<std::pair<std::string, json>> fields;  // object

    const json* get(const char *name) const
    {
        for(const auto &f : fields)
            if(f.first == name)
                return &f.second;
        return nullptr;
    }
};

class parser
{
    const std::string &s;
    std::size_t i = 0;
    int depth = 0;

    void space()
    {
        while(i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
            i++;
    }
    static void put_utf8(std::string &out, unsigned long cp)
    {
        if(cp < 0x80)
            out += static_cast<char>(cp);
        else if(cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if(cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned long &v)
    {
        if(i + 4 > s.size())
            return false;
        v = 0;
        for(int k = 0; k < 4; k++)
        {
            char c = s[i++];
            v <<= 4;
            if(c >= '0' && c <= '9') v |= c - '0';
            else if(c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if(c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool read_string(std::string &out)
    {
        if(i >= s.size() || s[i] != '"')
            return false;
        i++;
        while(i < s.size())
        {
            char c = s[i++];
            if(c == '"')
                return true;
            if(c != '\\')
            {
                out += c;
                continue;
            }
            if(i >= s.size())
                return false;
            char e = s[i++];
            switch(e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned long cp;
                    if(!hex4(cp))
                        return false;
                    if(cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u')
                    {
                        i += 2;
                        unsigned long low;
                        if(!hex4(low) || low < 0xDC00 || low > 0xDFFF)
                            return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    public:

    explicit parser(const std::string &text) : s(text) {}

    bool value(json &out)
    {
        space();
        if(i >= s.size() || ++depth > 32)
            return false;
        bool ok = false;
        char c = s[i];
        if(c == '{')
        {
            i++;
            out.type = json::object;
            space();
            if(i < s.size() && s[i] == '}')
            {
                i++;
                ok = true;
            }
            else while(true)
            {
                space();
                std::string key;
                json v;
                if(!read_string(key))
                    break;
                space();
                if(i >= s.size() || s[i++] != ':' || !value(v))
                    break;
                out.fields.emplace_back(std::move(key), std::move(v));
                space();
                if(i < s.size() && s[i] == ',')
                {
                    i++;
                    continue;
                }
                if(i < s.size() && s[i] == '}')
                {
                    i++;
                    ok = true;
                }
                break;
            }
        }
        else if(c == '[')
        {
            i++;
            out.type = json::array;
            space();
            if(i < s.size() && s[i] == ']')
            {
                i++;
                ok = true;
            }
            else while(true)
            {
                json v;
                if(!value(v))
                    break;
                out.items.push_back(std::move(v));
                space();
                if(i < s.size() && s[i] == ',')
                {
                    i++;
                    continue;
                }
                if(i < s.size() && s[i] == ']')
                {
                    i++;
                    ok = true;
                }
                break;
            }
        }
        else if(c == '"')
        {
            out.type = json::string;
            ok = read_string(out.text);
        }
        else if(c == '-' || (c >= '0' && c <= '9'))
        {
            const char *start = s.c_str() + i;
            char *end = nullptr;
            out.num = std::strtod(start, &end);
            out.type = json::number;
            ok = end != start;
            i += static_cast<std::size_t>(end - start);
        }
        else
        {
            for(const char *word : {"true", "false", "null"})
                if(s.compare(i, std::strlen(word), word) == 0)
                {
                    i += std::strlen(word);
                    ok = true;
                    break;
                }
        }
        depth--;
        return ok;
    }

    bool at_end()
    {
        space();
        return i == s.size();
    }
};

const char *const catalog_format = "corpus-catalog";
const int catalog_version = 1;

bool read_file(const fs::path &file, std::string &text, std::string &error)
{
    std::ifstream in(file, std::ios::binary);
    if(!in)
    {
        error = "could not open " + file.u8string();
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if(in.bad())
    {
        error = "could not read " + file.u8string();
        return false;
    }
    // A UTF-8 byte order mark from an editor.
    if(text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        text.erase(0, 3);
    return true;
}

std::string stamp(std::time_t t)
{
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// Days since 1970-01-01 of a civil date (proleptic Gregorian).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// corpus-YYYYMMDD-HHMMSS.db -> seconds since 1970 (UTC), or -1 if the name isn't one.
std::time_t parse_backup_name(const std::string &name)
{
    if(name.size() != 25 || name.compare(0, 7, "corpus-") != 0 || name[15] != '-' || name.compare(22, 3, ".db") != 0)
        return -1;
    for(std::size_t k = 7; k < 22; k++)
        if(k != 15 && !std::isdigit(static_cast<unsigned char>(name[k])))
            return -1;
    auto num = [&](std::size_t at, std::size_t n) { return std::stoi(name.substr(at, n)); };
    int mon = num(11, 2), day = num(13, 2), h = num(16, 2), mi = num(18, 2), sec = num(20, 2);
    if(mon < 1 || mon > 12 || day < 1 || day > 31 || h > 23 || mi > 59 || sec > 60)
        return -1;
    return static_cast<std::time_t>(days_from_civil(num(7, 4), mon, day) * 86400 + h * 3600 + mi * 60 + sec);
}

}  // namespace

bool export_catalog(const library &lib, const fs::path &dest, std::string &error)
{
    std::string out = "{\n  \"format\": \"";
    out += catalog_format;
    out += "\",\n  \"version\": " + std::to_string(catalog_version) + ",\n  \"tags\": [";
    bool first = true;
    for(const tag* t : lib.get_tags())
    {
        out += first ? "\n    " : ",\n    ";
        first = false;
        write_string(out, t->get_id());
    }
    out += "\n  ],\n  \"watched\": [";
    first = true;
    for(const std::string &w : lib.watched_folders())
    {
        out += first ? "\n    " : ",\n    ";
        first = false;
        write_string(out, w);
    }
    out += "\n  ],\n  \"files\": [";
    first = true;
    for(const file* f : lib.get_files())
    {
        out += first ? "\n    {" : ",\n    {";
        first = false;
        out += "\"path\": ";
        write_string(out, f->get_path());
        if(!f->get_identity().empty())
        {
            out += ", \"identity\": ";
            write_string(out, f->get_identity());
        }
        out += ", \"tags\": [";
        bool first_tag = true;
        for(const tag* t : f->get_tags())
        {
            out += first_tag ? "" : ", ";
            first_tag = false;
            write_string(out, t->get_id());
        }
        out += "]";
        if(const reading_progress *p = f->get_progress())
        {
            out += ", \"progress\": {\"position\": " + std::to_string(p->position) + ", \"length\": " + std::to_string(p->length)
                + ", \"last_read\": " + std::to_string(p->last_read);
            if(!p->location.empty())
            {
                out += ", \"location\": ";
                write_string(out, p->location);
            }
            out += "}";
        }
        out += "}";
    }
    out += "\n  ]\n}\n";

    fs::path temp = dest;
    temp += ".tmp";
    {
        std::ofstream o(temp, std::ios::binary | std::ios::trunc);
        o.write(out.data(), static_cast<std::streamsize>(out.size()));
        o.flush();
        if(!o)
        {
            error = "could not write " + temp.u8string();
            o.close();
            std::error_code ec;
            fs::remove(temp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, dest, ec);
    if(ec)
    {
        error = "could not write " + dest.u8string() + ": " + ec.message();
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

catalog_import import_catalog(library &lib, const fs::path &source)
{
    catalog_import result;
    std::string text;
    if(!read_file(source, text, result.error))
        return result;
    json root;
    parser p(text);
    if(!p.value(root) || !p.at_end() || root.type != json::object)
    {
        result.error = "the file is not a Corpus catalog export";
        return result;
    }
    const json *format = root.get("format"), *version = root.get("version"), *files = root.get("files");
    if(!format || format->type != json::string || format->text != catalog_format || !files || files->type != json::array)
    {
        result.error = "the file is not a Corpus catalog export";
        return result;
    }
    if(!version || version->type != json::number || version->num > catalog_version)
    {
        result.error = "the file was made by a newer version of Corpus";
        return result;
    }

    auto names_of = [&](const json *list, std::vector<std::string> &names)
    {
        if(!list || list->type != json::array)
            return;
        for(const json &n : list->items)
        {
            if(n.type == json::string && is_valid_tag_name(n.text))
                names.push_back(n.text);
            else
                result.skipped++;
        }
    };

    // Parse everything before touching the library, so a bad file changes nothing.
    std::vector<std::string> tag_names, watched;
    names_of(root.get("tags"), tag_names);
    if(const json *w = root.get("watched"))
        if(w->type == json::array)
            for(const json &n : w->items)
                if(n.type == json::string && !n.text.empty())
                    watched.push_back(n.text);

    std::vector<file_snapshot> entries;
    for(const json &e : files->items)
    {
        const json *path = e.type == json::object ? e.get("path") : nullptr;
        if(!path || path->type != json::string || path->text.empty())
        {
            result.skipped++;
            continue;
        }
        file_snapshot s;
        s.path = path->text;
        if(const json *id = e.get("identity"); id && id->type == json::string)
            s.identity = id->text;
        names_of(e.get("tags"), s.tags);
        if(const json *pr = e.get("progress"); pr && pr->type == json::object)
        {
            auto number = [&](const char *name, std::int64_t &into)
            {
                const json *v = pr->get(name);
                if(v && v->type == json::number)
                    into = static_cast<std::int64_t>(v->num);
            };
            number("position", s.progress.position);
            number("length", s.progress.length);
            number("last_read", s.progress.last_read);
            if(const json *loc = pr->get("location"); loc && loc->type == json::string)
                s.progress.location = loc->text;
            s.has_progress = s.progress.position >= 0 && s.progress.length >= 0;
        }
        entries.push_back(std::move(s));
    }

    database *db = lib.attached();
    if(db && !db->savepoint())
    {
        result.error = db->last_error();
        return result;
    }
    const std::size_t tags_before = lib.get_tags().size();
    bool ok = true;

    for(const std::string &name : tag_names)
        if(!(ok = lib.retrieve_tag(name) != nullptr))
            break;

    std::vector<file_snapshot> fresh;
    for(file_snapshot &s : entries)
    {
        if(!ok)
            break;
        file* f = lib.find_file(s.path);
        if(!f)
        {
            s.db_id = 0;
            fresh.push_back(std::move(s));
            continue;
        }
        const std::size_t tags_had = f->get_tags().size();
        const bool had_progress = f->get_progress() != nullptr;
        for(const std::string &name : s.tags)
        {
            tag* t = lib.retrieve_tag(name);
            if(!t || (!f->tagged(t) && !lib.edit_file(f, t, true)))
            {
                ok = false;
                break;
            }
        }
        if(ok && s.has_progress && !had_progress)
            ok = lib.set_progress(f, s.progress);
        if(ok && (f->get_tags().size() != tags_had || (!had_progress && f->get_progress())))
            result.files_updated++;
    }
    if(ok && !fresh.empty())
    {
        int put = lib.restore_files(fresh);
        ok = put >= 0;
        if(ok)
            result.files_added = put;
    }
    for(const std::string &w : watched)
        if(ok && !lib.is_watched(w))
            ok = lib.watch_folder(w);

    if(ok && db)
        ok = db->release_savepoint();
    else if(db)
        db->undo_savepoint();
    if(!ok)
    {
        result.error = db ? db->last_error() : "could not import";
        if(result.error.empty())
            result.error = "could not import";
        return result;
    }
    result.tags_added = static_cast<int>(lib.get_tags().size() - tags_before);
    result.ok = true;
    return result;
}

fs::path default_backup_folder()
{
    fs::path base;
#ifdef _WIN32
    if(const wchar_t *local = _wgetenv(L"LOCALAPPDATA"))
        base = local;
#else
    if(const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        base = xdg;
    else if(const char *home = std::getenv("HOME"); home && *home)
        base = fs::path(home) / ".local" / "share";
#endif
    return base.empty() ? base : base / "Corpus" / "backups";
}

std::vector<backup_info> list_backups(const fs::path &folder)
{
    std::vector<backup_info> out;
    std::error_code ec;
    for(fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        std::time_t when = parse_backup_name(it->path().filename().u8string());
        if(when < 0)
            continue;
        std::error_code size_error;
        if(!it->is_regular_file(size_error))
            continue;
        backup_info b;
        b.file = it->path();
        b.when = when;
        b.size = it->file_size(size_error);
        out.push_back(std::move(b));
    }
    std::sort(out.begin(), out.end(), [](const backup_info &a, const backup_info &b) { return a.when > b.when; });
    return out;
}

backup_result backup_database(database &db, const fs::path &folder, int keep, std::int64_t min_age_seconds)
{
    backup_result result;
    if(!db.is_open() || folder.empty() || keep < 1)
    {
        result.error = "no database or backup folder";
        return result;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if(ec)
    {
        result.error = "could not create " + folder.u8string() + ": " + ec.message();
        return result;
    }

    // Nothing worth keeping in an empty catalog, and it would push good backups out.
    if(!db.has_content())
    {
        result.ok = true;
        return result;
    }
    std::vector<backup_info> have = list_backups(folder);
    std::time_t now = std::time(nullptr);
    if(min_age_seconds > 0 && !have.empty() && now - have.front().when < min_age_seconds)
    {
        result.ok = true;
        return result;
    }

    // A backup in the same second (min_age 0) would take the same name: wait for the next free second.
    fs::path target;
    for(std::time_t t = now; target.empty() || fs::exists(target, ec); t++)
        target = folder / ("corpus-" + stamp(t) + ".db");
    fs::path temp = target;
    temp += ".tmp";
    if(!db.backup_to(temp))
    {
        result.error = db.last_error();
        return result;
    }
    fs::rename(temp, target, ec);
    if(ec)
    {
        result.error = "could not write " + target.u8string() + ": " + ec.message();
        fs::remove(temp, ec);
        return result;
    }
    result.made = true;
    result.file = target;

    have = list_backups(folder);
    for(std::size_t i = static_cast<std::size_t>(keep); i < have.size(); i++)
    {
        if(fs::remove(have[i].file, ec))
            result.removed++;
    }
    result.ok = true;
    return result;
}

bool restore_database(const fs::path &backup, const fs::path &db_file, std::string &error)
{
    if(!database::verify_file(backup, error))
        return false;
    std::error_code ec;
    fs::path incoming = db_file;
    incoming += ".restoring";
    fs::copy_file(backup, incoming, fs::copy_options::overwrite_existing, ec);
    if(ec)
    {
        error = "could not copy the backup (the catalog is unchanged): " + ec.message();
        std::error_code ignored;
        fs::remove(incoming, ignored);
        return false;
    }
    fs::path before = db_file;
    before += ".before-restore";
    bool had_old = fs::exists(db_file, ec);
    if(had_old)
    {
        fs::rename(db_file, before, ec);
        if(ec)
        {
            error = "could not set the current catalog aside (the catalog is unchanged): " + ec.message();
            fs::remove(incoming, ec);
            return false;
        }
    }
    fs::rename(incoming, db_file, ec);
    if(ec)
    {
        std::string why = ec.message();
        std::error_code ignored;
        if(had_old)
            fs::rename(before, db_file, ignored);
        fs::remove(incoming, ignored);
        error = "could not put the backup in place (the catalog is unchanged): " + why;
        return false;
    }
    // Only now, with the backup in place: the old file's journal must not be replayed over it.
    for(const char *suffix : {"-wal", "-shm"})
    {
        fs::path side = db_file;
        side += suffix;
        fs::remove(side, ec);
    }
    return true;
}
