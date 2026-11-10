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
#include <cstdint>
#include <cwchar>
#include <ctime>

using Microsoft::WRL::ComPtr;

namespace
{
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

    // The bytes of a format held in global memory or in a stream.
    // 'size' (when not -1) is the true length of data held in global memory, which may be rounded up.
    bool Bytes(IDataObject *data, CLIPFORMAT format, std::vector<uint8_t> &out, LONG index = -1, long long size = -1)
    {
        FORMATETC f = Fmt(format, TYMED_HGLOBAL | TYMED_ISTREAM, index);
        STGMEDIUM medium = {};
        if (FAILED(data->GetData(&f, &medium)))
            return false;
        bool ok = false;
        if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal)
        {
            SIZE_T held = GlobalSize(medium.hGlobal);
            SIZE_T length = size >= 0 && static_cast<SIZE_T>(size) <= held ? static_cast<SIZE_T>(size) : held;
            if (const void *p = GlobalLock(medium.hGlobal))
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
            while (SUCCEEDED(medium.pstm->Read(buffer, sizeof(buffer), &got)) && got > 0)
                out.insert(out.end(), buffer, buffer + got);
            ok = !out.empty();
        }
        ReleaseStgMedium(&medium);
        return ok;
    }

    bool IsWeb(const std::string &url)
    {
        return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
    }

    // The page or picture the data came from: the SourceURL: line of the CF_HTML header (browsers write it when a
    // picture is copied), else the dragged URL.
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
                    return Utf8ToWide(url);
            }
        }
        if (Bytes(data, Format(CFSTR_INETURLW), bytes) && bytes.size() >= sizeof(wchar_t))
        {
            std::wstring url(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
            url.resize(wcsnlen(url.c_str(), url.size()));
            if (IsWeb(WideToUtf8(url)))
                return url;
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
        for (int n = 2; GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; n++)
            path = folder + L"\\" + stem + L" (" + std::to_wstring(n) + L")" + ext;
        return path;
    }

    // A file name Windows accepts, from whatever a browser called the dragged data.
    std::wstring CleanName(std::wstring name)
    {
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
        return name;
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

    bool WriteFileBytes(const std::wstring &path, const void *bytes, size_t size)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        bool ok = WriteFile(file, bytes, static_cast<DWORD>(size), &written, nullptr) && written == size;
        CloseHandle(file);
        if (!ok)
            DeleteFileW(path.c_str());
        return ok;
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

    void KeepSource(const std::wstring &path, const std::wstring &url)
    {
        if (url.empty())
            return;
        std::string text = WideToUtf8(url);
        HANDLE stream = CreateFileW((path + L":Corpus.Source").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stream == INVALID_HANDLE_VALUE)
            return;  // not NTFS: the picture is kept, its source is not
        DWORD written = 0;
        WriteFile(stream, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(stream);
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
    std::string Folder(const std::string &libraryFolder)
    {
        std::wstring path = Utf8ToWide(libraryFolder) + L"\\Inbox";
        CreateDirectoryW(path.c_str(), nullptr);
        return WideToUtf8(path);
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

    std::vector<std::string> Capture(IDataObject *data, const std::string &folderUtf8)
    {
        std::vector<std::string> saved;
        if (!data)
            return saved;
        std::wstring folder = Utf8ToWide(folderUtf8);
        std::wstring url = SourceUrl(data);
        auto keep = [&](const std::wstring &path) { saved.push_back(WideToUtf8(path)); };

        if (Has(data, CF_HDROP))
        {
            for (const std::wstring &from : HdropFiles(data))
            {
                DWORD attributes = GetFileAttributesW(from.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                size_t slash = from.find_last_of(L"\\/");
                std::wstring to = FreePath(folder, from.substr(slash == std::wstring::npos ? 0 : slash + 1));
                if (CopyFileW(from.c_str(), to.c_str(), TRUE))
                    keep(to);
            }
            return saved;
        }

        // Dragged file contents: what a browser offers for a picture (name in the descriptor, bytes in the contents).
        std::vector<uint8_t> bytes;
        if (Bytes(data, Format(CFSTR_FILEDESCRIPTORW), bytes) && bytes.size() >= sizeof(UINT))
        {
            const FILEGROUPDESCRIPTORW *group = reinterpret_cast<const FILEGROUPDESCRIPTORW*>(bytes.data());
            UINT count = group->cItems;
            if (bytes.size() < offsetof(FILEGROUPDESCRIPTORW, fgd) + count * sizeof(FILEDESCRIPTORW))
                count = 0;
            for (UINT i = 0; i < count; i++)
            {
                const FILEDESCRIPTORW &item = group->fgd[i];
                std::wstring name = CleanName(item.cFileName);
                std::vector<uint8_t> contents;
                if (name.empty() || (item.dwFlags & FD_ATTRIBUTES && item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    !Bytes(data, Format(CFSTR_FILECONTENTS), contents, static_cast<LONG>(i),
                        (item.dwFlags & FD_FILESIZE) ? static_cast<long long>((static_cast<unsigned long long>(item.nFileSizeHigh) << 32) | item.nFileSizeLow) : -1))
                    continue;
                std::wstring to = FreePath(folder, name);
                if (WriteFileBytes(to, contents.data(), contents.size()))
                {
                    KeepSource(to, url);
                    keep(to);
                }
            }
            if (!saved.empty())
                return saved;
        }

        std::vector<uint8_t> png;
        if (!Bytes(data, Format(L"PNG"), png) || png.empty())
        {
            png.clear();
            std::vector<uint8_t> dib;
            if (!Bytes(data, CF_DIB, dib) || !DibToPng(dib, png))
                return saved;
        }
        std::wstring to = FreePath(folder, StampedName());
        if (WriteFileBytes(to, png.data(), png.size()))
        {
            KeepSource(to, url);
            keep(to);
        }
        return saved;
    }

    std::wstring SourceOf(const std::string &path)
    {
        HANDLE stream = CreateFileW((Utf8ToWide(path) + L":Corpus.Source").c_str(), GENERIC_READ,
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
