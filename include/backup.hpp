#ifndef BACKUP_HPP
#define BACKUP_HPP

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

class database;
class library;

/// Writes the catalog (files with tags, identity and reading progress, tags, watched folders) to one portable JSON
/// file, via a temporary file so a failure never leaves a half-written export. error says why on failure.
bool export_catalog(const library &lib, const std::filesystem::path &dest, std::string &error);

/// What import_catalog did.
struct catalog_import
{
    bool ok = false;
    std::string error;
    int files_added = 0;      // files the library did not have
    int files_updated = 0;    // files it had that gained tags or reading progress
    int tags_added = 0;       // tags the library did not have
    int skipped = 0;          // entries or tag names the file had but that were not valid
};

/// Merges an exported catalog into lib. Files match by path, tags by name with case ignored. Nothing is removed or
/// replaced: known files gain missing tags (and progress if they have none), unknown ones are added, watched folders
/// are watched again. One atomic group with a database attached; on failure the database rolls back but memory may
/// differ, so reload the library.
catalog_import import_catalog(library &lib, const std::filesystem::path &source);

/// Default backup folder: %LOCALAPPDATA%\Corpus\backups on Windows, else $XDG_DATA_HOME/Corpus/backups or
/// ~/.local/share/Corpus/backups. Empty if no base folder is known.
std::filesystem::path default_backup_folder();

/// A database backup file in a backup folder.
struct backup_info
{
    std::filesystem::path file;
    std::time_t when = 0;        // from its name (UTC)
    std::uintmax_t size = 0;
};

/// The backups in a folder, newest first. Only corpus-YYYYMMDD-HHMMSS.db names count.
std::vector<backup_info> list_backups(const std::filesystem::path &folder);

/// What backup_database did.
struct backup_result
{
    bool ok = false;               // false only on a real failure (see error)
    bool made = false;             // a new backup was written
    std::filesystem::path file;    // the new backup, if made
    int removed = 0;               // old backups deleted to keep to the limit
    std::string error;
};

/// Rolling backup of the open database into a folder, keeping at most `keep`. Call at startup, outside
/// begin/commit. Does nothing when the catalog is empty (so an empty one never pushes good backups out) or the
/// newest backup is younger than min_age_seconds (0 always backs up).
backup_result backup_database(database &db, const std::filesystem::path &folder, int keep = 7,
    std::int64_t min_age_seconds = 12 * 3600);

/// Puts a backup in place of a database file (which must not be open). The backup is verified first, the old file
/// is kept as <name>.before-restore, and its -wal/-shm files are removed so SQLite doesn't replay them.
bool restore_database(const std::filesystem::path &backup, const std::filesystem::path &db_file, std::string &error);

#endif
