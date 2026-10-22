#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <cwchar>
#include <windows.h>
#else
#include <sys/stat.h>
#ifdef __linux__
#include <fstream>
#include <sys/vfs.h>
#endif
#endif

#include "../include/file_identity.hpp"
#include "long_path.hpp"

static void append_hex(std::string &out, unsigned long long value, int digits)
{
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%0*llx", digits, value);
    out += buffer;
}

// Asks check whether a volume is FAT or exFAT, only once per volume if there is a cache.
template<class Check> static bool is_fat_cached(fat_cache *cache, const std::string &volume, Check check)
{
    if(!cache)
        return check();
    auto known = cache->fat.find(volume);
    if(known == cache->fat.end())
        known = cache->fat.emplace(volume, check()).first;
    return known->second;
}

#ifdef _WIN32

static HANDLE open_attributes(const std::string &utf8_path)
{
    std::wstring wide = long_path(std::filesystem::u8path(utf8_path));
    // Reading attributes needs no read access, so files opened by other programs can be checked too.
    // FILE_FLAG_BACKUP_SEMANTICS lets folders be opened as well.
    return CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

static bool is_fat(HANDLE h)
{
    wchar_t fs_name[MAX_PATH + 1];
    return GetVolumeInformationByHandleW(h, nullptr, 0, nullptr, nullptr, nullptr, fs_name, MAX_PATH + 1)
        && (wcsncmp(fs_name, L"FAT", 3) == 0 || wcscmp(fs_name, L"exFAT") == 0);
}

static std::string read_identity(HANDLE h)
{
    std::string out;
    FILE_ID_INFO info;
    BY_HANDLE_FILE_INFORMATION legacy;
    if(GetFileInformationByHandleEx(h, FileIdInfo, &info, sizeof info))
    {
        // 64-bit volume serial number and 128-bit file ID (ReFS needs all of it).
        append_hex(out, info.VolumeSerialNumber, 16);
        out += ':';
        for(BYTE b : info.FileId.Identifier)
            append_hex(out, b, 2);
    }
    else if(GetFileInformationByHandle(h, &legacy))
    {
        // File systems or Windows versions without FILE_ID_INFO: 32-bit serial and 64-bit index.
        append_hex(out, legacy.dwVolumeSerialNumber, 8);
        out += ':';
        append_hex(out, (static_cast<unsigned long long>(legacy.nFileIndexHigh) << 32) | legacy.nFileIndexLow, 16);
    }
    return out;
}

std::string file_identity(const std::string &utf8_path, fat_cache *cache)
{
    HANDLE h = open_attributes(utf8_path);
    if(h == INVALID_HANDLE_VALUE)
        return "";

    // FAT and exFAT have no real file IDs: Windows makes one from the directory entry, which changes when the
    // file moves and is given to the next file put there. Such an identity would only find the wrong file.
    std::string out = read_identity(h);
    bool fat = !out.empty() && is_fat_cached(cache, out.substr(0, out.find(':')), [h] { return is_fat(h); });
    CloseHandle(h);
    return fat ? "" : out;
}

std::string real_path(const std::string &utf8_path)
{
    HANDLE h = open_attributes(utf8_path);
    if(h == INVALID_HANDLE_VALUE)
        return "";
    std::wstring out(MAX_PATH, L'\0');
    DWORD length = GetFinalPathNameByHandleW(h, &out[0], static_cast<DWORD>(out.size()), VOLUME_NAME_DOS);
    if(length >= out.size())
    {
        out.resize(length);
        length = GetFinalPathNameByHandleW(h, &out[0], static_cast<DWORD>(out.size()), VOLUME_NAME_DOS);
    }
    CloseHandle(h);
    if(length == 0 || length >= out.size())
        return "";
    out.resize(length);
    // The path comes back as \\?\C:\... or \\?\UNC\server\share\...
    if(out.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        out = L"\\" + out.substr(7);
    else if(out.compare(0, 4, L"\\\\?\\") == 0)
        out = out.substr(4);
    return std::filesystem::path(out).u8string();
}

std::string fat_volume(const std::string &utf8_path)
{
    HANDLE h = open_attributes(utf8_path);
    if(h == INVALID_HANDLE_VALUE)
        return "";
    // Read the way file_identity did before it skipped FAT, so it matches identities recorded back then.
    std::string out = is_fat(h) ? read_identity(h) : "";
    CloseHandle(h);
    return out.substr(0, out.find(':'));
}

bool mount_points(std::vector<std::string> &out)
{
    wchar_t volume[MAX_PATH + 1];
    HANDLE find = FindFirstVolumeW(volume, MAX_PATH + 1);
    if(find == INVALID_HANDLE_VALUE)
        return false;
    std::vector<wchar_t> names(MAX_PATH + 1);
    do
    {
        DWORD needed = 0;
        if(!GetVolumePathNamesForVolumeNameW(volume, names.data(), static_cast<DWORD>(names.size()), &needed))
        {
            if(GetLastError() != ERROR_MORE_DATA)
                continue;
            names.resize(needed);
            if(!GetVolumePathNamesForVolumeNameW(volume, names.data(), static_cast<DWORD>(names.size()), &needed))
                continue;
        }
        // Each path ends with a null, and an empty one ends the list.
        for(const wchar_t *name = names.data(); *name; name += wcslen(name) + 1)
            out.push_back(std::filesystem::path(name).u8string());
    } while(FindNextVolumeW(find, volume, MAX_PATH + 1));
    FindVolumeClose(find);
    return true;
}

#else

#ifdef __linux__
// FAT and exFAT inode numbers come from the directory entry, so they change when the file moves.
static bool is_fat(const char *path)
{
    struct statfs fs;
    return statfs(path, &fs) == 0 && (fs.f_type == 0x4d44 || fs.f_type == 0x2011BAB0);
}
#endif

std::string file_identity(const std::string &utf8_path, fat_cache *cache)
{
    struct stat st;
    if(stat(utf8_path.c_str(), &st) != 0)
        return "";
    std::string out;
#ifdef __linux__
    append_hex(out, static_cast<unsigned long long>(st.st_dev), 16);
    if(is_fat_cached(cache, out, [&utf8_path] { return is_fat(utf8_path.c_str()); }))
        return "";
    out.clear();
#else
    (void)cache;
#endif
    append_hex(out, static_cast<unsigned long long>(st.st_dev), 16);
    out += ':';
    append_hex(out, static_cast<unsigned long long>(st.st_ino), 16);
    return out;
}

std::string fat_volume(const std::string &utf8_path)
{
    std::string out;
#ifdef __linux__
    struct stat st;
    if(is_fat(utf8_path.c_str()) && stat(utf8_path.c_str(), &st) == 0)
        append_hex(out, static_cast<unsigned long long>(st.st_dev), 16);
#else
    (void)utf8_path;
#endif
    return out;
}

bool mount_points(std::vector<std::string> &out)
{
#ifdef __linux__
    std::ifstream in("/proc/self/mountinfo");
    if(!in)
        return false;
    std::string line;
    while(std::getline(in, line))
    {
        // The fifth field is the mount point, with spaces and other special characters written as \ooo.
        std::size_t start = 0;
        for(int i = 0; i < 4 && start != std::string::npos; i++)
            start = line.find(' ', start + (i > 0));
        if(start == std::string::npos)
            continue;
        std::string field = line.substr(start + 1, line.find(' ', start + 1) - start - 1);
        std::string path;
        for(std::size_t i = 0; i < field.size(); i++)
        {
            if(field[i] == '\\' && i + 3 < field.size())
            {
                path += static_cast<char>(std::stoi(field.substr(i + 1, 3), nullptr, 8));
                i += 3;
            }
            else
                path += field[i];
        }
        out.push_back(path);
    }
    return true;
#else
    (void)out;
    return false;
#endif
}

#endif

#ifndef _WIN32
std::string real_path(const std::string &utf8_path)
{
    std::error_code ec;
    std::filesystem::path out = std::filesystem::canonical(std::filesystem::u8path(utf8_path), ec);
    return ec ? "" : out.u8string();
}
#endif

bool file_missing(const std::string &utf8_path)
{
    std::filesystem::path path = std::filesystem::u8path(utf8_path);
    std::error_code ec;
    if(std::filesystem::status(path, ec).type() != std::filesystem::file_type::not_found)
        return false;
    // A file on a drive that isn't there (an unplugged USB disk) is out of reach, not gone.
    std::filesystem::path root = path.root_path();
    return root.empty() || std::filesystem::status(root, ec).type() != std::filesystem::file_type::not_found;
}
