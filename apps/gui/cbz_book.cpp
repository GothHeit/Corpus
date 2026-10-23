#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_pages.hpp"
#include "zip_folder.hpp"
#include <shlwapi.h>
#include <wincodec.h>
#include <algorithm>
#include <vector>

using Microsoft::WRL::ComPtr;

// A comic book archive is a zip of pictures, one per page, in the order of their names.
namespace
{
    // A page is read whole into memory before it is decoded: a picture inside a zip can't be read out of order.
    const size_t kMaxPageBytes = 256u * 1024 * 1024;

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

    class CbzBook : public BookPages
    {
    public:
        CbzBook(IWICImagingFactory *wic, ZipFolder zip, std::vector<const ZipFolder::Entry *> pages)
            : m_wic(wic), m_zip(std::move(zip)), m_pages(std::move(pages)) {}

        size_t Count() const override { return m_pages.size(); }

        bool Render(size_t index, UINT width, UINT maxSide, DecodedFrame &out) override
        {
            std::vector<BYTE> bytes;
            if (index >= m_pages.size() || !m_zip.Read(*m_pages[index], bytes, kMaxPageBytes))
                return false;
            ComPtr<IStream> memory;
            memory.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
            return memory && DecodePicture(m_wic, memory.Get(), width, maxSide, out);
        }

    private:
        IWICImagingFactory *m_wic;
        ZipFolder m_zip;
        std::vector<const ZipFolder::Entry *> m_pages;  // in m_zip
    };
}

std::unique_ptr<BookPages> OpenCbzBook(IWICImagingFactory *wic, const std::wstring &path)
{
    ZipFolder zip;
    if (!zip.Open(path))
        return nullptr;
    std::vector<const ZipFolder::Entry *> pages;
    for (const ZipFolder::Entry &entry : zip.Entries())
    {
        if (IsPicture(entry.name))
            pages.push_back(&entry);
    }
    if (pages.empty())
        return nullptr;
    // As Explorer sorts names: "page 2" before "page 10".
    std::stable_sort(pages.begin(), pages.end(), [](const ZipFolder::Entry *a, const ZipFolder::Entry *b)
    {
        return StrCmpLogicalW(a->name.c_str(), b->name.c_str()) < 0;
    });
    // The entries stay where they are when the vector moves, so the pointers to them hold.
    return std::unique_ptr<BookPages>(new CbzBook(wic, std::move(zip), std::move(pages)));
}
