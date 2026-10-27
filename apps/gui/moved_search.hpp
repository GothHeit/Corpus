#ifndef MOVED_SEARCH_HPP
#define MOVED_SEARCH_HPP

#include <windows.h>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

// Posted to the window when a search is done; the window then takes its result.
const UINT kMovedFound = WM_APP + 9;

// Looks for missing files in a folder the user picked (library::find_moved) on a thread of its own. The thread
// searches a library of its own holding only the missing files, so the window's library stays the window thread's,
// and a cancelled search is left to end on its own. What it finds is only offered; the window asks before relinking.
//
// Only the window's thread calls these.
class MovedSearch
{
public:
    struct Missing
    {
        std::string path;
        std::string identity;  // see file::get_identity
    };
    struct Found
    {
        std::string path;     // where the library has the file
        std::string newPath;  // where it is now
    };

    MovedSearch() = default;
    MovedSearch(const MovedSearch &) = delete;
    MovedSearch &operator=(const MovedSearch &) = delete;
    ~MovedSearch() { Stop(); }

    // Searches 'folder' and what is in it for the 'missing' files, then posts kMovedFound to 'hWnd'. Replaces a
    // search under way.
    void Start(HWND hWnd, const std::string &folder, std::vector<Missing> missing);
    // Drops the search under way: it ends on its own and posts nothing.
    void Stop();
    bool Running() const { return m_running != nullptr; }
    // The folder searched, while Running.
    const std::string &Folder() const { return m_folder; }

    // After kMovedFound: the files found, and why the search stopped early, if it did (std::errc::cross_device_link:
    // none of the files can be on the folder's drive). Returns false if no search ended since the last call (a
    // stale message).
    bool Take(std::vector<Found> &found, std::error_code &error);

private:
    struct Shared
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once dropped
        bool done = false;
        std::vector<Found> found;
        std::error_code error;
    };

    static void Run(std::shared_ptr<Shared> shared, std::string folder, std::vector<Missing> missing);

    std::shared_ptr<Shared> m_running;
    std::string m_folder;
};

#endif
