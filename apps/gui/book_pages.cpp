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

    // A text, Markdown or HTML file, read as a page of text.
    bool IsText(const std::string &extension)
    {
        return extension == ".txt" || extension == ".md" || extension == ".markdown" || extension == ".html" ||
            extension == ".htm";
    }
}

bool IsBook(const std::string &path)
{
    std::string extension = Extension(path);
    return extension == ".pdf" || extension == ".cbz" || extension == ".epub" || IsText(extension);
}

bool IsReflowable(const std::string &path)
{
    std::string extension = Extension(path);
    return extension == ".epub" || IsText(extension);
}

bool HasCover(const std::wstring &path)
{
    std::string extension = Extension(WideToUtf8(path));
    return extension == ".pdf" || extension == ".cbz" || extension == ".epub";
}

bool RenderCover(IWICImagingFactory *wic, const std::wstring &path, UINT size, DecodedFrame &out)
{
    std::string extension = Extension(WideToUtf8(path));
    if (extension == ".epub")
        return RenderEpubCover(wic, path, size, out);
    if (extension != ".pdf" && extension != ".cbz")
        return false;
    std::unique_ptr<BookPages> book = BookPages::Open(wic, path);
    return book && book->Count() > 0 && book->Render(0, size, size, out);
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
    if (IsText(extension))
        return OpenTextBook(wic, path);
    return nullptr;
}
