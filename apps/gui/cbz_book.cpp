#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_pages.hpp"
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

// A comic book archive is a zip of pictures, one per page, in the order of their names. Windows reads zip files as
// folders in Explorer ("compressed folders"), and the same shell folder reads them here: no zip code of our own.
namespace
{
    // The zip folder of the Windows shell (zipfldr.dll).
    const CLSID kCompressedFolder = { 0xE88DCCE0, 0xB7B3, 0x11D1, { 0xA9, 0xF0, 0x00, 0xAA, 0x00, 0x60, 0xFA, 0x31 } };

    // A page is read whole into memory before it is decoded: a picture inside a zip can't be read out of order.
    const ULONG kMaxPageBytes = 256u * 1024 * 1024;

    bool IsPicture(const std::wstring &name)
    {
        static const wchar_t *const kExtensions[] = { L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".gif", L".bmp",
            L".webp", L".avif", L".heic", L".heif", L".tif", L".tiff", L".jxr", L".jxl" };
        const wchar_t *extension = PathFindExtensionW(name.c_str());
        for (const wchar_t *known : kExtensions)
        {
            if (_wcsicmp(extension, known) == 0)
                return true;
        }
        return false;
    }

    struct ChildId
    {
        void operator()(ITEMID_CHILD *id) const { CoTaskMemFree(id); }
    };

    struct Page
    {
        std::wstring name;  // its path inside the archive, which sets the order
        ComPtr<IShellFolder> folder;
        std::unique_ptr<ITEMID_CHILD, ChildId> id;
    };

    std::wstring DisplayName(IShellFolder *folder, PCUITEMID_CHILD id)
    {
        STRRET name = {};
        wchar_t *text = nullptr;
        if (FAILED(folder->GetDisplayNameOf(id, SHGDN_INFOLDER | SHGDN_FORPARSING, &name)) ||
            FAILED(StrRetToStrW(&name, id, &text)))
            return std::wstring();
        std::wstring result = text;
        CoTaskMemFree(text);
        return result;
    }

    // Every picture in 'folder' and the folders inside it. Folders a broken archive loops on stop at 'depth'.
    void ListPages(IShellFolder *folder, const std::wstring &prefix, int depth, std::vector<Page> &pages)
    {
        ComPtr<IEnumIDList> items;
        if (depth > 16 || folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
                items.GetAddressOf()) != S_OK)
            return;
        PITEMID_CHILD id = nullptr;
        while (items->Next(1, &id, nullptr) == S_OK)
        {
            std::unique_ptr<ITEMID_CHILD, ChildId> owned(id);
            std::wstring name = DisplayName(folder, id);
            SFGAOF attributes = SFGAO_FOLDER;
            // Archives zipped on a Mac carry a "__MACOSX" folder of "._name" files next to the pictures: metadata,
            // not pages.
            if (name.empty() || _wcsicmp(name.c_str(), L"__MACOSX") == 0 || name.compare(0, 2, L"._") == 0 ||
                FAILED(folder->GetAttributesOf(1, const_cast<PCUITEMID_CHILD *>(&id), &attributes)))
                continue;
            if (attributes & SFGAO_FOLDER)
            {
                ComPtr<IShellFolder> inner;
                if (SUCCEEDED(folder->BindToObject(id, nullptr, IID_PPV_ARGS(inner.GetAddressOf()))))
                    ListPages(inner.Get(), prefix + name + L"\\", depth + 1, pages);
            }
            else if (IsPicture(name))
                pages.push_back({ prefix + name, folder, std::move(owned) });
        }
    }

    class CbzBook : public BookPages
    {
    public:
        CbzBook(IWICImagingFactory *wic, std::vector<Page> pages) : m_wic(wic), m_pages(std::move(pages)) {}

        size_t Count() const override { return m_pages.size(); }

        bool Render(size_t index, UINT width, UINT maxSide, DecodedFrame &out) override
        {
            if (index >= m_pages.size())
                return false;
            const Page &page = m_pages[index];
            ComPtr<IStream> packed;
            STATSTG stat = {};
            if (FAILED(page.folder->BindToStorage(page.id.get(), nullptr, IID_PPV_ARGS(packed.GetAddressOf()))))
                return false;

            // Read whole, its size known or not.
            std::vector<BYTE> bytes;
            if (SUCCEEDED(packed->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart <= kMaxPageBytes)
                bytes.reserve(static_cast<size_t>(stat.cbSize.QuadPart));
            BYTE buffer[64 * 1024];
            for (;;)
            {
                ULONG read = 0;
                HRESULT hr = packed->Read(buffer, sizeof(buffer), &read);
                if (FAILED(hr) || bytes.size() + read > kMaxPageBytes)
                    return false;
                bytes.insert(bytes.end(), buffer, buffer + read);
                if (hr != S_OK || read == 0)
                    break;
            }
            ComPtr<IStream> memory;
            memory.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
            return memory && DecodePicture(m_wic, memory.Get(), width, maxSide, out);
        }

    private:
        IWICImagingFactory *m_wic;
        std::vector<Page> m_pages;
    };
}

std::unique_ptr<BookPages> OpenCbzBook(IWICImagingFactory *wic, const std::wstring &path)
{
    PIDLIST_ABSOLUTE id = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr)))
        return nullptr;
    ComPtr<IPersistFolder> persist;
    ComPtr<IShellFolder> root;
    HRESULT hr = CoCreateInstance(kCompressedFolder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(persist.GetAddressOf()));
    if (SUCCEEDED(hr))
        hr = persist->Initialize(id);
    CoTaskMemFree(id);
    if (FAILED(hr) || FAILED(persist.As(&root)))
        return nullptr;

    std::vector<Page> pages;
    ListPages(root.Get(), std::wstring(), 0, pages);
    if (pages.empty())
        return nullptr;
    // As Explorer sorts names: "page 2" before "page 10".
    std::stable_sort(pages.begin(), pages.end(), [](const Page &a, const Page &b)
    {
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    });
    return std::unique_ptr<BookPages>(new CbzBook(wic, std::move(pages)));
}
