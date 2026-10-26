#ifndef BOOK_PAGES_HPP
#define BOOK_PAGES_HPP

#include <windows.h>
#include "image_decoder.hpp"
#include <memory>
#include <string>

struct IWICImagingFactory;

// Whether the reader opens the file: a PDF, a comic book archive (.cbz, a zip of pictures), an EPUB, or a text, Markdown
// or HTML file. Only the extension is looked at.
bool IsBook(const std::string &path);
// Whether its pages flow with the size it is read at (an EPUB or a text file), rather than being fixed by the file.
bool IsReflowable(const std::string &path);
// Whether RenderCover makes a cover for it: a PDF, a comic book or an EPUB.
bool HasCover(const std::wstring &path);
// The cover of a book for its thumbnail, at most 'size' pixels on either side: a PDF's first page, a comic book's
// first picture, or the picture an EPUB names as its cover. False when there is none. Any thread with COM started
// (single-threaded apartment).
bool RenderCover(IWICImagingFactory *wic, const std::wstring &path, UINT size, DecodedFrame &out);

// The pages of an open book, made one at a time into pictures for the viewer. A PDF page is drawn by Windows'
// own PDF renderer (Windows.Data.Pdf); a comic book's page is a picture inside the archive, read through the zip
// folders of the Windows shell and decoded by WIC. An EPUB's text is laid out by litehtml into pages the size of
// the view (Layout), and drawn with Direct2D and DirectWrite.
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

    // A reflowable book lays its pages out again to fill a view 'width' x 'height' pixels big, at 'scale' times
    // the size of 96 DPI; Count and the pages change, and Render makes pages that size whatever it is asked.
    // The others have fixed pages and ignore it.
    virtual void Layout(UINT /*width*/, UINT /*height*/, float /*scale*/) {}
    // Where page 'index' begins in the text, kept with the reading progress, so the book opens at the same place
    // once its pages are laid out at another size; empty for fixed pages.
    virtual std::string Location(size_t /*index*/) const { return std::string(); }
    // The page where 'location' (from Location) is now; false when it isn't one of this book's.
    virtual bool PageAt(const std::string & /*location*/, size_t & /*page*/) const { return false; }
};

// BookPages::Open, by format: pdf_book.cpp, cbz_book.cpp, epub_book.cpp and text_book.cpp.
std::unique_ptr<BookPages> OpenPdfBook(IWICImagingFactory *wic, const std::wstring &path);
std::unique_ptr<BookPages> OpenCbzBook(IWICImagingFactory *wic, const std::wstring &path);
std::unique_ptr<BookPages> OpenEpubBook(IWICImagingFactory *wic, const std::wstring &path);
// A text, Markdown or HTML file, turned into a page and paged like an EPUB's chapter.
std::unique_ptr<BookPages> OpenTextBook(IWICImagingFactory *wic, const std::wstring &path);
// A single web page ('html', UTF-8) paged like an EPUB's chapter; 'folder' is where its pictures are looked for.
std::unique_ptr<BookPages> OpenHtmlBook(IWICImagingFactory *wic, std::string html, const std::wstring &folder);
// RenderCover for an EPUB, in epub_book.cpp: its pages are never laid out for it.
bool RenderEpubCover(IWICImagingFactory *wic, const std::wstring &path, UINT size, DecodedFrame &out);

#endif
