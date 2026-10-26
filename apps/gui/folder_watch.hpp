#ifndef FOLDER_WATCH_HPP
#define FOLDER_WATCH_HPP

#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include "../../include/folder_import.hpp"

// Posted to the window when a rescan found files; the window then takes them.
const UINT kWatchFound = WM_APP + 11;

// Keeps the watched folders (see library::watch_folder) in step with the disk, on a thread of its own: when it
// starts, it rescans each folder once for files saved while Corpus was closed, then ReadDirectoryChangesW reports
// files created or renamed in them and, once a folder has been quiet for a moment, it is rescanned. A rescan only
// lists the folder (scan_folder) and reads the identity of the files it hasn't seen yet, so it stays cheap.
// Folders on drives that can be unplugged (USB, memory cards) aren't watched that way, since an open folder would
// keep Windows from ejecting the drive; neither are folders that can't be opened (a drive that isn't there). Those
// are rescanned instead when the window comes back to the front (see Rescan), at most once a minute.
//
// The thread never touches the library: it gets the paths the library had when it started, remembers every file it
// reports, and the window decides what to do with them (library::sync_watched). Only the window's thread calls these.
class FolderWatch
{
public:
    // What a rescan found that the thread hadn't seen: the folder, and the files, sorted by path.
    struct Found
    {
        std::string folder;
        std::vector<scanned_file> files;
    };

    FolderWatch() = default;
    FolderWatch(const FolderWatch &) = delete;
    FolderWatch &operator=(const FolderWatch &) = delete;
    ~FolderWatch() { Stop(); }

    // Starts the thread on 'folders', each rescanned once. Files in 'known' aren't reported, and neither are the
    // library's own files in 'libraryFolder' (the database and the thumbnail cache).
    void Start(HWND hWnd, std::vector<std::string> folders, std::unordered_set<std::string> known, std::string libraryFolder);
    // The watched folders changed: folders new to the list are watched from now on, without a rescan (they were just
    // imported, and their files are given in 'known'), and those left out are no longer watched.
    void SetFolders(std::vector<std::string> folders, std::vector<std::string> known);
    // The window came to the front: folders not watched live are rescanned, unless they were a minute ago.
    void Rescan();
    // The window is closing: the thread stops on its own, and posts nothing more.
    void Stop();

    // After kWatchFound: what the rescans found since the last call. False if nothing (a stale message).
    bool Take(std::vector<Found> &found);

private:
    struct Shared
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once stopped
        HANDLE wake = nullptr;  // set when there is something for the thread to do
        std::atomic<bool> stop{ false };
        bool foldersChanged = false;
        bool rescan = false;
        std::vector<std::string> folders;
        std::vector<std::string> known;  // new paths for the thread to know
        std::vector<Found> found;
        ~Shared();
    };

    static void Run(std::shared_ptr<Shared> shared, std::vector<std::string> folders,
        std::unordered_set<std::string> known, std::string exclude);

    std::shared_ptr<Shared> m_shared;
};

#endif
