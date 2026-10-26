#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "folder_watch.hpp"
#include "ui_util.hpp"
#include "../../src/long_path.hpp"
#include <winioctl.h>
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <set>
#include <thread>

namespace
{
    const ULONGLONG kQuietMs = 2000;     // a folder is rescanned once no change came for this long
    const ULONGLONG kLongestWaitMs = 10000;  // or this long after the first change, while a big copy goes on
    const ULONGLONG kRescanMs = 60000;   // folders not watched live are rescanned at most this often
    const DWORD kBufferBytes = 64 * 1024;  // the most a network share takes
    const DWORD kChanges = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    const size_t kMostDirs = 256;          // past this many folders to rescan, the whole watched folder is rescanned

    // One watched folder, owned by the thread.
    struct Watch
    {
        std::string folder;
        std::wstring wide;
        const std::wstring *exclude = nullptr;
        HANDLE dir = INVALID_HANDLE_VALUE;
        OVERLAPPED overlapped = {};
        std::vector<DWORD> buffer;  // DWORD-aligned, as ReadDirectoryChangesW needs
        bool live = false;          // watched with ReadDirectoryChangesW (else rescanned when the window asks)
        bool reading = false;       // a read is under way: the buffer and the OVERLAPPED are in use
        bool failed = false;        // the last read failed: the folder is gone or can't be read
        bool dirty = false;         // to be rescanned
        bool whole = false;         // the whole folder, else only 'dirs'
        std::set<std::wstring> dirs;  // the folders that got something new, in full
        ULONGLONG firstChange = 0;
        ULONGLONG lastChange = 0;
        ULONGLONG scanned = 0;      // when it was last rescanned
    };

    // Whether 'path' is 'folder' or under it, ignoring case as Windows does.
    bool Under(const std::wstring &path, const std::wstring &folder)
    {
        if (folder.empty() || path.size() < folder.size())
            return false;
        if (CompareStringOrdinal(path.c_str(), static_cast<int>(folder.size()), folder.c_str(), static_cast<int>(folder.size()),
            TRUE) != CSTR_EQUAL)
            return false;
        return path.size() == folder.size() || path[folder.size()] == L'\\' || folder.back() == L'\\';
    }

    // Whether a path is one of the library's own files: the database (with its -wal, -shm and -journal files) in the
    // library's folder, or the thumbnail cache under it. The rest of that folder may be watched like any other.
    bool Excluded(const std::wstring &path, const std::wstring &library)
    {
        if (library.empty() || !Under(path, library) || path.size() <= library.size() + 1)
            return false;
        std::wstring rest = path.substr(library.size() + (library.back() == L'\\' ? 0 : 1));
        if (Under(rest, L"thumbnails"))
            return true;
        if (rest.find(L'\\') != std::wstring::npos)
            return false;
        for (const wchar_t *end : { L".db", L".db-wal", L".db-shm", L".db-journal" })
        {
            size_t n = wcslen(end);
            if (rest.size() > n && CompareStringOrdinal(rest.c_str() + rest.size() - n, static_cast<int>(n), end,
                static_cast<int>(n), TRUE) == CSTR_EQUAL)
                return true;
        }
        return false;
    }

    void MarkDirty(Watch &w, ULONGLONG now, bool whole)
    {
        if (!w.dirty)
            w.firstChange = now;
        w.dirty = true;
        w.lastChange = now;
        if (whole)
        {
            w.whole = true;
            w.dirs.clear();
        }
    }

