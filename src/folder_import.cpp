#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <cwchar>
#include <windows.h>
#endif

#include "../include/file_identity.hpp"
#include "../include/folder_import.hpp"
#include "../include/tag.hpp"
#include "../include/text.hpp"
#include "long_path.hpp"

enum class entry_kind { file, folder, skipped, other };

struct folder_entry
{
    std::filesystem::path path;
    entry_kind kind;
};

/// True for files Windows or macOS keeps in folders for itself (folder settings, thumbnail caches). Case-insensitive.
static bool system_clutter(const std::filesystem::path &name)
{
    std::string n = name.u8string();
    std::transform(n.begin(), n.end(), n.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
    return n == "desktop.ini" || n == "thumbs.db" || n == ".ds_store";
}

/// Lists a folder without entering subfolders; links to folders count as other and aren't followed. On Windows the
/// folder is read directly, since MinGW's std::filesystem can't list past MAX_PATH or tell a junction or folder
/// symlink from a folder. Other reparse points (OneDrive placeholders) are real folders. Hidden-and-system entries
/// ($RECYCLE.BIN, System Volume Information) and system_clutter names are skipped. False if the folder can't be
/// listed (ec says why); out then holds what was read.
static bool list_folder(const std::filesystem::path &dir, std::vector<folder_entry> &out, std::error_code &ec)
{
#ifdef _WIN32
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileExW(long_path(dir / L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
        nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if(h == INVALID_HANDLE_VALUE)
    {
        // Nothing found at all: an empty drive, whose root has no "." and ".." entries.
        DWORD error = GetLastError();
        if(error == ERROR_FILE_NOT_FOUND)
        {
            return true;
        }
        ec.assign(static_cast<int>(error), std::system_category());
        return false;
    }
    do
    {
        if(std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0)
        {
            continue;
        }
        // Folder links come first: the old profile junctions ("Application Data") are hidden and system too.
        const DWORD hidden_system = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
        bool system = (data.dwFileAttributes & hidden_system) == hidden_system;
        entry_kind kind = system || system_clutter(data.cFileName) ? entry_kind::skipped : entry_kind::file;
        if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            bool link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                && (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || data.dwReserved0 == IO_REPARSE_TAG_SYMLINK);
            kind = link ? entry_kind::other : system ? entry_kind::skipped : entry_kind::folder;
        }
        out.push_back({dir / data.cFileName, kind});
    } while(FindNextFileW(h, &data));
    DWORD error = GetLastError();
    FindClose(h);
    if(error != ERROR_NO_MORE_FILES)
    {
        ec.assign(static_cast<int>(error), std::system_category());
        return false;
    }
    return true;
#else
    namespace fs = std::filesystem;
    fs::directory_iterator it(dir, ec);
    for(; !ec && it != fs::directory_iterator(); it.increment(ec))
    {
        std::error_code type_error;
        const fs::directory_entry &entry = *it;
        entry_kind kind = entry_kind::other;
        if(entry.is_directory(type_error))
        {
            kind = entry.is_symlink(type_error) ? entry_kind::other : entry_kind::folder;
        }
        else if(entry.is_regular_file(type_error))
        {
            kind = system_clutter(entry.path().filename()) ? entry_kind::skipped : entry_kind::file;
        }
        out.push_back({entry.path(), kind});
    }
    return !ec;
#endif
}

static void sort_files(folder_scan &scan)
{
    std::sort(scan.files.begin(), scan.files.end(),
        [](const scanned_file &a, const scanned_file &b) {return a.path < b.path;});
}

folder_scan scan_folder(const std::string &folder, const std::atomic<bool> *cancel, const scan_progress &progress,
    const std::unordered_set<std::string> *known)
{
    namespace fs = std::filesystem;
    folder_scan out;
    std::error_code ec;
    fs::path root = fs::absolute(fs::u8path(folder), ec);
    if(ec)
    {
        out.error = ec;
        return out;
    }
    root = root.lexically_normal();

    // Folders by identity: a link the file system follows anyway (a junction on Windows) leads to one seen before.
    std::unordered_set<std::string> seen;
    fat_cache volumes;
    std::vector<fs::path> pending{root};
    std::vector<folder_entry> entries;
    while(!pending.empty())
    {
        fs::path dir = std::move(pending.back());
        pending.pop_back();
        std::string id = file_identity(dir.u8string(), &volumes);
        if(!id.empty() && !seen.insert(id).second)
        {
            continue;
        }

        entries.clear();
        if(!list_folder(dir, entries, ec))
        {
            if(dir == root)
            {
                out.error = ec;
            }
            else
            {
                out.unreadable++;
            }
            ec.clear();
        }
        for(const folder_entry &entry : entries)
        {
            if(cancel && cancel->load(std::memory_order_relaxed))
            {
                out.cancelled = true;
                sort_files(out);
                return out;
            }
            if(entry.kind == entry_kind::folder)
            {
                pending.push_back(entry.path);
            }
            else if(entry.kind == entry_kind::skipped)
            {
                out.skipped++;
            }
            else if(entry.kind == entry_kind::file)
            {
                std::string path = entry.path.u8string();
                if(known && known->count(path))
                {
                    continue;
                }
                out.files.push_back({path, file_identity(path, &volumes)});
                if(progress)
                {
                    progress(out.files.size(), path);
                }
            }
        }
    }
    sort_files(out);
    return out;
}

bool temporary_file(const std::string &path)
{
    std::string name = std::filesystem::u8path(path).filename().u8string();
    std::transform(name.begin(), name.end(), name.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
    auto ends = [&name](const char *end)
    {
        std::string e(end);
        return name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0;
    };
    return name.rfind("~$", 0) == 0 || name.rfind(".~lock.", 0) == 0 || ends(".crdownload") || ends(".part") ||
        ends(".partial") || ends(".download") || ends(".tmp") || ends(".temp");
}

std::vector<std::string> folder_tags(const std::string &root, const std::string &file_path, std::size_t max_names)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::absolute(fs::u8path(root), ec).lexically_normal();
    fs::path rel = fs::u8path(file_path).lexically_normal().lexically_relative(base);
    if(ec || rel.empty() || *rel.begin() == "..")
        return {};
    std::vector<std::string> names;
    // The root's own name; empty for a drive root.
    fs::path last = base.has_filename() ? base.filename() : base.parent_path().filename();
    names.push_back(last.u8string());
    for(auto it = rel.begin(), end = rel.end(); it != end; ++it)
    {
        auto next = it;
        if(++next == end)
            break; // the file's own name
        names.push_back(it->u8string());
    }
    std::vector<std::string> out;
    std::set<std::string> seen;
    for(const std::string &n : names)
    {
        std::string name = n == "." || n == ".." ? std::string() : normalize_tag_name(n);
        if(!name.empty() && seen.insert(fold_case(name)).second)
            out.push_back(name);
    }
    if(max_names < 1)
        max_names = 1;
    if(out.size() > max_names)
        out.erase(out.begin() + 1, out.end() - (max_names - 1));
    return out;
}

std::string folder_tag_path(const std::string &root, const std::string &file_path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path base = fs::absolute(fs::u8path(root), ec).lexically_normal();
    fs::path rel = fs::u8path(file_path).lexically_normal().lexically_relative(base);
    if(ec || rel.empty() || *rel.begin() == "..")
        return {};
    std::string out;
    auto append = [&out](const fs::path &part)
    {
        std::string n = part.u8string();
        std::string name = n == "." || n == ".." ? std::string() : normalize_tag_name(n);
        // Not a separator inside one folder's name ("AC/DC" can't be a folder, but a stray slash mustn't nest).
        std::replace(name.begin(), name.end(), tag_path_separator, '_');
        if(name.empty())
            return;
        if(!out.empty())
            out += tag_path_separator;
        out += name;
    };
    // The root's own name; empty for a drive root.
    append(base.has_filename() ? base.filename() : base.parent_path().filename());
    for(auto it = rel.begin(), end = rel.end(); it != end; ++it)
    {
        auto next = it;
        if(++next == end)
            break; // the file's own name
        append(*it);
    }
    return out;
}

bool generic_folder_name(const std::string &name)
{
    auto trim = [](char c) { return c == '~' || c == '-' || c == '_' || c == '.' || c == ' '; };
    std::size_t first = 0, last = name.size();
    while(first < last && trim(name[first]))
        first++;
    while(last > first && trim(name[last - 1]))
        last--;
    std::string n = name.substr(first, last - first);
    std::transform(n.begin(), n.end(), n.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
    static const char *const extras[] = { "scan", "scans", "artwork", "artworks", "cover", "covers", "booklet",
        "bonus", "extra", "extras", "misc", "video", "videos" };
    for(const char *e : extras)
    {
        if(n == e)
            return true;
    }
    // A disc word, then nothing, or a number of one or two digits or a single letter, maybe after a separator.
    static const char *const discs[] = { "cd", "disc", "disk", "side", "vinyl", "cassette" };
    for(const char *d : discs)
    {
        std::string word(d);
        if(n.compare(0, word.size(), word) != 0)
            continue;
        std::size_t at = word.size();
        if(at < n.size() && (n[at] == ' ' || n[at] == '_' || n[at] == '-' || n[at] == '.'))
            at++;
        std::string rest = n.substr(at);
        bool digits = !rest.empty() && rest.size() <= 2 &&
            std::all_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; });
        bool letter = rest.size() == 1 && rest[0] >= 'a' && rest[0] <= 'z';
        if(rest.empty() || digits || letter)
            return true;
    }
    return false;
}

std::vector<folder_tag_suggestion> suggest_folder_tags(const std::string &root, const std::vector<scanned_file> &files)
{
    std::vector<folder_tag_suggestion> out;
    std::unordered_map<std::string, std::size_t> by_name;
    for(std::size_t i = 0; i < files.size(); ++i)
    {
        std::string name = folder_tag_path(root, files[i].path);
        if(name.empty())
            continue;
        auto added = by_name.emplace(fold_case(name), out.size());
        if(added.second)
        {
            std::size_t at = name.rfind(tag_path_separator);
            out.push_back({name, {}, at != std::string::npos && generic_folder_name(name.substr(at + 1))});
        }
        out[added.first->second].files.push_back(i);
    }
    return out;
}
