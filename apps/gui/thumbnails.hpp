#ifndef THUMBNAILS_HPP
#define THUMBNAILS_HPP

#include <windows.h>
#include <wincodec.h>
#include "renderer.hpp"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Posted to the window when thumbnails are ready to be shown; the window hands it to the grid.
const UINT kThumbnailsReady = WM_APP + 2;

// The pictures shown in the tiles, as Explorer makes them (IShellItemImageFactory): photos, video frames, PDF
// pages... Books get their covers from Corpus's own readers (RenderCover): a PDF's first page, a comic book's first
// picture, an EPUB's cover picture. Shortcuts (.lnk, .url) show their icon instead, as Explorer draws it: usually
// their program's. Other files without a thumbnail keep their type icon.
//
// Worker threads make them, visible tiles first, and cache each on disk. A cached thumbnail is redone when its
// file's size or modification time changes, checked for the files on screen whenever the window becomes active.
// Cache files unused for a couple of months of using Corpus are deleted at start; a long break doesn't count.
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
    // Waits for the threads to finish the thumbnail they are making, for a while: one stuck in a thumbnail handler
    // is left behind, so the window can still close.
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
    // The monitor's color profile changed: the thumbnails are converted to the new one.
    void ColorsChanged();

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
        bool retry = false;        // kNone for now only: Windows may make it later (a locked file, a timeout)
        unsigned colors = 0;       // Shared::colors when it was made
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
        bool retry = false;        // ...for now: asked for again when the files are checked again
        Stamp stamp;
        unsigned epoch = 0;        // the Revalidate it was checked in
        ULONGLONG shownAt = 0;     // when the bitmap was first drawn, for its fade
        unsigned long long usedAt = 0;  // the paint it was last drawn in
        unsigned long long wantedIn = 0;  // the Want it was last in
    };

    // What the window shares with the threads, under 'mutex'. Each thread holds on to it, so one that is left
    // behind by Stop never uses memory that is gone.
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable done;    // a thread ended
        std::vector<Request> queue;      // most wanted first
        std::unordered_set<std::string> working;  // being made right now
        std::vector<Result> finished;
        UINT size = 0;
        unsigned colors = 0;             // counts the monitor profile changes
        bool stop = false;
        int running = 0;                 // threads that haven't ended
        HWND hWnd = nullptr;             // set before the threads start
        std::wstring folder;
        std::uint64_t breaks = 0;        // how long Corpus went unused, in FILETIME units
    };

    static void Run(std::shared_ptr<Shared> shared);
    static Result Make(const std::wstring &folder, std::uint64_t breaks, IWICImagingFactory *wic,
        const Request &request, UINT size);
    static void Prune(std::shared_ptr<Shared> shared);
    void Evict();
    static size_t Cost(const std::string &path, const Entry &entry);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::vector<std::thread> m_threads;

    // The window's thread only.
    std::unordered_map<std::string, Entry> m_entries;
    std::vector<std::string> m_wanted;   // the paths last passed to the threads
    unsigned m_generation = 0;
    unsigned m_epoch = 1;
    unsigned long long m_paint = 0;
    unsigned long long m_want = 0;
    size_t m_bytes = 0;                  // what m_entries costs
};

#endif
