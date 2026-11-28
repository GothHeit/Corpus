#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "inbox_capture.hpp"
#include "ui_util.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <cwchar>
#include <ctime>

using Microsoft::WRL::ComPtr;

namespace
{
    // The value under HKCU\Software\Corpus that holds the Inbox folder.
    const wchar_t *kFolderValue = L"InboxFolder";

    CLIPFORMAT Format(const wchar_t *name) { return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name)); }

    FORMATETC Fmt(CLIPFORMAT format, DWORD tymed = TYMED_HGLOBAL, LONG index = -1)
    {
        return FORMATETC{ format, nullptr, DVASPECT_CONTENT, index, tymed };
    }

    bool Has(IDataObject *data, CLIPFORMAT format, DWORD tymed = TYMED_HGLOBAL, LONG index = -1)
    {
        FORMATETC f = Fmt(format, tymed, index);
        return data->QueryGetData(&f) == S_OK;
    }

    // The bytes of a format held in global memory or in a stream, at most inbox::kMostBytes ('tooBig' says when
    // there was more). Only for what is small by nature (names, links, a copied picture): file contents are read by
    // the worker.
    bool Bytes(IDataObject *data, CLIPFORMAT format, std::vector<uint8_t> &out, bool *tooBig = nullptr)
    {
        FORMATETC f = Fmt(format, TYMED_HGLOBAL | TYMED_ISTREAM);
        STGMEDIUM medium = {};
        if (FAILED(data->GetData(&f, &medium)))
            return false;
        bool ok = false, over = false;
        if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal)
        {
            SIZE_T length = GlobalSize(medium.hGlobal);
            if (length > inbox::kMostBytes)
                over = true;
            else if (const void *p = GlobalLock(medium.hGlobal))
            {
                out.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + length);
                GlobalUnlock(medium.hGlobal);
                ok = true;
            }
        }
        else if (medium.tymed == TYMED_ISTREAM && medium.pstm)
        {
            out.clear();
            uint8_t buffer[64 * 1024];
            ULONG got = 0;
            LARGE_INTEGER zero = {};
            medium.pstm->Seek(zero, STREAM_SEEK_SET, nullptr);
            while (SUCCEEDED(medium.pstm->Read(buffer, sizeof(buffer), &got)) && got > 0 && !over)
            {
                over = out.size() + got > inbox::kMostBytes;
                if (!over)
                    out.insert(out.end(), buffer, buffer + got);
            }
            ok = !over && !out.empty();
        }
        ReleaseStgMedium(&medium);
        if (over)
            out.clear();
        if (tooBig)
            *tooBig = over;
        return ok;
    }

    bool IsWeb(const std::string &url)
    {
        return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
    }

    // The page or picture the data came from: the SourceURL: line of the CF_HTML header (browsers write it when a
    // picture is copied), else the dragged URL.
    // A link without control characters, so it can't add lines to the Zone.Identifier it is written into.
    std::wstring Printable(std::wstring url)
    {
        url.erase(std::remove_if(url.begin(), url.end(), [](wchar_t c) { return c < 32 || c == 127; }), url.end());
        return url;
    }

    std::wstring SourceUrl(IDataObject *data)
    {
        std::vector<uint8_t> bytes;
        if (Bytes(data, Format(L"HTML Format"), bytes))
        {
            std::string text(bytes.begin(), bytes.end());
            size_t at = text.find("SourceURL:");
            size_t end = text.find_first_of("\r\n", at == std::string::npos ? 0 : at);
            if (at != std::string::npos && end != std::string::npos)
            {
                std::string url = text.substr(at + 10, end - at - 10);
                if (IsWeb(url))
                    return Printable(Utf8ToWide(url));
            }
        }
        if (Bytes(data, Format(CFSTR_INETURLW), bytes) && bytes.size() >= sizeof(wchar_t))
        {
            std::wstring url(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
            url.resize(wcsnlen(url.c_str(), url.size()));
            if (IsWeb(WideToUtf8(url)))
                return Printable(url);
        }
        return std::wstring();
    }

    // A name for a file in 'folder' that isn't taken: "name.png", "name (2).png", ...
    std::wstring FreePath(const std::wstring &folder, const std::wstring &name)
    {
        size_t dot = name.find_last_of(L'.');
        std::wstring stem = dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
        std::wstring ext = dot == std::wstring::npos || dot == 0 ? L"" : name.substr(dot);
        std::wstring path = folder + L"\\" + name;
        for (int n = 2; GetFileAttributesW(FileWide(path).c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; n++)
            path = folder + L"\\" + stem + L" (" + std::to_wstring(n) + L")" + ext;
        return path;
    }

    // A file name Windows accepts, from whatever a browser called the dragged data: read up to 'most' characters
    // (the descriptor's field need not end in a zero), no folder part or character Windows refuses, not a device name
    // (CON, NUL, COM1, ...), and short enough to leave room for " (2)" and ".part" (the extension is kept).
    std::wstring CleanName(const wchar_t *raw, size_t most)
    {
        std::wstring name(raw, wcsnlen(raw, most));
        size_t slash = name.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            name = name.substr(slash + 1);
        for (wchar_t &c : name)
        {
            if (c < 32 || wcschr(L"<>:\"|?*", c))
                c = L'_';
        }
        while (!name.empty() && (name.back() == L' ' || name.back() == L'.'))
            name.pop_back();
        const size_t kLongest = 180;
        if (name.size() > kLongest)
        {
            size_t dot = name.find_last_of(L'.');
            std::wstring ext = dot != std::wstring::npos && name.size() - dot <= 16 ? name.substr(dot) : L"";
            std::wstring stem = name.substr(0, kLongest - ext.size());
            while (!stem.empty() && (stem.back() == L' ' || stem.back() == L'.'))
                stem.pop_back();
            name = stem + ext;
        }
        std::wstring stem = name.substr(0, name.find(L'.'));
        while (!stem.empty() && stem.back() == L' ')
            stem.pop_back();
        static const wchar_t *const kDevices[] = { L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$" };
        bool device = std::any_of(std::begin(kDevices), std::end(kDevices),
            [&](const wchar_t *d) { return _wcsicmp(stem.c_str(), d) == 0; });
        if (stem.size() == 4 && (_wcsnicmp(stem.c_str(), L"COM", 3) == 0 || _wcsnicmp(stem.c_str(), L"LPT", 3) == 0) &&
            stem[3] >= L'0' && stem[3] <= L'9')
            device = true;
        if (device)
            name = L"_" + name;
        return name;
    }

    // Shortcuts run their target when opened, and Windows doesn't warn for one that came from the internet.
    bool IsShortcut(const std::wstring &name)
    {
        size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return false;
        std::wstring ext = name.substr(dot);
        for (const wchar_t *refused : { L".lnk", L".url", L".pif", L".scf" })
        {
            if (_wcsicmp(ext.c_str(), refused) == 0)
                return true;
        }
        return false;
    }

    std::wstring StampedName()
    {
        std::time_t now = std::time(nullptr);
        std::tm local = {};
        localtime_s(&local, &now);
        wchar_t text[64];
        wcsftime(text, 64, L"Pasted %Y-%m-%d %H.%M.%S.png", &local);
        return text;
    }

    // A bitmap in the clipboard's DIB layout (header, colours, pixels) as a PNG, through WIC: the DIB gets a BMP
    // file header and the BMP decoder reads it, whatever its depth.
    bool DibToPng(const std::vector<uint8_t> &dib, std::vector<uint8_t> &png)
    {
        if (dib.size() < sizeof(BITMAPINFOHEADER))
            return false;
        const BITMAPINFOHEADER *header = reinterpret_cast<const BITMAPINFOHEADER*>(dib.data());
        DWORD colors = header->biClrUsed ? header->biClrUsed : header->biBitCount <= 8 ? (1u << header->biBitCount) : 0;
        DWORD masks = header->biCompression == BI_BITFIELDS && header->biSize == sizeof(BITMAPINFOHEADER) ? 12 : 0;
        DWORD offset = sizeof(BITMAPFILEHEADER) + header->biSize + colors * 4 + masks;
        std::vector<uint8_t> bmp(sizeof(BITMAPFILEHEADER) + dib.size());
        BITMAPFILEHEADER file = {};
        file.bfType = 0x4D42;
        file.bfSize = static_cast<DWORD>(bmp.size());
        file.bfOffBits = offset;
        memcpy(bmp.data(), &file, sizeof(file));
        memcpy(bmp.data() + sizeof(file), dib.data(), dib.size());

        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            return false;
        ComPtr<IWICStream> in;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(factory->CreateStream(&in)) || FAILED(in->InitializeFromMemory(bmp.data(), static_cast<DWORD>(bmp.size()))) ||
            FAILED(factory->CreateDecoderFromStream(in.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)))
            return false;

        ComPtr<IStream> out;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> target;
        ComPtr<IPropertyBag2> options;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &out)) ||
            FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(out.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&target, &options)) || FAILED(target->Initialize(options.Get())) ||
            FAILED(target->WriteSource(frame.Get(), nullptr)) || FAILED(target->Commit()) || FAILED(encoder->Commit()))
            return false;

        STATSTG stat = {};
        HGLOBAL memory = nullptr;
        if (FAILED(out->Stat(&stat, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(out.Get(), &memory)))
            return false;
        const void *p = GlobalLock(memory);
        if (!p)
            return false;
        png.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + stat.cbSize.QuadPart);
        GlobalUnlock(memory);
        return !png.empty();
    }

    // Writes 'text' as the alternate data stream 'stream' (":name") of the file; nothing on a drive without them.
    void WriteStream(const std::wstring &path, const wchar_t *stream, const std::string &text)
    {
        HANDLE file = CreateFileW(FileWide(path + stream).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
    }

    void KeepSource(const std::wstring &path, const std::wstring &url)
    {
        if (url.empty())
            return;
        std::string text = WideToUtf8(url);
        WriteStream(path, L":Corpus.Source", text);  // not NTFS: the picture is kept, its source is not
    }

    // The Mark of the Web, as a browser writes it on a download: Windows then asks before running the file, and
    // Office opens it in Protected View.
    void MarkFromWeb(const std::wstring &path, const std::wstring &url)
    {
        std::string text = "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=" + (url.empty() ? std::string("about:internet") : WideToUtf8(url)) +
            "\r\n";
        WriteStream(path, L":Zone.Identifier", text);
    }

    std::vector<std::wstring> HdropFiles(IDataObject *data)
    {
        std::vector<std::wstring> paths;
        FORMATETC f = Fmt(CF_HDROP);
        STGMEDIUM medium = {};
        if (FAILED(data->GetData(&f, &medium)))
            return paths;
        if (HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal)))
        {
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; i++)
            {
                UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length, L'\0');
                DragQueryFileW(drop, i, &path[0], length + 1);
                paths.push_back(std::move(path));
            }
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        return paths;
    }
}

