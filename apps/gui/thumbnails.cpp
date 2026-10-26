#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "thumbnails.hpp"
#include "book_pages.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <shobjidl.h>
#include <shlguid.h>
#include <thumbcache.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace
{
    // Two threads, so one slow file (a long video on a network drive) doesn't hold up the rest.
    const int kThreadCount = 2;
    // The bitmaps kept in memory, in bytes. Past it, those drawn longest ago go first, but never those the grid
    // wants now.
    const size_t kMemoryBudget = 64 * 1024 * 1024;
    // What a thumbnail costs besides its pixels, roughly, so files without one count too.
    const size_t kEntryCost = 128;
    // Cache files bigger than this aren't ours.
    const LONGLONG kMaxCacheFile = 16 * 1024 * 1024;
    // How long closing the window waits for a thread to finish the thumbnail it is making.
    const int kStopWaitMs = 2000;

    // In FILETIME units (100 ns).
    const std::uint64_t kDay = 24ull * 60 * 60 * 10000000;
    // A cache file is deleted at start once no thumbnail was read from it for this long, counting only the time
    // Corpus was used. Reading one marks it as used again (its modification time), at most once a day so most
    // starts write nothing.
    const std::uint64_t kUnusedFor = 60 * kDay;
    // Of the wait between two starts, what goes past this is a break, which doesn't count.
    const std::uint64_t kBreakAfter = 7 * kDay;
    // Its modification time is when Corpus last started, and it holds how long all the breaks so far took. Cache
    // files' times are in time used (the clock minus the breaks), so a break never makes them look unused.
    const wchar_t kLastStart[] = L"\\last-start";
    // A temporary file this old was left by a Corpus that crashed while writing it.
    const std::uint64_t kTemporaryFor = kDay;

    std::uint64_t TimeValue(const FILETIME &time)
    {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    }

    std::uint64_t Now()
    {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        return TimeValue(now);
    }

    bool EndsWith(const wchar_t *name, const wchar_t *suffix)
    {
        size_t length = wcslen(name);
        size_t suffixLength = wcslen(suffix);
        return length >= suffixLength && _wcsicmp(name + length - suffixLength, suffix) == 0;
    }

    // A cache file: this header, the file's path, then the thumbnail as a PNG (nothing if the file has none).
    struct CacheHeader
    {
        char magic[4];
        std::uint32_t size;        // the square the thumbnail was made to fit
        std::uint64_t fileSize;    // the file it was made from, when it was made
        std::uint64_t writeTime;
        std::uint32_t pathLength;  // in UTF-16 units
        std::uint32_t width;       // 0: the file has no thumbnail
        std::uint32_t height;
        std::uint32_t flags;       // kLinkIcons, kBookCovers
    };
    const char kMagic[4] = { 'C', 'T', 'H', '1' };
    // Written by a Corpus that shows a shortcut's icon. Older ones wrote down that shortcuts have no thumbnail.
    const std::uint32_t kLinkIcons = 1;
    // Written by a Corpus that makes covers for books. Older ones wrote down that most books have no thumbnail.
    const std::uint32_t kBookCovers = 2;

    // A shortcut (.lnk, and the like: web links, ClickOnce apps, DOS program settings) has no thumbnail, so it shows
    // the icon Explorer gives it, usually its program's.
    bool IsShortcut(const std::wstring &path)
    {
        for (const wchar_t *extension : { L".lnk", L".url", L".appref-ms", L".pif" })
        {
            if (EndsWith(path.c_str(), extension))
                return true;
        }
        return false;
    }

    // A .lnk shows its target's icon, which changes when that program is updated or uninstalled: the shortcut's
    // stamp mixes in the target's modification time (this, or 0 once it is gone). Only targets on fixed local drives are
    // looked at, so an offline network drive never holds up the thread.
    std::uint64_t TargetWriteTime(const std::wstring &path)
    {
        if (!EndsWith(path.c_str(), L".lnk"))
            return 0;
        ComPtr<IShellLinkW> link;
        ComPtr<IPersistFile> file;
        wchar_t target[MAX_PATH] = {};
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()))) ||
            FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(), STGM_READ)) ||
            link->GetPath(target, MAX_PATH, nullptr, 0) != S_OK || target[0] == 0 || target[1] != L':')
            return 0;
        const wchar_t root[] = { target[0], L':', L'\\', 0 };
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (GetDriveTypeW(root) != DRIVE_FIXED || !GetFileAttributesExW(target, GetFileExInfoStandard, &attributes))
            return 0;
        return TimeValue(attributes.ftLastWriteTime);
    }

    // Whether Windows failing to make a thumbnail means the file has none: no handler for its type, or one that
    // can't read it. Anything else (a locked file, a handler that timed out) may work later, so it isn't cached.
    // Windows reports files without a handler, and damaged or empty pictures, videos and PDFs alike as
    // WTS_E_FAILEDEXTRACTION; a file open elsewhere without sharing as ERROR_SHARING_VIOLATION.
    bool HasNoThumbnail(HRESULT hr)
    {
        return hr == WTS_E_FAILEDEXTRACTION || hr == WTS_E_EXTRACTIONBLOCKED || hr == E_NOTIMPL || hr == E_NOINTERFACE;
    }

    // Each file has one cache file per size, named after a hash of its path, so a changed file overwrites its
    // old thumbnail instead of leaving it behind.
    std::wstring CacheFile(const std::wstring &folder, const std::wstring &path, UINT size)
    {
        std::uint64_t hash = 14695981039346656037ull;  // FNV-1a
        for (wchar_t c : path)
        {
            hash = (hash ^ (c & 0xFF)) * 1099511628211ull;
            hash = (hash ^ (c >> 8)) * 1099511628211ull;
        }
        wchar_t name[48];
        swprintf(name, 48, L"\\%016llx-%u.thumb", static_cast<unsigned long long>(hash), size);
        return folder + name;
    }

    bool ReadWholeFile(const std::wstring &path, std::vector<BYTE> &data, FILETIME &written)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size;
        DWORD read = 0;
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxCacheFile &&
            GetFileTime(file, nullptr, nullptr, &written);
        if (ok)
        {
            data.resize(static_cast<size_t>(size.QuadPart));
            ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
        }
        CloseHandle(file);
        return ok;
    }

    FILETIME FileTime(std::uint64_t value)
    {
        return { static_cast<DWORD>(value), static_cast<DWORD>(value >> 32) };
    }

    // Writes next to the target and then renames, so a crash or another Corpus never leaves half a file. 'time'
    // becomes its modification time.
    void WriteWholeFile(const std::wstring &path, const std::vector<BYTE> &data, std::uint64_t time)
    {
        std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
        FILETIME modified = FileTime(time);
        SetFileTime(file, nullptr, nullptr, &modified);
        CloseHandle(file);
        if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            DeleteFileW(temporary.c_str());
    }

    // Sets a cache file's modification time to 'now', in time used.
    void SetUsedAt(const std::wstring &path, std::uint64_t now)
    {
        HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        FILETIME time = FileTime(now);
        SetFileTime(file, nullptr, nullptr, &time);
        CloseHandle(file);
    }

    // Marks a cache file as used at 'now', in time used, so it isn't pruned.
    void Touch(const std::wstring &path, const FILETIME &written, std::uint64_t now)
    {
        if (now >= TimeValue(written) + kDay)
            SetUsedAt(path, now);
    }

    // How long all the breaks took, counting the one that ends now, and marks this start.
    std::uint64_t Breaks(const std::wstring &folder, std::uint64_t now)
    {
        HANDLE file = CreateFileW((folder + kLastStart).c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return 0;
        bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        std::uint64_t breaks = 0;
        FILETIME time;
        DWORD done = 0;
        if (existed && GetFileTime(file, nullptr, nullptr, &time))
        {
            // Empty when it was written by a Corpus that didn't count breaks.
            if (!ReadFile(file, &breaks, sizeof(breaks), &done, nullptr) || done != sizeof(breaks))
                breaks = 0;
            std::uint64_t last = TimeValue(time);
            if (now > last && now - last > kBreakAfter)  // the clock may have been set back
                breaks += now - last - kBreakAfter;
        }
        breaks = std::min(breaks, now);  // a damaged stamp
        LARGE_INTEGER start = {};
        if (SetFilePointerEx(file, start, nullptr, FILE_BEGIN))
            WriteFile(file, &breaks, sizeof(breaks), &done, nullptr);
        time = FileTime(now);
        SetFileTime(file, nullptr, nullptr, &time);
        CloseHandle(file);
        return breaks;
    }

    // Deletes a file if it wasn't changed since 'before'. It is opened without sharing, so a thread marking it as
    // used can't do so between the check and the deletion.
    void DeleteIfOlder(const std::wstring &path, std::uint64_t before)
    {
        HANDLE file = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        FILETIME written;
        FILE_DISPOSITION_INFO disposition = { TRUE };
        if (GetFileTime(file, nullptr, nullptr, &written) && TimeValue(written) < before)
            SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        CloseHandle(file);
    }

    // The pixels of a thumbnail from Windows, as premultiplied BGRA rows from the top.
    bool BitmapPixels(HBITMAP bitmap, UINT size, UINT &width, UINT &height, std::vector<BYTE> &pixels)
    {
        BITMAP info;
        if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight == 0)
            return false;
        width = static_cast<UINT>(info.bmWidth);
        height = static_cast<UINT>(info.bmHeight < 0 ? -info.bmHeight : info.bmHeight);
        if (width > 2 * size || height > 2 * size)
            return false;

        BITMAPINFO format = {};
        format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        format.bmiHeader.biWidth = static_cast<LONG>(width);
        format.bmiHeader.biHeight = -static_cast<LONG>(height);  // negative: rows from the top
        format.bmiHeader.biPlanes = 1;
        format.bmiHeader.biBitCount = 32;
        format.bmiHeader.biCompression = BI_RGB;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        HDC dc = CreateCompatibleDC(nullptr);
        int lines = dc ? GetDIBits(dc, bitmap, 0, height, pixels.data(), &format, DIB_RGB_COLORS) : 0;
        if (dc)
            DeleteDC(dc);
        if (lines != static_cast<int>(height))
            return false;

        // Pictures without transparency come with an alpha of 0 everywhere. Those with it are usually
        // premultiplied already; a colour brighter than its alpha says they aren't.
        bool anyAlpha = false;
        bool straight = false;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            BYTE a = pixels[i + 3];
            anyAlpha |= a != 0;
            straight |= pixels[i] > a || pixels[i + 1] > a || pixels[i + 2] > a;
        }
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            BYTE &a = pixels[i + 3];
            if (!anyAlpha)
                a = 255;
            else if (straight)
            {
                for (int c = 0; c < 3; c++)
                    pixels[i + c] = static_cast<BYTE>((pixels[i + c] * a + 127) / 255);
            }
        }
        return true;
    }

    bool EncodePng(IWICImagingFactory *wic, UINT width, UINT height, std::vector<BYTE> &pixels, std::vector<BYTE> &png)
    {
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICFormatConverter> converter;
        ComPtr<IStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;  // PNG keeps colours unpremultiplied
        if (FAILED(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
                static_cast<UINT>(pixels.size()), pixels.data(), bitmap.GetAddressOf())) ||
            FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
            FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) ||
            FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) ||
            FAILED(frame->Initialize(nullptr)) ||
            FAILED(frame->SetSize(width, height)) ||
            FAILED(frame->SetPixelFormat(&format)) ||
            FAILED(frame->WriteSource(converter.Get(), nullptr)) ||
            FAILED(frame->Commit()) ||
            FAILED(encoder->Commit()))
            return false;

        STATSTG stat;
        LARGE_INTEGER start = {};
        ULONG read = 0;
        if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > kMaxCacheFile ||
            FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)))
            return false;
        png.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        return SUCCEEDED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) && read == png.size();
    }

    bool DecodePng(IWICImagingFactory *wic, const BYTE *data, size_t size, UINT width, UINT height, std::vector<BYTE> &pixels)
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        UINT decodedWidth = 0;
        UINT decodedHeight = 0;
        if (FAILED(wic->CreateStream(stream.GetAddressOf())) ||
            FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size))) ||
            FAILED(wic->CreateDecoder(GUID_ContainerFormatPng, nullptr, decoder.GetAddressOf())) ||
            FAILED(decoder->Initialize(stream.Get(), WICDecodeMetadataCacheOnDemand)) ||
            FAILED(decoder->GetFrame(0, frame.GetAddressOf())) ||
            FAILED(frame->GetSize(&decodedWidth, &decodedHeight)) ||
            decodedWidth != width || decodedHeight != height ||
            FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom)))
            return false;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    }
}