    // Whether a folder under the watched one, or a folder between them, is both hidden and system ($RECYCLE.BIN,
    // System Volume Information): scan_folder skips those, and so do the rescans of a part of the folder.
    bool InSystemFolder(const Watch &w, const std::wstring &dir)
    {
        size_t start = w.wide.size() + (w.wide.back() == L'\\' ? 0 : 1);
        const DWORD hiddenSystem = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
        while (start <= dir.size())
        {
            size_t end = dir.find(L'\\', start);
            if (end == std::wstring::npos)
                end = dir.size();
            DWORD attributes = GetFileAttributesW(long_path(std::filesystem::path(dir.substr(0, end))).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & hiddenSystem) == hiddenSystem)
                return true;
            start = end + 1;
        }
        return false;
    }

    // Takes down the folders a batch of changes brought something into (a file or folder created, or renamed or moved
    // to there): the folder of each new file, or the new folder itself. The library's own files, temporary files
    // and what is inside system folders don't count, and deletions need no rescan. Too many folders, and the whole
    // watched folder is rescanned instead.
    void CameIn(Watch &w, DWORD bytes, ULONGLONG now)
    {
        const BYTE *at = reinterpret_cast<const BYTE*>(w.buffer.data());
        const BYTE *end = at + bytes;
        while (at + sizeof(FILE_NOTIFY_INFORMATION) <= end)
        {
            const FILE_NOTIFY_INFORMATION *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(at);
            if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_RENAMED_NEW_NAME)
            {
                std::wstring path = w.wide;
                if (path.back() != L'\\')
                    path += L'\\';
                path.append(info->FileName, info->FileNameLength / sizeof(WCHAR));
                DWORD attributes = GetFileAttributesW(long_path(std::filesystem::path(path)).c_str());
                // Gone already (a temporary file), or the library's own.
                if (attributes != INVALID_FILE_ATTRIBUTES && !Excluded(path, *w.exclude))
                {
                    std::wstring dir = path;
                    bool folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    if (!folder)
                        dir.erase(std::min(dir.find_last_of(L'\\'), dir.size()));
                    if ((folder || !temporary_file(WideToUtf8(path))) &&
                        (w.whole || w.dirs.count(dir) || dir.size() <= w.wide.size() || !InSystemFolder(w, dir)))
                    {
                        MarkDirty(w, now, !w.dirs.count(dir) && w.dirs.size() >= kMostDirs);
                        if (!w.whole)
                            w.dirs.insert(dir);
                    }
                }
            }
            if (!info->NextEntryOffset)
                break;
            at += info->NextEntryOffset;
        }
    }

    void CALLBACK ReadDone(DWORD error, DWORD bytes, LPOVERLAPPED overlapped)
    {
        Watch &w = *static_cast<Watch*>(overlapped->hEvent);
        w.reading = false;
        if (error == ERROR_OPERATION_ABORTED)
            return;
        // No bytes: more changed than the buffer holds, so what did is unknown.
        if (error == ERROR_NOTIFY_ENUM_DIR || (error == ERROR_SUCCESS && bytes == 0))
            MarkDirty(w, GetTickCount64(), true);
        else if (error == ERROR_SUCCESS)
            CameIn(w, bytes, GetTickCount64());
        else
            w.failed = true;
    }

    // Whether the folder's drive is one that stays: a fixed disk inside the computer, or a network share. A disk
    // on USB, FireWire or a memory card reader may say it is fixed, so the bus is asked too.
    bool StaysPlugged(const std::wstring &folder)
    {
        wchar_t volume[MAX_PATH];
        if (!GetVolumePathNameW(folder.c_str(), volume, MAX_PATH))
            return false;
        UINT type = GetDriveTypeW(volume);
        if (type == DRIVE_REMOTE)
            return true;
        if (type != DRIVE_FIXED)
            return false;
        // \\.\C: for C:\. A folder mounted volume has no letter: taken as fixed.
        if (!(volume[0] && volume[1] == L':'))
            return true;
        std::wstring device = std::wstring(L"\\\\.\\") + volume[0] + L':';
        HANDLE h = CreateFileW(device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return true;
        STORAGE_PROPERTY_QUERY query = {};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        BYTE buffer[1024] = {};
        DWORD read = 0;
        bool stays = true;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &read, nullptr) &&
            read >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
        {
            STORAGE_BUS_TYPE bus = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer)->BusType;
            stays = bus != BusTypeUsb && bus != BusType1394 && bus != BusTypeSd && bus != BusTypeMmc;
        }
        CloseHandle(h);
        return stays;
    }

    void Close(Watch &w)
    {
        if (w.dir == INVALID_HANDLE_VALUE)
            return;
        CancelIoEx(w.dir, &w.overlapped);
        // The completion routine runs on this thread, at an alertable wait; the buffer is in use until it did.
        while (w.reading)
            SleepEx(INFINITE, TRUE);
        CloseHandle(w.dir);
        w.dir = INVALID_HANDLE_VALUE;
    }

    // Opens the folder for watching, and starts reading its changes. False if it can't be watched live.
    bool Arm(Watch &w)
    {
        if (w.dir == INVALID_HANDLE_VALUE)
        {
            if (!StaysPlugged(w.wide))
                return false;
            w.dir = CreateFileW(long_path(std::filesystem::path(w.wide)).c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (w.dir == INVALID_HANDLE_VALUE)
                return false;
            w.buffer.resize(kBufferBytes / sizeof(DWORD));
            w.failed = false;
        }
        if (w.reading)
            return true;
        w.overlapped = {};
        w.overlapped.hEvent = &w;  // unused by ReadDirectoryChangesW with a completion routine: carries the watch
        if (!ReadDirectoryChangesW(w.dir, w.buffer.data(), kBufferBytes, TRUE, kChanges, nullptr, &w.overlapped, ReadDone))
        {
            CloseHandle(w.dir);
            w.dir = INVALID_HANDLE_VALUE;
            return false;
        }
        w.reading = true;
        return true;
    }
}

