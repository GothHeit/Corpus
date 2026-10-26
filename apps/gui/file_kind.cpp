#include "file_kind.hpp"
#include <unordered_map>

std::string FileExtension(const std::string &path)
{
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return std::string();

    std::string ext = path.substr(dot + 1);
    for (char &c : ext)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

FileKind KindOf(const std::string &path)
{
    // Pictures are what Windows has a codec for, or makes thumbnails of; videos what libmpv plays (see
    // IsPlayableVideo); books what the reader opens, and the other ebook and comic formats.
    static const std::unordered_map<std::string, FileKind> kinds =
    {
        { "jpg", FileKind::kPicture }, { "jpeg", FileKind::kPicture }, { "jfif", FileKind::kPicture },
        { "png", FileKind::kPicture }, { "gif", FileKind::kPicture }, { "webp", FileKind::kPicture },
        { "bmp", FileKind::kPicture }, { "tif", FileKind::kPicture }, { "tiff", FileKind::kPicture },
        { "heic", FileKind::kPicture }, { "heif", FileKind::kPicture }, { "avif", FileKind::kPicture },
        { "jxl", FileKind::kPicture }, { "svg", FileKind::kPicture }, { "ico", FileKind::kPicture },
        { "raw", FileKind::kPicture }, { "cr2", FileKind::kPicture }, { "cr3", FileKind::kPicture },
        { "nef", FileKind::kPicture }, { "arw", FileKind::kPicture }, { "orf", FileKind::kPicture },
        { "rw2", FileKind::kPicture }, { "raf", FileKind::kPicture }, { "dng", FileKind::kPicture },
        { "psd", FileKind::kPicture }, { "psb", FileKind::kPicture },
        { "mp4", FileKind::kVideo }, { "m4v", FileKind::kVideo }, { "mkv", FileKind::kVideo },
        { "mov", FileKind::kVideo }, { "avi", FileKind::kVideo }, { "webm", FileKind::kVideo },
        { "wmv", FileKind::kVideo }, { "asf", FileKind::kVideo }, { "flv", FileKind::kVideo },
        { "f4v", FileKind::kVideo }, { "mpg", FileKind::kVideo }, { "mpeg", FileKind::kVideo },
        { "m2v", FileKind::kVideo }, { "ts", FileKind::kVideo }, { "mts", FileKind::kVideo },
        { "m2ts", FileKind::kVideo }, { "vob", FileKind::kVideo }, { "ogv", FileKind::kVideo },
        { "3gp", FileKind::kVideo }, { "3g2", FileKind::kVideo }, { "divx", FileKind::kVideo },
        { "rm", FileKind::kVideo }, { "rmvb", FileKind::kVideo },
        { "mp3", FileKind::kAudio }, { "flac", FileKind::kAudio }, { "wav", FileKind::kAudio },
        { "ogg", FileKind::kAudio }, { "m4a", FileKind::kAudio }, { "aac", FileKind::kAudio },
        { "opus", FileKind::kAudio }, { "wma", FileKind::kAudio },
        { "pdf", FileKind::kBook }, { "epub", FileKind::kBook }, { "cbz", FileKind::kBook },
        { "cbr", FileKind::kBook }, { "cb7", FileKind::kBook }, { "mobi", FileKind::kBook },
        { "azw", FileKind::kBook }, { "azw3", FileKind::kBook }, { "fb2", FileKind::kBook },
        { "djvu", FileKind::kBook }, { "txt", FileKind::kBook },
        { "md", FileKind::kDocument }, { "doc", FileKind::kDocument }, { "docx", FileKind::kDocument },
        { "odt", FileKind::kDocument }, { "rtf", FileKind::kDocument }, { "xls", FileKind::kDocument },
        { "xlsx", FileKind::kDocument }, { "ods", FileKind::kDocument }, { "csv", FileKind::kDocument },
        { "ppt", FileKind::kDocument }, { "pptx", FileKind::kDocument }, { "odp", FileKind::kDocument },
        { "html", FileKind::kDocument }, { "htm", FileKind::kDocument },
        { "zip", FileKind::kArchive }, { "rar", FileKind::kArchive }, { "7z", FileKind::kArchive },
        { "tar", FileKind::kArchive }, { "gz", FileKind::kArchive }, { "bz2", FileKind::kArchive },
        { "xz", FileKind::kArchive }, { "zst", FileKind::kArchive },
    };
    auto kind = kinds.find(FileExtension(path));
    return kind == kinds.end() ? FileKind::kOther : kind->second;
}
