#ifndef FILE_IDENTITY_HPP
#define FILE_IDENTITY_HPP

#include <string>
#include <unordered_map>
#include <vector>

/// Remembers which volumes are FAT or exFAT so file_identity checks each volume once, not per file. One per thread.
struct fat_cache
{
    std::unordered_map<std::string, bool> fat; // by volume, as written in an identity
};

/// The identity the file system gives a file, stable across rename and move within its volume: volume serial and
/// NTFS file ID on Windows, device and inode elsewhere. FAT and exFAT (most USB sticks and cards) have none.
/// Empty if unreadable or on FAT/exFAT. `cache` remembers FAT volumes across calls.
std::string file_identity(const std::string &utf8_path, fat_cache *cache = nullptr);

/// The volume part of an identity (text before ':') if the path is on FAT or exFAT, else empty (also if it can't be
/// checked).
std::string fat_volume(const std::string &utf8_path);

/// Absolute path with symlinks (and junctions on Windows) resolved, in UTF-8; empty if it can't be resolved.
std::string real_path(const std::string &utf8_path);

/// Lists where volumes are mounted: drive roots and folder mounts on Windows, mount points on Linux. False if they
/// can't be listed.
bool mount_points(std::vector<std::string> &out);

/// True if nothing exists at the path. A path that can't be checked (no permission, unplugged drive) isn't missing.
bool file_missing(const std::string &utf8_path);

#endif