Thumbnails::~Thumbnails()
{
    Stop();
}

void Thumbnails::Start(HWND hWnd, const std::wstring &folder)
{
    m_shared->hWnd = hWnd;
    m_shared->folder = folder;
    std::error_code ec;
    if (!folder.empty() && !std::filesystem::create_directories(folder, ec) && ec)
        m_shared->folder.clear();
    if (!m_shared->folder.empty())
        m_shared->breaks = Breaks(m_shared->folder, Now());

    for (int i = 0; i < kThreadCount; i++)
    {
        {
            std::lock_guard<std::mutex> lock(m_shared->mutex);
            m_shared->running++;
        }
        m_threads.emplace_back(&Thumbnails::Run, m_shared);
    }
    if (!m_shared->folder.empty())
    {
        {
            std::lock_guard<std::mutex> lock(m_shared->mutex);
            m_shared->running++;
        }
        m_threads.emplace_back(&Thumbnails::Prune, m_shared);
    }
}

void Thumbnails::Stop()
{
    if (m_threads.empty())
        return;

    // A thumbnail handler can hang (a file on a drive that stopped answering). The threads still busy after the
    // wait are let go: they end on their own, or with the program.
    std::unique_lock<std::mutex> lock(m_shared->mutex);
    m_shared->stop = true;
    m_shared->queue.clear();
    m_shared->wake.notify_all();
    bool ended = m_shared->done.wait_for(lock, std::chrono::milliseconds(kStopWaitMs),
        [this] { return m_shared->running == 0; });
    lock.unlock();
    for (std::thread &thread : m_threads)
    {
        if (ended)
            thread.join();
        else
            thread.detach();
    }
    m_threads.clear();
}

