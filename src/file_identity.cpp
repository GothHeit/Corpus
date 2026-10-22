#include <cstdio>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <cwchar>
#include <windows.h>
#else
#include <sys/stat.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#endif

#include "../include/file_identity.hpp"

static void append_hex(std::string &out, unsigned long long value, int digits)
{
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%0*llx", digits, value);
    out += buffer;
}

#ifdef _WIN32

std::string file_identity(const std::string &utf8_path)
{
    std::wstring wide = std::filesystem::u8path(utf8_path).wstring();
    // Reading attributes needs no read access, so files opened by other programs can be checked too.
    // FILE_FLAG_BACKUP_SEMANTICS lets folders be opened as well.
    HANDLE h = CreateFileW(wide.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if(h == INVALID_HANDLE_VALUE)
        return "";

    // FAT and exFAT have no real file IDs: Windows makes one from the directory entry, which changes when the
    // file moves and is given to the next file put there. Such an identity would only find the wrong file.
    wchar_t fs_name[MAX_PATH + 1];
    if(GetVolumeInformationByHandleW(h, nullptr, 0, nullptr, nullptr, nullptr, fs_name, MAX_PATH + 1)
        && (wcsncmp(fs_name, L"FAT", 3) == 0 || wcscmp(fs_name, L"exFAT") == 0))
    {
        CloseHandle(h);
        return "";
    }

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
    CloseHandle(h);
    return out;
}

#else

std::string file_identity(const std::string &utf8_path)
{
    struct stat st;
    if(stat(utf8_path.c_str(), &st) != 0)
        return "";
#ifdef __linux__
    // FAT and exFAT inode numbers come from the directory entry, so they change when the file moves.
    struct statfs fs;
    if(statfs(utf8_path.c_str(), &fs) == 0 && (fs.f_type == 0x4d44 || fs.f_type == 0x2011BAB0))
        return "";
#endif
    std::string out;
    append_hex(out, static_cast<unsigned long long>(st.st_dev), 16);
    out += ':';
    append_hex(out, static_cast<unsigned long long>(st.st_ino), 16);
    return out;
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
