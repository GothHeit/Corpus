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
    int skipped = 0;                 // system files and folders left out (see scan_folder)
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
/// isn't a regular file, aren't returned. Files the system keeps for itself are skipped and counted:
/// desktop.ini, Thumbs.db and .DS_Store everywhere, and on Windows whatever is both hidden and system
/// (folders too, like $RECYCLE.BIN, without what is in them).
/// @param folder Folder to scan, in UTF-8. Paths are returned in full, starting from it made absolute.
/// @param cancel If given, the scan stops as soon as it reads true.
/// @param progress If given, called for each file found.
folder_scan scan_folder(const std::string &folder, const std::atomic<bool> *cancel = nullptr,
    const scan_progress &progress = nullptr);

/// @brief A tag the folders of an import suggest, and the files it would go on.
struct folder_tag_suggestion
{
    std::string name;                 // a valid tag name (see normalize_tag_name)
    std::vector<std::size_t> files;   // indexes into the scanned files, ascending
};

/// @brief The folder names that describe a file: the import root's own name, then each folder between the root
/// and the file, outermost first. Only looks at the strings, not the file system, so any thread will do.
/// Names go through normalize_tag_name (spaces become '_': "Holiday Photos" -> "Holiday_Photos") and are dropped
/// if nothing usable is left; a name repeated at another level appears once (compared with fold_case).
/// A drive root or "/" has no name and gives none. A file straight in the root gets just the root's name.
/// With more than `max_names` names, the root's name and the folders closest to the file are kept, and the
/// ones in between dropped (Music/A/B/C/D/E/F/track.mp3 with 4 gives Music, D, E, F).
/// @param root Folder that was imported, in UTF-8 (made absolute the way scan_folder does).
/// @param file_path Path of a file found under it, as scan_folder returned it.
/// @return The names, or none if the file isn't under the root.
std::vector<std::string> folder_tags(const std::string &root, const std::string &file_path, std::size_t max_names = 6);

/// @brief folder_tags for a whole scan, grouped by tag: one suggestion per name (case aside), in order of first
/// use, each with the files it applies to. The GUI shows them for the user to confirm, then applies each with
/// library::edit_files after library::add_files.
std::vector<folder_tag_suggestion> suggest_folder_tags(const std::string &root, const std::vector<scanned_file> &files,
    std::size_t max_names = 6);

#endif
