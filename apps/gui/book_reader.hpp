#ifndef BOOK_READER_HPP
#define BOOK_READER_HPP

#include <windows.h>
#include "image_decoder.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Posted to the window when the reader has a page for the viewer; the window hands it to the viewer.
const UINT kPageReady = WM_APP + 7;

// Makes the pages of a PDF, a comic book or an EPUB (BookPages) on a thread of its own, so a slow page never
// freezes the window. Once the page asked for is up, the pages on either side of it are made too, the next one in
// the way the reader is going first, so turning a page usually shows the next one at once. A few pages are kept.
//
// An EPUB's pages are laid out to fill the view, there too: again when the view changes size (Resize), staying
// at the same place in the text.
//
// Only the window's thread calls these.
class BookReader
{
public:
    enum class Status
    {
        kNothing,  // nothing new
        kPage,     // the page asked for
        kFailed,   // the file can't be read as a book, or the page can't be made
        kMissing,  // the file isn't at its path
    };

    BookReader() = default;
    BookReader(const BookReader &) = delete;
    BookReader &operator=(const BookReader &) = delete;
    ~BookReader();

    void Start(HWND hWnd);
    // Waits for the page being made, for a while; a thread stuck on a slow drive is left behind.
    void Stop();

    // How big the pages are made. Fixed pages are made 'width' pixels wide (a picture is only scaled down), and
    // at most 'maxSide' on either side. An EPUB's fill a view 'viewWidth' x 'viewHeight' pixels big, at 'scale'
    // times the size of 96 DPI.
    struct Sizes
    {
        UINT width = 0;
        UINT maxSide = 0;
        UINT viewWidth = 0;
        UINT viewHeight = 0;
        float scale = 1.0f;
    };

    // Opens the book at 'path' and asks for the page at 'location' (BookPages::Location), or else page 'page'
    // (the last one if the book is shorter). An empty path closes the book, and lets go of the file.
    void Open(const std::wstring &path, size_t page, const std::string &location, const Sizes &sizes);
    void Close() { Open(L"", 0, std::string(), Sizes()); }
    // The view changed size: an EPUB lays its pages out again, and the page with the text that was shown comes up.
    void Resize(UINT viewWidth, UINT viewHeight, float scale);
    // Asks for another page of the open book. 'again' makes the page again even when it was the last one taken
    // (the bitmap it became was lost).
    void Show(size_t page, bool again = false);
    // Takes what the thread made: the page, its number, how many pages the book has and where the page is in
    // the text (BookPages::Location). After a Resize the number can be another than the one asked for.
    Status Take(DecodedFrame &frame, size_t &page, size_t &count, std::string &location);

private:
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable done;  // the thread ended
        bool stop = false;
        bool running = false;
        HWND hWnd = nullptr;

        unsigned book = 0;   // counts the Opens, so the thread knows when to open another book
        std::wstring path;
        Sizes sizes;
        unsigned resized = 0;  // counts the Resizes
        std::string location;  // where to open the book
        size_t wanted = 0;    // the page asked for
        unsigned asked = 0;   // counts the Shows, so a page asked for again is made again

        Status status = Status::kNothing;
        DecodedFrame frame;
        size_t page = 0;
        size_t count = 0;
        std::string pageLocation;
    };

    static void Run(std::shared_ptr<Shared> shared);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::thread m_thread;
};

#endif