void Thumbnails::BeginPaint(Renderer &renderer, UINT size)
{
    m_paint++;
    if (size == m_shared->size && renderer.Generation() == m_generation)
        return;

    // Thumbnails of another size are made again; so are the bitmaps of a render target that is gone (the disk
    // cache makes that quick).
    m_generation = renderer.Generation();
    m_entries.clear();
    m_bytes = 0;
    m_wanted.clear();
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->size = size;
    m_shared->queue.clear();
    m_shared->finished.clear();
}

void Thumbnails::Want(const std::vector<std::string> &paths)
{
    m_want++;
    std::vector<std::string> wanted;
    std::vector<Request> requests;
    for (const std::string &path : paths)
    {
        auto it = m_entries.find(path);
        if (it != m_entries.end())
            it->second.wantedIn = m_want;
        if (it != m_entries.end() && it->second.epoch == m_epoch)
            continue;
        Request request;
        request.path = path;
        // A thumbnail that failed for now is asked for again even if the file didn't change.
        if (it != m_entries.end() && !it->second.retry)
        {
            request.known = it->second.stamp;
            request.hasKnown = true;
        }
        wanted.push_back(path);
        requests.push_back(std::move(request));
    }

    // Repaints that want the same keep the threads' list as it is.
    if (wanted == m_wanted)
        return;
    m_wanted = std::move(wanted);
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->queue = std::move(requests);
    }
    m_shared->wake.notify_all();
}

