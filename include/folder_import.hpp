#ifndef FOLDER_IMPORT_HPP
#define FOLDER_IMPORT_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

/// A file found by scan_folder, ready for library::add_files.
struct scanned_file
{
    std::string path;     // UTF-8
    std::string identity; // see file_identity
};

/// What scan_folder found.
struct folder_scan
{
    std::vector<scanned_file> files; // sorted by path
    int unreadable = 0;              // subfolders that couldn't be listed, and were skipped
    int skipped = 0;                 // system files and folders left out (see scan_folder)
    bool cancelled = false;          // the scan was cancelled: files only holds what was found until then
    std::error_code error;           // set if the folder itself couldn't be listed
};

/// Called by scan_folder for each file found, on the scanning thread: the running count and the file's UTF-8 path.
using scan_progress = std::function<void(std::size_t found, const std::string &current)>;

/// Lists every file under a folder and reads their identities. Touches only the file system, never a library, so
/// it can run on any thread; pass the result to library::add_files on the library's thread. Links to folders
/// aren't followed and a folder reached twice is listed once. Unlistable subfolders are skipped and counted, and so
/// are system files (desktop.ini, Thumbs.db, .DS_Store, and on Windows anything hidden and system, folders too).
/// Paths come back absolute. `cancel` stops the scan when it reads true. `known` paths are left out and their
/// identity isn't read, so a rescan only pays for what is new.
folder_scan scan_folder(const std::string &folder, const std::atomic<bool> *cancel = nullptr,
    const scan_progress &progress = nullptr, const std::unordered_set<std::string> *known = nullptr);

/// True for names programs use only while writing a file: .crdownload, .part, .partial, .download, .tmp, .temp,
/// office lock files (~$name, .~lock.name#). Watched folders don't add them (see library::sync_watched).
bool temporary_file(const std::string &path);

/// A tag the folders of an import suggest, and the files it would go on.
struct folder_tag_suggestion
{
    std::string name;                 // a valid tag name (see normalize_tag_name)
    std::vector<std::size_t> files;   // indexes into the scanned files, ascending
};

/// The folder names that describe a file: the import root's name, then each folder down to the file. Looks at
/// strings only. Names go through normalize_tag_name ("Holiday Photos" -> "Holiday_Photos"), unusable ones are
/// dropped, and a name repeated at another level appears once. A drive root or "/" gives none. With more than
/// `max_names`, the root and the folders nearest the file are kept (Music/A/B/C/D/E/F/track.mp3 with 4 gives
/// Music, D, E, F). Empty if the file isn't under root.
std::vector<std::string> folder_tags(const std::string &root, const std::string &file_path, std::size_t max_names = 6);

/// folder_tags for a whole scan, one suggestion per name (case aside) in order of first use. The GUI shows them
/// for confirmation, then applies each with library::edit_files after library::add_files.
std::vector<folder_tag_suggestion> suggest_folder_tags(const std::string &root, const std::vector<scanned_file> &files,
    std::size_t max_names = 6);

#endif
