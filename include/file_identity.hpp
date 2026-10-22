#ifndef FILE_IDENTITY_HPP
#define FILE_IDENTITY_HPP

#include <string>
#include <vector>

/// @brief Reads the identity the file system gives a file, which stays the same when the file is renamed or
/// moved within its volume. On Windows it is the volume serial number and the NTFS file ID; elsewhere the
/// device and inode numbers. Files on FAT and exFAT volumes (most USB sticks and memory cards) have none,
/// since those file systems can't tell a file apart once it moves.
/// @param utf8_path Path to a file or folder, in UTF-8.
/// @return The identity as text, or an empty string if it can't be read (the file doesn't exist, say) or the
/// file is on FAT or exFAT.
std::string file_identity(const std::string &utf8_path);

/// @brief Tells whether a path is on a FAT or exFAT volume, whose files get no identity (see file_identity),
/// and which one.
/// @return The volume part of an identity (the text before ':') as it would read on that volume, or an empty
/// string if the path isn't on FAT or exFAT, or that can't be checked (the path doesn't exist, say).
std::string fat_volume(const std::string &utf8_path);

/// @brief Lists the folders where a volume is mounted: drive roots and folder mounts on Windows, every mount
/// point on Linux.
/// @param out Receives the mount points, in UTF-8.
/// @return false if they can't be listed (on other systems, or the list can't be read).
bool mount_points(std::vector<std::string> &out);

/// @brief Tells whether nothing exists at a path anymore. A path that can't be checked (no permission, a
/// drive that isn't plugged in) doesn't count as missing.
bool file_missing(const std::string &utf8_path);

#endif