ID2D1Bitmap *Thumbnails::Get(Renderer &renderer, const std::string &path, float &level)
{
    level = 0.0f;
    auto it = m_entries.find(path);
    if (it == m_entries.end() || it->second.none)
        return nullptr;

    Entry &entry = it->second;
    if (!entry.bitmap)
    {
        entry.bitmap = renderer.CreateBitmap(entry.width, entry.height, entry.pixels.data());
        if (!entry.bitmap)
            return nullptr;
        std::vector<BYTE>().swap(entry.pixels);
        entry.shownAt = AnimationsEnabled() ? GetTickCount64() : 0;
    }
    entry.usedAt = m_paint;

    ULONGLONG elapsed = GetTickCount64() - entry.shownAt;
    level = entry.shownAt == 0 || elapsed >= theme::kThumbnailFadeMs ? 1.0f : static_cast<float>(elapsed) / theme::kThumbnailFadeMs;
    return entry.bitmap.Get();
}

bool Thumbnails::TakeFinished()
{
    std::vector<Result> finished;
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        finished.swap(m_shared->finished);
    }

    for (Result &result : finished)
    {
        // Made for the old size. The grid may still want this file at the new size, and the threads skipped it
        // while this one was being made, so it is asked for again.
        if (result.size != m_shared->size)
        {
            m_wanted.clear();
            continue;
        }

        if (result.outcome == Outcome::kUnchanged)
        {
            auto it = m_entries.find(result.path);
            if (it != m_entries.end())
                it->second.epoch = m_epoch;
            continue;
        }

        auto it = m_entries.find(result.path);
        if (it == m_entries.end())
            it = m_entries.emplace(result.path, Entry()).first;
        else
            m_bytes -= Cost(it->first, it->second);
        Entry &entry = it->second;
        entry = Entry();
        entry.stamp = result.stamp;
        entry.retry = result.retry;
        entry.epoch = m_epoch;
        entry.wantedIn = m_want;  // most likely still wanted; the next Want says
        if (result.outcome == Outcome::kNone)
            entry.none = true;
        else
        {
            entry.width = result.width;
            entry.height = result.height;
            entry.pixels = std::move(result.pixels);
        }
        m_bytes += Cost(it->first, entry);
    }

    Evict();
    return !finished.empty();
}

