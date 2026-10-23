#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "book_reader.hpp"
#include "book_pages.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <map>

using Microsoft::WRL::ComPtr;

namespace
{
    // How long closing the window waits for the thread to finish the page it is making.
    const int kStopWaitMs = 2000;
    const size_t kNoPage = static_cast<size_t>(-1);
}

BookReader::~BookReader()
{
    Stop();
}

void BookReader::Start(HWND hWnd)
{
    if (m_thread.joinable())
        return;
    m_shared->hWnd = hWnd;
    m_shared->running = true;
    m_thread = std::thread(&BookReader::Run, m_shared);
}

void BookReader::Stop()
{
    if (!m_thread.joinable())
        return;
    bool finished;
    {
        std::unique_lock<std::mutex> lock(m_shared->mutex);
        m_shared->stop = true;
        m_shared->wake.notify_all();
        finished = m_shared->done.wait_for(lock, std::chrono::milliseconds(kStopWaitMs),
            [this] { return !m_shared->running; });
    }
    if (finished)
        m_thread.join();
    else
        m_thread.detach();
}

void BookReader::Open(const std::wstring &path, size_t page, UINT width, UINT maxSide)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->book++;
    m_shared->path = path;
    m_shared->width = width;
    m_shared->maxSide = maxSide;
    m_shared->wanted = page;
    m_shared->status = Status::kNothing;
    m_shared->frame = DecodedFrame();
    m_shared->wake.notify_all();
}

void BookReader::Show(size_t page, bool again)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    if (page == m_shared->wanted && !again)
        return;
    m_shared->wanted = page;
    m_shared->asked++;
    m_shared->wake.notify_all();
}

BookReader::Status BookReader::Take(DecodedFrame &frame, size_t &page, size_t &count)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    Status status = m_shared->status;
    if (status != Status::kNothing)
    {
        frame = std::move(m_shared->frame);
        m_shared->frame = DecodedFrame();
        m_shared->status = Status::kNothing;
        page = m_shared->page;
        count = m_shared->count;
    }
    return status;
}

void BookReader::Run(std::shared_ptr<Shared> shared)
{
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    std::unique_ptr<BookPages> book;
    unsigned currentBook = 0;
    unsigned currentAsk = 0;
    UINT width = 0;
    UINT maxSide = 0;
    size_t shown = kNoPage;  // the page handed over last
    long long direction = 1;  // the way the reader is going: 1 forward, -1 back
    // The pages made, by number: the one shown and those around it.
    std::map<size_t, DecodedFrame> pages;

    // The page to make ahead next, or kNoPage once the pages around the one shown are all made.
    auto ahead = [&]() -> size_t
    {
        if (!book || shown == kNoPage)
            return kNoPage;
        for (long long step : { direction, -direction, 2 * direction })
        {
            long long page = static_cast<long long>(shown) + step;
            if (page >= 0 && page < static_cast<long long>(book->Count()) && !pages.count(static_cast<size_t>(page)))
                return static_cast<size_t>(page);
        }
        return kNoPage;
    };
    // Lets go of pages that are no longer around the one shown.
    auto forget = [&]()
    {
        for (auto it = pages.begin(); it != pages.end();)
        {
            long long distance = static_cast<long long>(it->first) - static_cast<long long>(shown);
            bool keep = distance == 0 || distance == direction || distance == -direction || distance == 2 * direction;
            it = keep ? std::next(it) : pages.erase(it);
        }
    };

    for (;;)
    {
        bool openBook = false;
        bool showPage = false;
        size_t wanted = 0;
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(shared->mutex);
            shared->wake.wait(lock, [&]
            {
                return shared->stop || shared->book != currentBook || (book && shared->asked != currentAsk) ||
                    ahead() != kNoPage;
            });
            if (shared->stop)
                break;
            if (shared->book != currentBook)
            {
                openBook = showPage = true;
                currentBook = shared->book;
                path = shared->path;
                width = shared->width;
                maxSide = shared->maxSide;
            }
            else if (shared->asked != currentAsk)
                showPage = true;
            currentAsk = shared->asked;
            wanted = shared->wanted;
        }

        Status status = Status::kPage;
        DecodedFrame frame;
        if (openBook)
        {
            book.reset();
            pages.clear();
            shown = kNoPage;
            direction = 1;
            if (path.empty())
                continue;
            if (wic)
                book = BookPages::Open(wic.Get(), path);
            if (!book)
                status = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? Status::kMissing : Status::kFailed;
        }

        if (!showPage)
        {
            // Nothing asked for: the next page ahead.
            size_t page = ahead();
            DecodedFrame made;
            if (page != kNoPage && book->Render(page, width, maxSide, made))
                pages[page] = std::move(made);
            else if (page != kNoPage)
                pages[page] = DecodedFrame();  // can't be made: not tried again while it stays around
            continue;
        }

        if (book)
        {
            if (wanted >= book->Count())
                wanted = book->Count() - 1;
            if (shown != kNoPage && wanted != shown)
                direction = wanted > shown ? 1 : -1;
            auto made = pages.find(wanted);
            if (made != pages.end() && made->second.width > 0)
                frame = made->second;
            else if (book->Render(wanted, width, maxSide, frame))
                pages[wanted] = frame;
            else
                status = Status::kFailed;
            shown = wanted;
            forget();
        }

        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->stop)
            break;
        // Something else was asked for meanwhile: this one isn't wanted anymore.
        if (shared->book != currentBook || shared->asked != currentAsk)
            continue;
        shared->status = status;
        shared->frame = std::move(frame);
        shared->page = wanted;
        shared->count = book ? book->Count() : 0;
        PostMessage(shared->hWnd, kPageReady, 0, 0);
    }

    pages.clear();
    book.reset();
    wic.Reset();
    if (com)
        CoUninitialize();
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->running = false;
    shared->done.notify_all();
}
