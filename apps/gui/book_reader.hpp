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

// Makes the pages of a PDF or a comic book (BookPages) on a thread of its own, so a slow page never freezes the
// window. Once the page asked for is up, the pages on either side of it are made too, the next one in the way
// the reader is going first, so turning a page usually shows the next one at once. A few pages are kept.
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

    // Opens the book at 'path' and asks for page 'page' (the last one if the book is shorter). Pages are made
    // 'width' pixels wide (a picture is only scaled down), and at most 'maxSide' on either side. An empty path
    // closes the book, and lets go of the file.
    void Open(const std::wstring &path, size_t page, UINT width, UINT maxSide);
    void Close() { Open(L"", 0, 0, 0); }
    // Asks for another page of the open book. 'again' makes the page again even when it was the last one taken
    // (the bitmap it became was lost).
    void Show(size_t page, bool again = false);
    // Takes what the thread made: the page, its number and how many pages the book has.
    Status Take(DecodedFrame &frame, size_t &page, size_t &count);

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
        UINT width = 0;
        UINT maxSide = 0;
        size_t wanted = 0;    // the page asked for
        unsigned asked = 0;   // counts the Shows, so a page asked for again is made again

        Status status = Status::kNothing;
        DecodedFrame frame;
        size_t page = 0;
        size_t count = 0;
    };

    static void Run(std::shared_ptr<Shared> shared);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::thread m_thread;
};

#endif
