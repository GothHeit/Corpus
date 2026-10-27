#ifndef BACKUP_HPP
#define BACKUP_HPP

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

class database;
class library;

/// @brief Writes the catalog (every file with its tags, identity and reading progress, every tag, the watched
/// folders) to one portable JSON file. It is written to a temporary file first, so a failure never leaves a
/// half-written export in place of an old one.
/// @return Whether it was written; error says why if not.
bool export_catalog(const library &lib, const std::filesystem::path &dest, std::string &error);

/// @brief What import_catalog did.
struct catalog_import
{
    bool ok = false;
    std::string error;
    int files_added = 0;      // files the library did not have
    int files_updated = 0;    // files it had that gained tags or reading progress
    int tags_added = 0;       // tags the library did not have
    int skipped = 0;          // entries or tag names the file had but that were not valid
};

/// @brief Merges an exported catalog into lib. Files are matched by path (as the library does), tags by name
/// with case ignored; nothing is ever removed or replaced: a known file gains the tags it lacked (and its reading
/// progress if it has none), an unknown one is added with its identity, tags and progress, and watched folders
/// are watched again. With a database attached it is one atomic group (inside the caller's begin/commit if there
/// is one); on failure the database is rolled back to before the call, but memory may differ from it, so the
/// caller should reload the library like after any failed change.
catalog_import import_catalog(library &lib, const std::filesystem::path &source);

/// @brief Where the automatic backups go by default: %LOCALAPPDATA%\Corpus\backups on Windows, otherwise
/// $XDG_DATA_HOME/Corpus/backups or ~/.local/share/Corpus/backups. Empty if no base folder is known.
std::filesystem::path default_backup_folder();

/// @brief A database backup file in a backup folder.
struct backup_info
{
    std::filesystem::path file;
    std::time_t when = 0;        // from its name (UTC)
    std::uintmax_t size = 0;
};

/// @brief The backups in a folder, newest first. Only files named corpus-YYYYMMDD-HHMMSS.db count.
std::vector<backup_info> list_backups(const std::filesystem::path &folder);

/// @brief What backup_database did.
struct backup_result
{
    bool ok = false;               // false only on a real failure (see error)
    bool made = false;             // a new backup was written
    std::filesystem::path file;    // the new backup, if made
    int removed = 0;               // old backups deleted to keep to the limit
    std::string error;
};

/// @brief Takes a rolling backup of the open database into a folder (made if needed), then deletes the oldest so
/// at most `keep` stay. Meant to be called at startup, outside begin/commit. It does nothing (ok, not made) when
/// the catalog is empty, so an empty catalog never pushes good backups out, or when the newest backup is younger
/// than min_age_seconds (0 always backs up).
backup_result backup_database(database &db, const std::filesystem::path &folder, int keep = 7,
    std::int64_t min_age_seconds = 12 * 3600);

/// @brief Puts a backup in place of a database file. The database must not be open. The backup is checked first
/// (database::verify_file); the file it replaces is kept next to it as <name>.before-restore, and its -wal/-shm
/// files are removed so SQLite doesn't replay them over the restored data.
bool restore_database(const std::filesystem::path &backup, const std::filesystem::path &db_file, std::string &error);

#endif