size_t Thumbnails::Cost(const std::string &path, const Entry &entry)
{
    return static_cast<size_t>(entry.width) * entry.height * 4 + path.size() + kEntryCost;
}

// Forgets the thumbnails drawn longest ago until there is room again. Those the grid wants now (on screen and next
// to it) stay, even past the budget: forgetting them would only have them made again at once.
void Thumbnails::Evict()
{
    if (m_bytes <= kMemoryBudget)
        return;

    std::vector<std::pair<unsigned long long, const std::string*>> byAge;
    for (const auto &item : m_entries)
    {
        if (item.second.wantedIn != m_want && item.second.usedAt != m_paint)
            byAge.emplace_back(item.second.usedAt, &item.first);
    }
    std::sort(byAge.begin(), byAge.end());

    std::vector<std::string> gone;
    size_t bytes = m_bytes;
    for (const auto &item : byAge)
    {
        if (bytes <= kMemoryBudget / 4 * 3)
            break;
        bytes -= Cost(*item.second, m_entries.at(*item.second));
        gone.push_back(*item.second);
    }
    if (gone.empty())
        return;
    for (const std::string &path : gone)
        m_entries.erase(path);
    m_bytes = bytes;
    m_wanted.clear();  // the files may be on screen again soon, and wanted again
}

// ---- The threads ----

void Thumbnails::Run(std::shared_ptr<Shared> shared)
{
    // Thumbnail handlers are COM objects, and most expect a single-threaded apartment.
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    for (;;)
    {
        Request request;
        UINT size;
        {
            std::unique_lock<std::mutex> lock(shared->mutex);
            shared->wake.wait(lock, [&] { return shared->stop || !shared->queue.empty(); });
            if (shared->stop)
                break;
            request = std::move(shared->queue.front());
            shared->queue.erase(shared->queue.begin());
            size = shared->size;

            // Another thread has it, or has just made it and the window hasn't taken it yet.
            const std::string &path = request.path;
            if (shared->working.count(path) ||
                std::any_of(shared->finished.begin(), shared->finished.end(), [&](const Result &r) { return r.path == path; }))
                continue;
            shared->working.insert(path);
        }

        Result result = Make(shared->folder, shared->breaks, wic.Get(), request, size);

        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->working.erase(request.path);
        if (shared->stop)
            break;
        // One message for everything finished before the window gets to it.
        if (shared->finished.empty())
            PostMessage(shared->hWnd, kThumbnailsReady, 0, 0);
        shared->finished.push_back(std::move(result));
    }

    wic.Reset();
    if (com)
        CoUninitialize();
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->running--;
    shared->done.notify_all();
}

// Deletes the cache files no thumbnail was read from for a while, and temporary files left by a crash.
void Thumbnails::Prune(std::shared_ptr<Shared> shared)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    std::uint64_t now = Now();
    std::uint64_t used = now - shared->breaks;
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileExW((shared->folder + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch,
        nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->stop)
                    break;
            }
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::wstring path = shared->folder + L"\\" + found.cFileName;
            bool thumb = EndsWith(found.cFileName, L".thumb");
            // From the future: the clock was wrong (a jump counted as a break), so it would never be pruned.
            if (thumb && TimeValue(found.ftLastWriteTime) > used + kDay)
                SetUsedAt(path, used);
            else if (thumb && used > kUnusedFor)
                DeleteIfOlder(path, used - kUnusedFor);
            else if (EndsWith(found.cFileName, L".tmp") && now > kTemporaryFor)
                DeleteIfOlder(path, now - kTemporaryFor);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }

    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->running--;
    shared->done.notify_all();
}

