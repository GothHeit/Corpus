#ifndef IMAGE_DECODER_HPP
#define IMAGE_DECODER_HPP

#include <windows.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Posted to the window when the decoder has something for the viewer; the window hands it to the viewer.
const UINT kImageDecoded = WM_APP + 3;

// Whether Windows can show the file as a picture: some decoder of WIC (the Windows Imaging Component) takes its
// extension. That is JPEG, PNG, GIF, BMP, TIFF, ICO and WebP out of the box, and HEIC, AVIF or camera raw files once
// their extensions from the Store are installed. Only the window's thread calls it.
bool IsDecodableImage(const std::string &path);

// One picture, ready to become a bitmap: premultiplied BGRA, 4 bytes per pixel, rows top to bottom.
struct DecodedFrame
{
    UINT width = 0;
    UINT height = 0;
    // The picture's own size, upright. Bigger than width x height when it had to be scaled down to fit in memory.
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    std::vector<BYTE> pixels;
    UINT delayMs = 0;  // how long an animation's frame stays up; 0 for a still picture
    bool last = false;  // the frame an animation ends on: it stays up as a still picture
};

// Decodes pictures for the viewer on a thread of its own, so a big photo never freezes the window. Photos are
// turned the way the camera was held. An animated GIF is decoded one frame at a time, the next one while the
// current one is up, so a long animation costs the memory of two frames rather than all of them.
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
