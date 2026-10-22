#ifndef FILE_IDENTITY_HPP
#define FILE_IDENTITY_HPP

#include <string>

/// @brief Reads the identity the file system gives a file, which stays the same when the file is renamed or
/// moved within its volume. On Windows it is the volume serial number and the NTFS file ID; elsewhere the
/// device and inode numbers.
/// @param utf8_path Path to a file or folder, in UTF-8.
/// @return The identity as text, or an empty string if it can't be read (the file doesn't exist, say).
std::string file_identity(const std::string &utf8_path);

/// @brief Tells whether nothing exists at a path anymore. A path that can't be checked (no permission, a
/// drive that isn't plugged in) doesn't count as missing.
bool file_missing(const std::string &utf8_path);

#endif