FolderWatch::Shared::~Shared()
{
    if (wake)
        CloseHandle(wake);
}

void FolderWatch::Start(HWND hWnd, std::vector<std::string> folders, std::unordered_set<std::string> known, std::string exclude)
{
    Stop();
    m_shared = std::make_shared<Shared>();
    m_shared->hWnd = hWnd;
    m_shared->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_shared->wake)
    {
        m_shared.reset();
        return;
    }
    m_shared->folders = folders;
    // Detached: a scan of a network folder that stopped answering can take the network's timeout, and the window must
    // still close meanwhile. It only touches its own copies and the Shared it keeps alive.
    std::thread(Run, m_shared, std::move(folders), std::move(known), std::move(exclude)).detach();
}

void FolderWatch::SetFolders(std::vector<std::string> folders, std::vector<std::string> known)
{
    if (!m_shared)
        return;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->folders = std::move(folders);
        m_shared->foldersChanged = true;
        m_shared->known.insert(m_shared->known.end(), std::make_move_iterator(known.begin()), std::make_move_iterator(known.end()));
    }
    SetEvent(m_shared->wake);
}

void FolderWatch::Rescan()
{
    if (!m_shared)
        return;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->rescan = true;
    }
    SetEvent(m_shared->wake);
}

void FolderWatch::Stop()
{
    if (!m_shared)
        return;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->hWnd = nullptr;
    }
    m_shared->stop = true;
    SetEvent(m_shared->wake);
    m_shared.reset();
}

bool FolderWatch::Take(std::vector<Found> &found)
{
    if (!m_shared)
        return false;
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    if (m_shared->found.empty())
        return false;
    found = std::move(m_shared->found);
    m_shared->found.clear();
    return true;
}

