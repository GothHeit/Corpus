#ifndef FOLDER_IMPORT_HPP
#define FOLDER_IMPORT_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

/// @brief A file found by scan_folder, ready for library::add_files.
struct scanned_file
{
    std::string path;     // UTF-8
    std::string identity; // see file_identity
};

/// @brief What scan_folder found.
struct folder_scan
{
    std::vector<scanned_file> files; // sorted by path
    int unreadable = 0;              // subfolders that couldn't be listed, and were skipped
    bool cancelled = false;          // the scan was cancelled: files only holds what was found until then
    std::error_code error;           // set if the folder itself couldn't be listed
};

/// @brief Called by scan_folder for each file it finds, on the thread running the scan.
/// @param found How many files were found so far, this one included.
/// @param current The file's path, in UTF-8.
using scan_progress = std::function<void(std::size_t found, const std::string &current)>;

/// @brief Lists every file in a folder and the folders under it, and reads their identities. Only touches
/// the file system, never a library or a database, so it can run on any thread: pass the result to
/// library::add_files on the thread that owns the library.
/// Links to folders aren't followed, and a folder reached twice (a junction to a folder above it, say) is
/// only listed once. Subfolders that can't be listed are skipped and counted. Folders, and anything that
/// isn't a regular file, aren't returned.
/// @param folder Folder to scan, in UTF-8. Paths are returned in full, starting from it made absolute.
/// @param cancel If given, the scan stops as soon as it reads true.
/// @param progress If given, called for each file found.
folder_scan scan_folder(const std::string &folder, const std::atomic<bool> *cancel = nullptr,
    const scan_progress &progress = nullptr);

#endif
