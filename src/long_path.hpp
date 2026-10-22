#ifndef LONG_PATH_HPP
#define LONG_PATH_HPP

#ifdef _WIN32
#include <filesystem>
#include <string>

/// @brief Writes a path for the wide Windows calls so it may be longer than MAX_PATH (260 characters): an
/// absolute path gets the \\?\ prefix, which turns off that limit. Such a path is used as it is, so it must be
/// in full, with backslashes and no "." or "..": the path is made so first.
inline std::wstring long_path(const std::filesystem::path &path)
{
    std::error_code ec;
    std::wstring full = std::filesystem::absolute(path, ec).lexically_normal().wstring();
    // The prefix also turns off dropping a trailing separator, which only a drive root ("C:\") may keep.
    if(full.size() > 3 && full.back() == L'\\')
        full.pop_back();
    if(ec || full.size() < 3 || full.compare(0, 4, L"\\\\?\\") == 0)
        return path.wstring();
    if(full.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + full.substr(2); // \\server\share
    if(full[1] == L':' && full[2] == L'\\')
        return L"\\\\?\\" + full;
    return path.wstring();
}
#endif

#endif