Thumbnails::Result Thumbnails::Make(const std::wstring &folder, std::uint64_t breaks, IWICImagingFactory *wic,
    const Request &request, UINT size)
{
    Result result;
    result.path = request.path;
    result.size = size;
    result.outcome = Outcome::kNone;

    std::wstring path = Utf8ToWide(request.path);
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return result;  // missing: the type icon, until the file is back
    result.stamp.size = (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
    result.stamp.writeTime = (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
        attributes.ftLastWriteTime.dwLowDateTime;
    if (IsShortcut(path))
        result.stamp.writeTime ^= TargetWriteTime(path);
    if (request.hasKnown && request.known == result.stamp)
    {
        result.outcome = Outcome::kUnchanged;
        return result;
    }

    // From the disk cache, if it was made from the file as it is now.
    std::wstring cacheFile = folder.empty() ? std::wstring() : CacheFile(folder, path, size);
    std::vector<BYTE> data;
    FILETIME written;
    if (wic && !cacheFile.empty() && ReadWholeFile(cacheFile, data, written) && data.size() >= sizeof(CacheHeader))
    {
        CacheHeader header;
        std::memcpy(&header, data.data(), sizeof(header));
        size_t pathBytes = static_cast<size_t>(header.pathLength) * sizeof(wchar_t);
        if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 && header.size == size &&
            header.fileSize == result.stamp.size && header.writeTime == result.stamp.writeTime &&
            header.pathLength == path.size() && data.size() >= sizeof(header) + pathBytes &&
            std::memcmp(data.data() + sizeof(header), path.data(), pathBytes) == 0)
        {
            const BYTE *png = data.data() + sizeof(header) + pathBytes;
            size_t pngSize = data.size() - sizeof(header) - pathBytes;
            // Written before Corpus made covers, or showed icons for shortcuts other than .lnk.
            bool newer = HasCover(path) || (IsShortcut(path) && !EndsWith(path.c_str(), L".lnk"));
            bool stale = header.width == 0 && ((IsShortcut(path) && !(header.flags & kLinkIcons)) ||
                (newer && !(header.flags & kBookCovers)));
            if (!stale)
                Touch(cacheFile, written, Now() - breaks);
            if (header.width == 0 && !stale)
                return result;
            // A damaged file could ask for an enormous picture.
            if (header.width != 0 && header.width <= 2 * size && header.height <= 2 * size &&
                DecodePng(wic, png, pngSize, header.width, header.height, result.pixels))
            {
                result.outcome = Outcome::kImage;
                result.width = header.width;
                result.height = header.height;
                return result;
            }
        }
    }

    // A book's cover, which Windows makes for few of them (a PDF with a reader that has a handler, at best).
    DecodedFrame cover;
    if (wic && HasCover(path) && RenderCover(wic, path, size, cover) && cover.width > 0 && cover.width <= 2 * size &&
        cover.height > 0 && cover.height <= 2 * size)
    {
        result.outcome = Outcome::kImage;
        result.width = cover.width;
        result.height = cover.height;
        result.pixels = std::move(cover.pixels);
    }

    // From Windows, which only makes thumbnails of the kinds of files it has a handler for.
    if (result.outcome != Outcome::kImage)
    {
        ComPtr<IShellItemImageFactory> factory;
        HBITMAP bitmap = nullptr;
        HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(factory.GetAddressOf()));
        SIIGBF kind = IsShortcut(path) ? SIIGBF_ICONONLY : SIIGBF_THUMBNAILONLY;
        if (SUCCEEDED(hr))
            hr = factory->GetImage(SIZE{ static_cast<LONG>(size), static_cast<LONG>(size) }, kind, &bitmap);
        if (SUCCEEDED(hr))
        {
            if (BitmapPixels(bitmap, size, result.width, result.height, result.pixels))
                result.outcome = Outcome::kImage;
            DeleteObject(bitmap);
        }
        else if (!HasNoThumbnail(hr))
            result.retry = true;
    }
    if (result.outcome != Outcome::kImage)
    {
        result.width = result.height = 0;
        result.pixels.clear();
    }

    // Files without a thumbnail are written down too, so Windows isn't asked again on every start.
    std::vector<BYTE> png;
    if (!wic || cacheFile.empty() || result.retry ||
        (result.outcome == Outcome::kImage && !EncodePng(wic, result.width, result.height, result.pixels, png)))
        return result;
    CacheHeader header = {};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.size = size;
    header.fileSize = result.stamp.size;
    header.writeTime = result.stamp.writeTime;
    header.pathLength = static_cast<std::uint32_t>(path.size());
    header.width = result.width;
    header.height = result.height;
    header.flags = kLinkIcons | kBookCovers;
    data.resize(sizeof(header) + path.size() * sizeof(wchar_t));
    std::memcpy(data.data(), &header, sizeof(header));
    std::memcpy(data.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));
    data.insert(data.end(), png.begin(), png.end());
    WriteWholeFile(cacheFile, data, Now() - breaks);
    return result;
}
