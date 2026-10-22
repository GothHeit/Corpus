#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include "../include/file_identity.hpp"
#include "../include/folder_import.hpp"

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
    std::vector<fs::path> pending{root};
    while(!pending.empty())
    {
        fs::path dir = std::move(pending.back());
        pending.pop_back();
        std::string id = file_identity(dir.u8string());
        if(!id.empty() && !seen.insert(id).second)
        {
            continue;
        }

        fs::directory_iterator it(dir, ec);
        for(; !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if(cancel && cancel->load(std::memory_order_relaxed))
            {
                out.cancelled = true;
                return out;
            }
            std::error_code type_error;
            const fs::directory_entry &entry = *it;
            if(entry.is_directory(type_error))
            {
                if(!entry.is_symlink(type_error))
                {
                    pending.push_back(entry.path());
                }
            }
            else if(entry.is_regular_file(type_error))
            {
                std::string path = entry.path().u8string();
                out.files.push_back({path, file_identity(path)});
                if(progress)
                {
                    progress(out.files.size(), path);
                }
            }
        }
        if(ec)
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
    }
    std::sort(out.files.begin(), out.files.end(),
        [](const scanned_file &a, const scanned_file &b) {return a.path < b.path;});
    return out;
}