void FolderWatch::Run(std::shared_ptr<Shared> shared, std::vector<std::string> folders, std::unordered_set<std::string> known,
    std::string exclude)
{
    const std::wstring excluded = Utf8ToWide(exclude);
    std::vector<std::unique_ptr<Watch>> watches;
    auto add = [&](const std::string &folder, bool rescan)
    {
        std::unique_ptr<Watch> w(new Watch);
        w->folder = folder;
        w->wide = Utf8ToWide(folder);
        w->exclude = &excluded;
        // The first rescan is due at once.
        w->dirty = w->whole = rescan;
        w->live = Arm(*w);
        watches.push_back(std::move(w));
    };
    for (const std::string &folder : folders)
        add(folder, true);

    while (!shared->stop)
    {
        bool rescan = false, changed = false;
        std::vector<std::string> wanted;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            for (std::string &path : shared->known)
                known.insert(std::move(path));
            shared->known.clear();
            rescan = shared->rescan;
            shared->rescan = false;
            if ((changed = shared->foldersChanged))
            {
                shared->foldersChanged = false;
                wanted = shared->folders;
            }
        }
        // Outside the lock: closing a folder on a network share that stopped answering can take a while, and the
        // window's calls must not wait for it.
        if (changed)
        {
            for (auto it = watches.begin(); it != watches.end();)
            {
                if (std::find(wanted.begin(), wanted.end(), (*it)->folder) == wanted.end())
                {
                    Close(**it);
                    it = watches.erase(it);
                }
                else
                    ++it;
            }
            for (const std::string &folder : wanted)
            {
                if (std::none_of(watches.begin(), watches.end(), [&](const std::unique_ptr<Watch> &w) { return w->folder == folder; }))
                    add(folder, false);
            }
        }

        ULONGLONG now = GetTickCount64();
        for (std::unique_ptr<Watch> &w : watches)
        {
            // A folder deleted, or a share gone: rescanned when the window asks, like a folder on a USB drive.
            if (w->failed)
            {
                Close(*w);
                w->live = false;
            }
            if (w->live)
                w->live = Arm(*w);
            else if (rescan)
            {
                // It may be back (the drive plugged in again): then it is live from now on.
                w->live = Arm(*w);
                if (now - w->scanned >= kRescanMs)
                {
                    MarkDirty(*w, 0, true);
                    w->firstChange = w->lastChange = 0;
                }
            }
        }

        DWORD timeout = INFINITE;
        for (std::unique_ptr<Watch> &w : watches)
        {
            if (shared->stop)
                break;
            if (!w->dirty)
                continue;
            ULONGLONG due = std::min(w->lastChange + kQuietMs, w->firstChange + kLongestWaitMs);
            now = GetTickCount64();
            if (due > now)
            {
                timeout = std::min<DWORD>(timeout, static_cast<DWORD>(due - now));
                continue;
            }
            // The whole folder, or only the folders that got something new, outermost first (one inside another
            // is scanned with it).
            std::vector<std::string> roots;
            if (w->whole)
                roots.push_back(w->folder);
            else
            {
                std::wstring outer;
                for (const std::wstring &dir : w->dirs)
                {
                    if (outer.empty() || !Under(dir, outer))
                    {
                        outer = dir;
                        roots.push_back(WideToUtf8(dir));
                    }
                }
            }
            w->dirty = w->whole = false;
            w->dirs.clear();
            w->scanned = GetTickCount64();
            Found found{ w->folder, {} };
            bool cancelled = false;
            for (const std::string &root : roots)
            {
                folder_scan scan = scan_folder(root, &shared->stop, nullptr, &known);
                if ((cancelled = scan.cancelled))
                    break;
                for (scanned_file &s : scan.files)
                {
                    known.insert(s.path);
                    if (!Excluded(Utf8ToWide(s.path), excluded))
                        found.files.push_back(std::move(s));
                }
            }
            if (cancelled)
                break;
            std::sort(found.files.begin(), found.files.end(),
                [](const scanned_file &a, const scanned_file &b) { return a.path < b.path; });
            if (found.files.empty())
                continue;
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->found.push_back(std::move(found));
            if (shared->hWnd)
                PostMessage(shared->hWnd, kWatchFound, 0, 0);
        }
        if (!shared->stop)
            WaitForSingleObjectEx(shared->wake, timeout, TRUE);
    }
    for (std::unique_ptr<Watch> &w : watches)
        Close(*w);
}
