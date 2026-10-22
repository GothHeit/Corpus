#ifndef THUMBNAILS_HPP
#define THUMBNAILS_HPP

#include <windows.h>
#include <wincodec.h>
#include "renderer.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Posted to the window when thumbnails are ready to be shown; the window hands it to the grid.
const UINT kThumbnailsReady = WM_APP + 2;

// The pictures shown in the tiles, as Explorer makes them (IShellItemImageFactory): photos, video frames, PDF
// pages... Files without one keep their type icon.
//
// Asking Windows for a thumbnail can take a while, so threads of their own do it, in the order the grid wants
// them: the visible tiles first, then the ones next to them. Each thumbnail is also kept on disk, so the next start
// shows it at once. A cached thumbnail is redone when its file's size or modification time changes; the grid checks
// that for the files on screen each time the window becomes active.
//
// Only the window's thread calls these. The bitmaps are made while painting, when the render target exists.
class Thumbnails
{
public:
    Thumbnails() = default;
    Thumbnails(const Thumbnails &) = delete;
    Thumbnails &operator=(const Thumbnails &) = delete;
    ~Thumbnails();

    // Starts the threads. 'folder' is where the thumbnails are kept on disk; they are only kept in memory if it
    // is empty or can't be made.
    void Start(HWND hWnd, const std::wstring &folder);
    // Waits for the threads to finish the thumbnail they are making.
    void Stop();

    // Call at the start of each paint, with the side of the square the thumbnails fit in, in pixels. Forgets the
    // bitmaps when that size or the render target changed.
    void BeginPaint(Renderer &renderer, UINT size);
    // The files whose thumbnails are wanted now, most wanted first. Replaces the previous list; files whose
    // thumbnail is known are left out of it.
    void Want(const std::vector<std::string> &paths);
    // The thumbnail of a file, or nullptr if it has none or isn't loaded yet. 'level' says how far it has faded in
    // (0 to 1).
    ID2D1Bitmap *Get(Renderer &renderer, const std::string &path, float &level);
    // Takes what the threads made since the last call. Returns true if there is something new to show.
    bool TakeFinished();
    // Checks again whether the files changed, the next time their thumbnails are wanted.
    void Revalidate() { m_epoch++; }

private:
    // When a file was last changed, to tell whether a thumbnail is still current.
    struct Stamp
    {
        std::uint64_t size = 0;
        std::uint64_t writeTime = 0;
        bool operator==(const Stamp &other) const { return size == other.size && writeTime == other.writeTime; }
    };

    struct Request
    {
        std::string path;
        Stamp known;      // what the thumbnail shown now was made from
        bool hasKnown = false;
    };

    enum class Outcome { kImage, kNone, kUnchanged };

    struct Result
    {
        std::string path;
        UINT size = 0;
        Outcome outcome = Outcome::kNone;
        Stamp stamp;
        UINT width = 0;
        UINT height = 0;
        std::vector<BYTE> pixels;  // premultiplied BGRA
    };

    struct Entry
    {
        ComPtr<ID2D1Bitmap> bitmap;
        std::vector<BYTE> pixels;  // waiting to become the bitmap at the next paint
        UINT width = 0;
        UINT height = 0;
        bool none = false;         // the file has no thumbnail
        Stamp stamp;
        unsigned epoch = 0;        // the Revalidate it was checked in
        ULONGLONG shownAt = 0;     // when the bitmap was first drawn, for its fade
        unsigned long long usedAt = 0;  // the paint it was last drawn in
    };

    void Run();
    Result Make(IWICImagingFactory *wic, const Request &request, UINT size);
    void Evict();

    // Shared with the threads, under m_mutex.
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<Request> m_queue;        // most wanted first
    std::unordered_set<std::string> m_working;  // being made right now
    std::vector<Result> m_finished;
    UINT m_size = 0;
    bool m_stop = false;

    std::vector<std::thread> m_threads;
    HWND m_hWnd = nullptr;
    std::wstring m_folder;

    // The window's thread only.
    std::unordered_map<std::string, Entry> m_entries;
    std::vector<std::string> m_wanted;   // the paths last passed to the threads
    unsigned m_generation = 0;
    unsigned m_epoch = 1;
    unsigned long long m_paint = 0;
    size_t m_bytes = 0;                  // of the bitmaps and pixels in m_entries
};

#endif
