#ifndef IMAGE_DECODER_HPP
#define IMAGE_DECODER_HPP

#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct IWICImagingFactory;

// Posted to the window when the decoder has something for the viewer; the window hands it to the viewer.
const UINT kImageDecoded = WM_APP + 3;

// Whether some WIC decoder takes the file's extension (HEIC, AVIF and raw need their Store extensions). Only the
// window's thread calls it.
bool IsDecodableImage(const std::string &path);

// Whether the file is a Photoshop document (.psd, or .psb for the large ones), which Corpus reads itself.
bool IsPsd(const std::string &path);

// One picture, ready to become a bitmap: premultiplied BGRA, 4 bytes per pixel, rows top to bottom.
struct DecodedFrame
{
    UINT width = 0;
    UINT height = 0;
    // The picture's own size, upright. Bigger than width x height when it had to be scaled down to fit in memory.
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    std::vector<BYTE> pixels;
    std::wstring note;  // a few words on why this is not the whole picture, drawn over it ("Preview only: ...")
    UINT delayMs = 0;  // how long an animation's frame stays up; 0 for a still picture
    bool last = false;  // the frame an animation ends on: it stays up as a still picture
    // Scaled down from a file Windows can decode again by region: the viewer can fetch its detail as tiles
    // (TileDecoder) when zoomed in.
    bool tiled = false;
};

// ---- Tiles ----
//
// A picture too big for one bitmap is shown from a fitted overview (DecodedFrame), and when zoomed in past it from
// tiles of kTileSize pixels cut from a level: level k is the picture scaled by 1/2^k, upright, k = 0 being its own
// size.
const UINT kTileSize = 512;

inline UINT LevelSize(UINT source, int level)
{
    return std::max(1u, (source + (1u << level) - 1) >> level);
}

struct TileKey
{
    int level = 0;
    int col = 0;
    int row = 0;
    bool operator==(const TileKey &o) const { return level == o.level && col == o.col && row == o.row; }
};

struct DecodedTile
{
    TileKey key;
    UINT width = 0;
    UINT height = 0;
    std::vector<BYTE> pixels;  // premultiplied BGRA, like DecodedFrame
};

// Decodes tiles of one picture on a thread of its own, reading the file again only when the first is asked for. The
// tiles wanted are decoded a row of them at a time (one pass over the file, as most decoders can't seek), the ones
// nearest the front of the list first.
//
// Only the window's thread calls these.
class TileDecoder
{
public:
    TileDecoder() = default;
    TileDecoder(const TileDecoder &) = delete;
    TileDecoder &operator=(const TileDecoder &) = delete;
    ~TileDecoder();

    void Start(HWND hWnd);
    void Stop();

    // A new picture, of this size upright; drops what was queued and made. Close() when none.
    void Open(const std::wstring &path, UINT sourceWidth, UINT sourceHeight);
    void Close() { Open(L"", 0, 0); }
    // Replaces the tiles wanted, most wanted first.
    void Want(const std::vector<TileKey> &keys);
    // Moves the finished tiles of the picture opened last to 'tiles'. Announced with kImageDecoded.
    void Take(std::vector<DecodedTile> &tiles);

private:
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable done;
        bool stop = false;
        bool running = false;
        HWND hWnd = nullptr;

        unsigned request = 0;
        std::wstring path;
        UINT width = 0;
        UINT height = 0;
        std::vector<TileKey> wanted;
        std::vector<TileKey> inFlight;
        std::vector<DecodedTile> ready;
    };

    static void Run(std::shared_ptr<Shared> shared);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::thread m_thread;
};

// Decodes the first picture in 'stream', put upright and scaled down to at most 'maxWidth' wide and 'maxSide' on
// either side: a page of a comic book, or one Windows drew from a PDF. Any thread with COM started.
bool DecodePicture(IWICImagingFactory *wic, IStream *stream, UINT maxWidth, UINT maxSide, DecodedFrame &out);

// A Photoshop document for the viewer: its merged image, or when it has none that can be read, the thumbnail inside it
// with a note saying so. False when neither works. Any thread with COM started.
bool DecodePsd(IWICImagingFactory *wic, const std::string &path, UINT maxSide, DecodedFrame &out);

// A Photoshop document for a tile of 'size' pixels: the thumbnail inside it if that is big enough, else the merged
// image scaled down, else the small thumbnail. Any thread with COM started.
bool DecodePsdThumbnail(IWICImagingFactory *wic, const std::string &path, UINT size, DecodedFrame &out);

// Decodes pictures for the viewer on a thread of its own, turned upright. An animation is decoded one frame ahead
// of the one shown, so it costs two frames of memory rather than all of them.
//
// Only the window's thread calls these.
class ImageDecoder
{
public:
    enum class Status
    {
        kNothing,  // nothing new
        kFrame,    // the picture, or the next frame of an animation
        kFailed,   // Windows can't decode the file
        kMissing,  // the file isn't at its path
    };

    ImageDecoder() = default;
    ImageDecoder(const ImageDecoder &) = delete;
    ImageDecoder &operator=(const ImageDecoder &) = delete;
    ~ImageDecoder();

    void Start(HWND hWnd);
    // Waits for the thread to finish the picture it is decoding, for a while: one stuck on a slow drive is left
    // behind, so the window can still close.
    void Stop();

    // Starts decoding 'path', dropping the picture before. Pictures with a side longer than 'maxSide' are scaled
    // down to fit (Direct2D can't make bigger bitmaps). Each result is announced with kImageDecoded.
    void Open(const std::wstring &path, UINT maxSide);
    // Drops the picture, and the file it holds open.
    void Close() { Open(L"", 0); }
    // Takes what the thread made for the last picture opened.
    Status Take(DecodedFrame &frame);
    // For an animation: asks for the frame after the one taken last.
    void WantNext();

private:
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable done;  // the thread ended
        bool stop = false;
        bool running = false;
        HWND hWnd = nullptr;

        unsigned request = 0;  // counts the Opens, so the thread knows when it has new work
        std::wstring path;
        UINT maxSide = 0;
        bool wantNext = false;

        Status status = Status::kNothing;
        DecodedFrame frame;
    };

    static void Run(std::shared_ptr<Shared> shared);

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::thread m_thread;
};

#endif
