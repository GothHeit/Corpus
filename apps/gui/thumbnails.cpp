#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "thumbnails.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <shobjidl.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace
{
    // Two threads, so one slow file (a long video on a network drive) doesn't hold up the rest.
    const int kThreadCount = 2;
    // The bitmaps kept in memory, in bytes. Past it, those drawn longest ago go first.
    const size_t kMemoryBudget = 64 * 1024 * 1024;
    // Cache files bigger than this aren't ours.
    const LONGLONG kMaxCacheFile = 16 * 1024 * 1024;

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
        std::uint32_t reserved;
    };
    const char kMagic[4] = { 'C', 'T', 'H', '1' };

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

    bool ReadWholeFile(const std::wstring &path, std::vector<BYTE> &data)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size;
        DWORD read = 0;
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxCacheFile;
        if (ok)
        {
            data.resize(static_cast<size_t>(size.QuadPart));
            ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
        }
        CloseHandle(file);
        return ok;
    }

    // Writes next to the target and then renames, so a crash or another Corpus never leaves half a file.
    void WriteWholeFile(const std::wstring &path, const std::vector<BYTE> &data)
    {
        std::wstring temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
        CloseHandle(file);
        if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            DeleteFileW(temporary.c_str());
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
    m_hWnd = hWnd;
    m_folder = folder;
    std::error_code ec;
    if (!m_folder.empty() && !std::filesystem::create_directories(m_folder, ec) && ec)
        m_folder.clear();

    for (int i = 0; i < kThreadCount; i++)
        m_threads.emplace_back(&Thumbnails::Run, this);
}

void Thumbnails::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_queue.clear();
    }
    m_wake.notify_all();
    for (std::thread &thread : m_threads)
        thread.join();
    m_threads.clear();
}

void Thumbnails::BeginPaint(Renderer &renderer, UINT size)
{
    m_paint++;
    if (size == m_size && renderer.Generation() == m_generation)
        return;

    // Thumbnails of another size are made again; so are the bitmaps of a render target that is gone (the disk
    // cache makes that quick).
    m_generation = renderer.Generation();
    m_entries.clear();
    m_bytes = 0;
    m_wanted.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_size = size;
    m_queue.clear();
    m_finished.clear();
}

void Thumbnails::Want(const std::vector<std::string> &paths)
{
    std::vector<std::string> wanted;
    std::vector<Request> requests;
    for (const std::string &path : paths)
    {
        auto it = m_entries.find(path);
        if (it != m_entries.end() && it->second.epoch == m_epoch)
            continue;
        Request request;
        request.path = path;
        if (it != m_entries.end())
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
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue = std::move(requests);
    }
    m_wake.notify_all();
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
        std::lock_guard<std::mutex> lock(m_mutex);
        finished.swap(m_finished);
    }

    for (Result &result : finished)
    {
        // Made for the old size. The grid may still want this file at the new size, and the threads skipped it
        // while this one was being made, so it is asked for again.
        if (result.size != m_size)
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

        Entry &entry = m_entries[result.path];
        m_bytes -= static_cast<size_t>(entry.width) * entry.height * 4;
        entry = Entry();
        entry.stamp = result.stamp;
        entry.epoch = m_epoch;
        if (result.outcome == Outcome::kNone)
        {
            entry.none = true;
            continue;
        }
        entry.width = result.width;
        entry.height = result.height;
        entry.pixels = std::move(result.pixels);
        m_bytes += static_cast<size_t>(entry.width) * entry.height * 4;
    }

    Evict();
    return !finished.empty();
}

// Forgets the thumbnails drawn longest ago until there is room again. Those on screen now stay.
void Thumbnails::Evict()
{
    if (m_bytes <= kMemoryBudget)
        return;

    std::vector<std::pair<unsigned long long, const std::string*>> byAge;
    for (const auto &item : m_entries)
    {
        if (item.second.width > 0 && item.second.usedAt != m_paint)
            byAge.emplace_back(item.second.usedAt, &item.first);
    }
    std::sort(byAge.begin(), byAge.end());

    std::vector<std::string> gone;
    size_t bytes = m_bytes;
    for (const auto &item : byAge)
    {
        if (bytes <= kMemoryBudget / 4 * 3)
            break;
        const Entry &entry = m_entries.at(*item.second);
        bytes -= static_cast<size_t>(entry.width) * entry.height * 4;
        gone.push_back(*item.second);
    }
    for (const std::string &path : gone)
        m_entries.erase(path);
    m_bytes = bytes;
    m_wanted.clear();  // the files may be on screen again soon, and wanted again
}

// ---- The threads ----

void Thumbnails::Run()
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
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop)
                break;
            request = std::move(m_queue.front());
            m_queue.erase(m_queue.begin());
            size = m_size;

            // Another thread has it, or has just made it and the window hasn't taken it yet.
            const std::string &path = request.path;
            if (m_working.count(path) ||
                std::any_of(m_finished.begin(), m_finished.end(), [&](const Result &r) { return r.path == path; }))
                continue;
            m_working.insert(path);
        }

        Result result = Make(wic.Get(), request, size);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_working.erase(request.path);
        // One message for everything finished before the window gets to it.
        if (m_finished.empty())
            PostMessage(m_hWnd, kThumbnailsReady, 0, 0);
        m_finished.push_back(std::move(result));
    }

    wic.Reset();
    if (com)
        CoUninitialize();
}

Thumbnails::Result Thumbnails::Make(IWICImagingFactory *wic, const Request &request, UINT size)
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
    if (request.hasKnown && request.known == result.stamp)
    {
        result.outcome = Outcome::kUnchanged;
        return result;
    }

    // From the disk cache, if it was made from the file as it is now.
    std::wstring cacheFile = m_folder.empty() ? std::wstring() : CacheFile(m_folder, path, size);
    std::vector<BYTE> data;
    if (wic && !cacheFile.empty() && ReadWholeFile(cacheFile, data) && data.size() >= sizeof(CacheHeader))
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
            if (header.width == 0)
                return result;
            // A damaged file could ask for an enormous picture.
            if (header.width <= 2 * size && header.height <= 2 * size &&
                DecodePng(wic, png, pngSize, header.width, header.height, result.pixels))
            {
                result.outcome = Outcome::kImage;
                result.width = header.width;
                result.height = header.height;
                return result;
            }
        }
    }

    // From Windows, which only makes thumbnails of the kinds of files it has a handler for.
    ComPtr<IShellItemImageFactory> factory;
    HBITMAP bitmap = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(factory.GetAddressOf()))) &&
        SUCCEEDED(factory->GetImage(SIZE{ static_cast<LONG>(size), static_cast<LONG>(size) }, SIIGBF_THUMBNAILONLY, &bitmap)))
    {
        if (BitmapPixels(bitmap, size, result.width, result.height, result.pixels))
            result.outcome = Outcome::kImage;
        DeleteObject(bitmap);
    }
    if (result.outcome != Outcome::kImage)
    {
        result.width = result.height = 0;
        result.pixels.clear();
    }

    // Files without a thumbnail are written down too, so Windows isn't asked again on every start.
    std::vector<BYTE> png;
    if (!wic || cacheFile.empty() ||
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
    data.resize(sizeof(header) + path.size() * sizeof(wchar_t));
    std::memcpy(data.data(), &header, sizeof(header));
    std::memcpy(data.data() + sizeof(header), path.data(), path.size() * sizeof(wchar_t));
    data.insert(data.end(), png.begin(), png.end());
    WriteWholeFile(cacheFile, data);
    return result;
}
