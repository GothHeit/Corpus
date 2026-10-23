#ifndef BOOK_PAGES_HPP
#define BOOK_PAGES_HPP

#include <windows.h>
#include "image_decoder.hpp"
#include <memory>
#include <string>

struct IWICImagingFactory;

// Whether the reader opens the file: a PDF, or a comic book archive (.cbz, a zip of pictures). Only the extension
// is looked at.
bool IsBook(const std::string &path);

// The pages of an open book, made one at a time into pictures for the viewer. A PDF page is drawn by Windows'
// own PDF renderer (Windows.Data.Pdf); a comic book's page is a picture inside the archive, read through the zip
// folders of the Windows shell and decoded by WIC.
//
// An object lives on one thread, which has COM started (single-threaded apartment).
class BookPages
{
public:
    // nullptr when the file can't be opened as a book (gone, damaged, locked with a password, no pictures in it).
    static std::unique_ptr<BookPages> Open(IWICImagingFactory *wic, const std::wstring &path);

    virtual ~BookPages() = default;
    virtual size_t Count() const = 0;
    // Makes page 'index' (from 0) 'width' pixels wide, and never more than 'maxSide' on either side. A PDF page is
    // drawn at that size; a picture is only ever scaled down to it.
    virtual bool Render(size_t index, UINT width, UINT maxSide, DecodedFrame &out) = 0;
};

// BookPages::Open, by format: pdf_book.cpp and cbz_book.cpp.
std::unique_ptr<BookPages> OpenPdfBook(IWICImagingFactory *wic, const std::wstring &path);
std::unique_ptr<BookPages> OpenCbzBook(IWICImagingFactory *wic, const std::wstring &path);

#endif
