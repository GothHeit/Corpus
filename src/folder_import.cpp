#include <algorithm>
#include <filesystem>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <cwchar>
#include <windows.h>
#endif

#include "../include/file_identity.hpp"
#include "../include/folder_import.hpp"
#include "long_path.hpp"

enum class entry_kind { file, folder, skipped, other };

struct folder_entry
{
    std::filesystem::path path;
    entry_kind kind;
};

/// @brief Tells whether a file is one Windows or macOS keeps in folders for itself (folder settings, thumbnail
/// caches), which nobody means to catalog. Names are compared ignoring case, as Windows does.
static bool system_clutter(const std::filesystem::path &name)
{
    std::string n = name.u8string();
    std::transform(n.begin(), n.end(), n.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
    return n == "desktop.ini" || n == "thumbs.db" || n == ".ds_store";
}

/// @brief Lists what is in a folder, without going into its subfolders. Links to folders count as other: they
/// aren't followed. On Windows the folder is read directly, since MinGW's std::filesystem can neither list
/// folders past MAX_PATH nor tell a junction or folder symbolic link from a folder. Other reparse points there,
/// like OneDrive folders that aren't downloaded yet, are real folders. Files and folders both hidden and system
/// on Windows ($RECYCLE.BIN, System Volume Information), and the files system_clutter names, are skipped.
/// @return false if the folder can't be listed (ec says why); out then holds what was read until then.
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

folder_scan scan_folder(const std::string &folder, const std::atomic<bool> *cancel, const scan_progress &progress)
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
