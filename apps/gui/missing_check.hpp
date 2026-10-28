#ifndef MISSING_CHECK_HPP
#define MISSING_CHECK_HPP

#include <windows.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Posted to the window when a check is done; the window then takes its result.
const UINT kMissingChecked = WM_APP + 6;

// Finds which files are no longer at their path (see file_missing) on a thread of its own, so a slow network drive
// can't freeze the window. The thread only gets the paths, never the library. It also reads each present file's
// size, for sorting, so the disk is read once.
//
// Only the window's thread calls these.
class MissingCheck
{
public:
    MissingCheck() = default;
    MissingCheck(const MissingCheck &) = delete;
    MissingCheck &operator=(const MissingCheck &) = delete;
    ~MissingCheck() { Stop(); }

    // Checks every path in 'paths', then posts kMissingChecked to 'hWnd'. A check asked for while one runs waits
    // for it, then runs on the paths of this later call.
    void Start(HWND hWnd, std::vector<std::string> paths);
    // The window is closing: a check left running finishes on its own and posts nothing.
    void Stop();

    // After kMissingChecked: the paths found missing by the check that just ended, and the size in bytes of every
    // other one whose size could be read. Returns false if none ended since the last call (a stale message),
    // leaving both alone.
    bool Take(std::unordered_set<std::string> &missing, std::unordered_map<std::string, std::uint64_t> &sizes);

private:
    struct Shared
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once stopped
        bool done = false;
        std::unordered_set<std::string> missing;
        std::unordered_map<std::string, std::uint64_t> sizes;
    };

    void Launch(std::vector<std::string> paths);
    static void Run(std::shared_ptr<Shared> shared, std::vector<std::string> paths);

    HWND m_hWnd = nullptr;
    std::shared_ptr<Shared> m_running;  // the check under way, if any
    bool m_queued = false;              // another one was asked for meanwhile
    std::vector<std::string> m_queuedPaths;
};

#endif
