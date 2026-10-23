#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_pages.hpp"
#include "ui_util.hpp"

namespace
{
    // ".pdf" for C:\Books\novel.PDF, lowercase; empty without one.
    std::string Extension(const std::string &path)
    {
        size_t dot = path.find_last_of('.');
        size_t slash = path.find_last_of("\\/");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return std::string();
        std::string extension = path.substr(dot);
        for (char &c : extension)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return extension;
    }
}

bool IsBook(const std::string &path)
{
    std::string extension = Extension(path);
    return extension == ".pdf" || extension == ".cbz" || extension == ".epub";
}

bool IsReflowable(const std::string &path)
{
    return Extension(path) == ".epub";
}

std::unique_ptr<BookPages> BookPages::Open(IWICImagingFactory *wic, const std::wstring &path)
{
    std::string extension = Extension(WideToUtf8(path));
    if (extension == ".pdf")
        return OpenPdfBook(wic, path);
    if (extension == ".cbz")
        return OpenCbzBook(wic, path);
    if (extension == ".epub")
        return OpenEpubBook(wic, path);
    return nullptr;
}