namespace inbox
{
    static bool s_testLibrary = false;

    void SetTestLibrary(bool test)
    {
        s_testLibrary = test;
    }

    bool Movable()
    {
        return !s_testLibrary;
    }

    std::string Folder(const std::string &libraryFolder)
    {
        // A test library (--library) keeps its Inbox beside it, away from the real one's setting.
        if (!Movable())
        {
            std::wstring own = Utf8ToWide(libraryFolder) + L"\\Inbox";
            CreateDirectoryW(own.c_str(), nullptr);
            return WideToUtf8(own);
        }
        std::wstring path;
        DWORD size = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kFolderValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS &&
            size > sizeof(wchar_t))
        {
            path.resize(size / sizeof(wchar_t));
            if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kFolderValue, RRF_RT_REG_SZ, nullptr, &path[0], &size) == ERROR_SUCCESS)
                path.resize(wcsnlen(path.c_str(), path.size()));
            else
                path.clear();
        }
        if (path.empty())
        {
            // First run of this version: a catalog with an Inbox keeps it where it is (moving it is the user's call, in
            // Settings); a new one gets it in Pictures, out of the library folder.
            std::wstring legacy = Utf8ToWide(libraryFolder) + L"\\Inbox";
            DWORD attributes = GetFileAttributesW(legacy.c_str());
            path = legacy;
            PWSTR pictures = nullptr;
            if ((attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) &&
                SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &pictures)))
                path = std::wstring(pictures) + L"\\Corpus Inbox";
            CoTaskMemFree(pictures);
            RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kFolderValue, REG_SZ, path.c_str(),
                static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
        }
        SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
        return WideToUtf8(path);
    }

    bool SetFolder(const std::string &folder)
    {
        std::wstring path = Utf8ToWide(folder);
        return RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kFolderValue, REG_SZ, path.c_str(),
            static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    bool IsEmpty(const std::string &folder)
    {
        WIN32_FIND_DATAW found = {};
        HANDLE find = FindFirstFileW(FileWide(Utf8ToWide(folder) + L"\\*").c_str(), &found);
        if (find == INVALID_HANDLE_VALUE)
            return true;
        bool empty = true;
        do
        {
            if (wcscmp(found.cFileName, L".") != 0 && wcscmp(found.cFileName, L"..") != 0)
                empty = false;
        } while (empty && FindNextFileW(find, &found));
        FindClose(find);
        return empty;
    }

    bool HasFiles(IDataObject *data)
    {
        return data && Has(data, CF_HDROP);
    }

    std::vector<std::string> Files(IDataObject *data)
    {
        std::vector<std::string> out;
        for (const std::wstring &path : HdropFiles(data))
            out.push_back(WideToUtf8(path));
        return out;
    }

    bool CanCapture(IDataObject *data)
    {
        return data && (Has(data, CF_HDROP) || Has(data, Format(L"PNG")) || Has(data, CF_DIB) ||
            Has(data, Format(CFSTR_FILEDESCRIPTORW)));
    }

    // ---- Worker ----

    // One thing to save: a file to copy, contents in memory (a picture, or dragged contents given in memory), or
    // dragged contents its source keeps as a stream.
    struct Worker::Item
    {
        std::wstring name;             // the name to save under
        std::wstring from;             // a file to copy, or empty
        std::vector<uint8_t> bytes;    // the contents, when held in memory
        bool dib = false;              // 'bytes' is a bitmap, saved as PNG
        IStream *marshaled = nullptr;  // the source's stream, marshaled for the thread (CoMarshalInterThreadInterfaceInStream)
        bool web = false;              // dragged contents: gets the Mark of the Web
        bool source = false;           // keeps the source link (see SourceOf)
    };

    struct Worker::Job
    {
        Outcome outcome;
        std::wstring folder, url;
        std::vector<Item> items;
        std::vector<std::pair<std::wstring, std::wstring>> moves;  // a move back: (where the file is, where it goes)
        ~Job()
        {
            // A job that never ran lets go of the streams it holds.
            for (Item &item : items)
            {
                if (item.marshaled)
                {
                    LARGE_INTEGER zero = {};
                    item.marshaled->Seek(zero, STREAM_SEEK_SET, nullptr);
                    CoReleaseMarshalData(item.marshaled);
                    item.marshaled->Release();
                }
            }
        }
    };

    struct Worker::Shared
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once stopped
        std::deque<std::unique_ptr<Job>> jobs;
        std::vector<Outcome> done;
        bool running = false;  // the thread is on; it ends when the queue is empty
        HANDLE idle = nullptr;  // set while no thread runs
        std::atomic<bool> stop{ false };
        std::atomic<size_t> moved{ 0 }, total{ 0 };
        ULONGLONG posted = 0;  // when progress was last posted (the thread's own)
        ~Shared()
        {
            if (idle)
                CloseHandle(idle);
        }
    };

    namespace
    {
        // Progress of a copy (CopyFileEx, MoveFileWithProgress): stops it when the window closes, and tells the window
        // how a move goes now and then, even through one big file.
        struct CopyWatch
        {
            const std::atomic<bool> *stop = nullptr;  // null: never stops (a move back)
            std::function<void()> tick;
            bool copied = false;  // the routine was called: a copy across drives
        };

        DWORD CALLBACK CopyProgress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE,
            LPVOID data)
        {
            CopyWatch *watch = static_cast<CopyWatch*>(data);
            watch->copied = true;
            if (watch->tick)
                watch->tick();
            return watch->stop && *watch->stop ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
        }

        // Writes the item's contents (bytes or a stream) to 'path', a new file. 0 when done, 1 when over kMostBytes,
        // 2 when it failed or was stopped; nothing is left behind then.
        int WriteContents(const std::wstring &path, const std::vector<uint8_t> &bytes, IStream *stream,
            const std::atomic<bool> &stop)
        {
            HANDLE file = CreateFileW(FileWide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return 2;
            int result = 0;
            DWORD written = 0;
            if (!stream)
            {
                result = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size() ? 0 : 2;
            }
            else
            {
                std::vector<uint8_t> buffer(256 * 1024);
                unsigned long long total = 0;
                LARGE_INTEGER zero = {};
                stream->Seek(zero, STREAM_SEEK_SET, nullptr);
                for (;;)
                {
                    if (stop)
                    {
                        result = 2;
                        break;
                    }
                    ULONG got = 0;
                    HRESULT hr = stream->Read(buffer.data(), static_cast<ULONG>(buffer.size()), &got);
                    if (FAILED(hr))
                    {
                        result = 2;
                        break;
                    }
                    if (got == 0)
                        break;
                    total += got;
                    if (total > kMostBytes)
                    {
                        result = 1;
                        break;
                    }
                    if (!WriteFile(file, buffer.data(), got, &written, nullptr) || written != got)
                    {
                        result = 2;
                        break;
                    }
                }
                if (result == 0 && total == 0)
                    result = 2;
            }
            CloseHandle(file);
            if (result != 0)
                DeleteFileW(FileWide(path).c_str());
            return result;
        }

        // Renames a complete .part file to 'name' in 'folder', a free name if it is taken. The new path, or empty.
        std::wstring Settle(const std::wstring &part, const std::wstring &folder, const std::wstring &name)
        {
            for (int attempt = 0; attempt < 5; attempt++)
            {
                std::wstring to = FreePath(folder, name);
                if (MoveFileExW(FileWide(part).c_str(), FileWide(to).c_str(), 0))
                    return to;
                DWORD error = GetLastError();
                if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
                    break;
            }
            return std::wstring();
        }

        // Moves one file (a copy and a delete across drives). False if it didn't move; a copy whose original can't
        // be deleted is taken back, so a file is never in both places.
        bool MoveOne(const std::wstring &from, const std::wstring &to, CopyWatch *watch)
        {
            // On one drive the move is a rename and the copy routine is never called.
            watch->copied = false;
            if (!MoveFileWithProgressW(FileWide(from).c_str(), FileWide(to).c_str(), CopyProgress, watch,
                    MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
                return false;
            if (watch->copied && GetFileAttributesW(FileWide(from).c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                DeleteFileW(FileWide(to).c_str());
                return false;
            }
            return true;
        }
    }

    Outcome Worker::Capture(HWND hWnd, IDataObject *data, const std::string &folder,
        const std::function<bool(const std::string &)> &known, bool &started)
    {
        started = false;
        std::unique_ptr<Job> job(new Job);
        Outcome &out = job->outcome;
        if (!data)
            return out;
        job->folder = Utf8ToWide(folder);
        job->url = SourceUrl(data);

        if (Has(data, CF_HDROP))
        {
            for (const std::wstring &from : HdropFiles(data))
            {
                DWORD attributes = GetFileAttributesW(FileWide(from).c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (known && known(WideToUtf8(from)))
                {
                    out.known++;
                    continue;
                }
                size_t slash = from.find_last_of(L"\\/");
                Item item;
                item.name = from.substr(slash == std::wstring::npos ? 0 : slash + 1);
                item.from = from;
                job->items.push_back(std::move(item));
            }
        }
        else
        {
            // Dragged file contents: what a browser offers for a picture (name in the descriptor, bytes in the
            // contents). The bytes come now when the source holds them in memory; a stream is read by the thread.
            std::vector<uint8_t> bytes;
            if (Bytes(data, Format(CFSTR_FILEDESCRIPTORW), bytes) && bytes.size() >= sizeof(UINT))
            {
                const FILEGROUPDESCRIPTORW *group = reinterpret_cast<const FILEGROUPDESCRIPTORW*>(bytes.data());
                size_t count = group->cItems;
                if (bytes.size() < offsetof(FILEGROUPDESCRIPTORW, fgd) + count * sizeof(FILEDESCRIPTORW))
                    count = 0;
                for (size_t i = 0; i < count; i++)
                {
                    const FILEDESCRIPTORW &entry = group->fgd[i];
                    if ((entry.dwFlags & FD_ATTRIBUTES) && (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        continue;
                    bool sized = entry.dwFlags & FD_FILESIZE;
                    unsigned long long size = (static_cast<unsigned long long>(entry.nFileSizeHigh) << 32) | entry.nFileSizeLow;
                    Item item;
                    item.name = CleanName(entry.cFileName, MAX_PATH);
                    item.web = item.source = true;
                    if (item.name.empty())
                    {
                        out.failed++;
                        continue;
                    }
                    if (IsShortcut(item.name))
                    {
                        out.refused++;
                        continue;
                    }
                    if (sized && size > kMostBytes)
                    {
                        out.tooBig++;
                        continue;
                    }
                    FORMATETC f = Fmt(Format(CFSTR_FILECONTENTS), TYMED_ISTREAM | TYMED_HGLOBAL, static_cast<LONG>(i));
                    STGMEDIUM medium = {};
                    if (FAILED(data->GetData(&f, &medium)))
                    {
                        out.failed++;
                        continue;
                    }
                    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal)
                    {
                        // Global memory may be rounded up: the descriptor's size is the true one.
                        SIZE_T length = GlobalSize(medium.hGlobal);
                        if (sized && size < length)
                            length = static_cast<SIZE_T>(size);
                        const void *p = length > kMostBytes ? nullptr : GlobalLock(medium.hGlobal);
                        if (length > kMostBytes)
                            out.tooBig++;
                        else if (!p)
                            out.failed++;
                        else
                        {
                            item.bytes.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + length);
                            GlobalUnlock(medium.hGlobal);
                            job->items.push_back(std::move(item));
                        }
                    }
                    else if (medium.tymed == TYMED_ISTREAM && medium.pstm)
                    {
                        STATSTG stat = {};
                        if (SUCCEEDED(medium.pstm->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > kMostBytes)
                            out.tooBig++;
                        else if (SUCCEEDED(CoMarshalInterThreadInterfaceInStream(IID_IStream, medium.pstm, &item.marshaled)))
                            job->items.push_back(std::move(item));
                        else
                            out.failed++;
                    }
                    else
                        out.failed++;
                    ReleaseStgMedium(&medium);
                }
            }

            // Else a picture: PNG as it came, a bitmap re-encoded as PNG by the thread. It stands for contents that
            // were left out, when there is one.
            if (job->items.empty())
            {
                bool over = false;
                Item item;
                item.source = true;
                if (Bytes(data, Format(L"PNG"), bytes, &over) && !bytes.empty())
                    item.bytes.swap(bytes);
                else if (!over && Bytes(data, CF_DIB, bytes, &over))
                {
                    item.bytes.swap(bytes);
                    item.dib = true;
                }
                if (over)
                    out.tooBig++;
                else if (!item.bytes.empty())
                {
                    item.name = StampedName();
                    out.tooBig = out.refused = out.failed = 0;
                    job->items.push_back(std::move(item));
                }
            }
        }

        Outcome left = out;
        if (!job->items.empty())
        {
            started = true;
            m_pending++;
            Queue(hWnd, std::move(job));
        }
        return left;
    }

    void Worker::Move(HWND hWnd, const std::string &from, const std::string &to)
    {
        std::unique_ptr<Job> job(new Job);
        job->outcome.move = true;
        job->outcome.from = from;
        job->outcome.to = to;
        m_pending++;
        m_moves++;
        Queue(hWnd, std::move(job));
    }

    void Worker::MoveBack(HWND hWnd, const std::vector<std::pair<std::string, std::string>> &moved)
    {
        if (moved.empty())
            return;
        std::unique_ptr<Job> job(new Job);
        job->outcome.move = job->outcome.back = true;
        for (const auto &move : moved)
            job->moves.emplace_back(Utf8ToWide(move.second), Utf8ToWide(move.first));
        m_pending++;
        m_moves++;
        Queue(hWnd, std::move(job));
    }

    void Worker::Progress(size_t &done, size_t &total) const
    {
        done = m_shared ? m_shared->moved.load() : 0;
        total = m_shared ? m_shared->total.load() : 0;
    }

    void Worker::Queue(HWND hWnd, std::unique_ptr<Job> job)
    {
        if (!m_shared)
        {
            m_shared = std::make_shared<Shared>();
            m_shared->idle = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        }
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->hWnd = hWnd;
        m_shared->jobs.push_back(std::move(job));
        if (!m_shared->running)
        {
            m_shared->running = true;
            if (m_shared->idle)
                ResetEvent(m_shared->idle);
            // Detached: a source that stops answering mustn't keep the window from closing (see Stop).
            std::thread(Run, m_shared).detach();
        }
    }

    void Worker::Run(std::shared_ptr<Shared> shared)
    {
        // Free threaded: the streams of a drop are called from here, and WIC re-encodes bitmaps.
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;)
        {
            std::unique_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->jobs.empty())
                {
                    // Under the lock: a job queued from now on starts another thread.
                    shared->running = false;
                    if (shared->idle)
                        SetEvent(shared->idle);
                    break;
                }
                job = std::move(shared->jobs.front());
                shared->jobs.pop_front();
            }
            if (job->outcome.move)
                RunMove(*shared, *job);
            else
                RunCapture(*shared, *job);
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->done.push_back(std::move(job->outcome));
            if (shared->hWnd)
                PostMessage(shared->hWnd, kInboxWork, 1, 0);
        }
        if (SUCCEEDED(com))
            CoUninitialize();
    }

    void Worker::RunCapture(Shared &shared, Job &job)
    {
        Outcome &out = job.outcome;
        for (Item &item : job.items)
        {
            IStream *stream = nullptr;
            if (item.marshaled)
            {
                IStream *marshaled = item.marshaled;
                item.marshaled = nullptr;
                if (FAILED(CoGetInterfaceAndReleaseStream(marshaled, IID_IStream, reinterpret_cast<void**>(&stream))))
                    stream = nullptr;
                if (!stream)
                {
                    out.failed++;
                    continue;
                }
            }
            if (shared.stop)
            {
                if (stream)
                    stream->Release();
                continue;
            }
            if (item.dib)
            {
                std::vector<uint8_t> png;
                if (!DibToPng(item.bytes, png))
                {
                    out.failed++;
                    continue;
                }
                item.bytes.swap(png);
            }

            // Under a .part name until complete: the folder watcher leaves those alone.
            std::wstring part = FreePath(job.folder, item.name + L".part");
            int result = 0;
            if (!item.from.empty())
            {
                CopyWatch watch;
                watch.stop = &shared.stop;
                result = CopyFileExW(FileWide(item.from).c_str(), FileWide(part).c_str(), CopyProgress, &watch, nullptr,
                    COPY_FILE_FAIL_IF_EXISTS) ? 0 : 2;
            }
            else
                result = WriteContents(part, item.bytes, stream, shared.stop);
            if (stream)
                stream->Release();
            std::vector<uint8_t>().swap(item.bytes);
            if (result != 0)
            {
                (result == 1 ? out.tooBig : out.failed)++;
                continue;
            }
            if (item.web)
                MarkFromWeb(part, job.url);
            if (item.source)
                KeepSource(part, job.url);
            std::wstring to = Settle(part, job.folder, item.name);
            if (to.empty())
            {
                DeleteFileW(FileWide(part).c_str());
                out.failed++;
                continue;
            }
            out.saved.push_back(WideToUtf8(to));
        }
    }

    void Worker::RunMove(Shared &shared, Job &job)
    {
        Outcome &out = job.outcome;
        bool back = out.back;
        // (where the file is, where it goes); forward, where it goes is only the name: the free name is found when
        // it moves.
        std::vector<std::pair<std::wstring, std::wstring>> todo = job.moves;
        std::wstring target = Utf8ToWide(out.to);
        if (!back)
        {
            // The names first: moving while listing could meet a file twice when 'to' is under 'from'.
            std::wstring source = Utf8ToWide(out.from);
            WIN32_FIND_DATAW found = {};
            HANDLE find = FindFirstFileW(FileWide(source + L"\\*").c_str(), &found);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        todo.emplace_back(source + L"\\" + found.cFileName, found.cFileName);
                } while (FindNextFileW(find, &found));
                FindClose(find);
            }
        }
        shared.moved = 0;
        shared.total = todo.size();
        CopyWatch watch;
        watch.stop = back ? nullptr : &shared.stop;
        watch.tick = [&shared]()
        {
            ULONGLONG now = GetTickCount64();
            if (now - shared.posted < 200)
                return;
            shared.posted = now;
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (shared.hWnd)
                PostMessage(shared.hWnd, kInboxWork, 0, 0);
        };
        std::vector<std::pair<std::wstring, std::wstring>> done;
        for (const auto &move : todo)
        {
            if (!back && shared.stop)
            {
                out.cancelled = true;
                break;
            }
            std::wstring to = back ? move.second : FreePath(target, move.second);
            if (MoveOne(move.first, to, &watch))
                done.emplace_back(move.first, to);
            else if (!back && shared.stop)
            {
                out.cancelled = true;
                break;
            }
            shared.moved++;
            watch.tick();
        }
        // The window closed: what moved goes back, since the catalog will never follow it.
        if (out.cancelled)
        {
            CopyWatch quiet;
            for (auto it = done.rbegin(); it != done.rend(); ++it)
                MoveOne(it->second, it->first, &quiet);
            done.clear();
        }
        for (const auto &move : done)
            out.moved.emplace_back(WideToUtf8(move.first), WideToUtf8(move.second));
    }

    bool Worker::Take(std::vector<Outcome> &done)
    {
        if (!m_shared)
            return false;
        {
            std::lock_guard<std::mutex> lock(m_shared->mutex);
            done = std::move(m_shared->done);
            m_shared->done.clear();
        }
        for (const Outcome &outcome : done)
        {
            m_pending--;
            if (outcome.move)
                m_moves--;
        }
        return !done.empty();
    }

    void Worker::Stop()
    {
        if (!m_shared)
            return;
        bool running;
        {
            std::lock_guard<std::mutex> lock(m_shared->mutex);
            m_shared->hWnd = nullptr;
            // A move that ended but that the window never took (a dialog was up): the catalog doesn't follow it, so
            // the files go back. Moves back run to the end even once stopped.
            for (const Outcome &outcome : m_shared->done)
            {
                if (!outcome.move || outcome.back || outcome.moved.empty())
                    continue;
                std::unique_ptr<Job> job(new Job);
                job->outcome.move = job->outcome.back = true;
                for (const auto &move : outcome.moved)
                    job->moves.emplace_back(Utf8ToWide(move.second), Utf8ToWide(move.first));
                m_shared->jobs.push_back(std::move(job));
                m_moves++;
            }
            if (!m_shared->jobs.empty() && !m_shared->running)
            {
                m_shared->running = true;
                if (m_shared->idle)
                    ResetEvent(m_shared->idle);
                std::thread(Run, m_shared).detach();
            }
            running = m_shared->running;
        }
        m_shared->stop = true;
        // Captures stop at once. A move puts back what it moved, which across drives is a copy: that is waited for.
        // The wait serves COM calls into this thread meanwhile (a stream this process offered, being read).
        if (running && m_shared->idle)
        {
            DWORD index = 0;
            while (CoWaitForMultipleHandles(0, 5000, 1, &m_shared->idle, &index) == RPC_S_CALLPENDING && m_moves > 0)
            {
            }
        }
        m_shared.reset();
        m_pending = m_moves = 0;
    }

    std::wstring SourceOf(const std::string &path)
    {
        HANDLE stream = CreateFileW(FileWide(Utf8ToWide(path) + L":Corpus.Source").c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stream == INVALID_HANDLE_VALUE)
            return std::wstring();
        char buffer[2048];
        DWORD got = 0;
        bool ok = ReadFile(stream, buffer, sizeof(buffer), &got, nullptr);
        CloseHandle(stream);
        return ok ? Utf8ToWide(std::string(buffer, got)) : std::wstring();
    }

    // ---- DropTarget ----

    STDMETHODIMP DropTarget::QueryInterface(REFIID riid, void **object)
    {
        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP DropTarget::DragEnter(IDataObject *data, DWORD, POINTL pt, DWORD *effect)
    {
        m_data = data;
        return DragOver(0, pt, effect);
    }

    STDMETHODIMP DropTarget::DragOver(DWORD, POINTL pt, DWORD *effect)
    {
        *effect = m_data && m_wants(m_data, POINT{ pt.x, pt.y }) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHODIMP DropTarget::DragLeave()
    {
        m_data = nullptr;
        if (m_leave)
            m_leave();
        return S_OK;
    }

    STDMETHODIMP DropTarget::Drop(IDataObject *data, DWORD, POINTL pt, DWORD *effect)
    {
        m_data = nullptr;
        if (m_leave)
            m_leave();
        POINT screen{ pt.x, pt.y };
        bool wanted = m_wants(data, screen);
        *effect = wanted ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (wanted)
            m_drop(data, screen);
        return S_OK;
    }
}
