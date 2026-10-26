#ifndef FILE_KIND_HPP
#define FILE_KIND_HPP

#include <string>

// What a file is, told by its extension alone, so it costs no disk access: the grid draws its icon from it and the
// sidebar's Pictures, Videos and Books views pick files by it.
enum class FileKind { kOther, kPicture, kVideo, kAudio, kDocument, kBook, kArchive };

// The extension of the path's file name in lower case (ASCII letters only, as extensions are), without the dot, or
// empty if it has none.
std::string FileExtension(const std::string &path);

FileKind KindOf(const std::string &path);

#endif
