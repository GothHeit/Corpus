#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "image_decoder.hpp"
#include "ui_util.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace
{
    // A picture is scaled down to at most this many pixels: 256 MB in memory, and again as a bitmap. Photos of
    // 64 megapixels and less are shown as they are.
    const double kMaxPixels = 64.0 * 1024 * 1024;
    // How long closing the window waits for the thread to finish the picture it is decoding.
    const int kStopWaitMs = 2000;
    // A GIF frame that asks to stay up less than this is shown for kDefaultDelayMs, as browsers do: many GIFs
    // say 0 and count on it.
    const UINT kMinDelayMs = 20;
    const UINT kDefaultDelayMs = 100;

    UINT ReadNumber(IWICMetadataQueryReader *reader, const wchar_t *name, UINT fallback)
    {
        UINT result = fallback;
        PROPVARIANT value;
        PropVariantInit(&value);
        if (reader && SUCCEEDED(reader->GetMetadataByName(name, &value)))
        {
            switch (value.vt)
            {
            case VT_UI1: result = value.bVal; break;
            case VT_UI2: result = value.uiVal; break;
            case VT_UI4: result = value.ulVal; break;
            default: break;
            }
        }
        PropVariantClear(&value);
        return result;
    }

    // How the camera was held, from the photo's EXIF data: 1 upright (or unknown) to 8.
    UINT ExifOrientation(IWICBitmapFrameDecode *frame, const GUID &container)
    {
        const wchar_t *query = container == GUID_ContainerFormatJpeg ? L"/app1/ifd/{ushort=274}"
            : container == GUID_ContainerFormatTiff ? L"/ifd/{ushort=274}" : nullptr;
        ComPtr<IWICMetadataQueryReader> reader;
        if (!query || FAILED(frame->GetMetadataQueryReader(reader.GetAddressOf())))
            return 1;
        UINT orientation = ReadNumber(reader.Get(), query, 1);
        return orientation >= 1 && orientation <= 8 ? orientation : 1;
    }

    // Turns or mirrors 'source' with one WIC flip-rotator, so the order of the two never depends on WIC.
    bool Transform(IWICImagingFactory *wic, ComPtr<IWICBitmapSource> &source, WICBitmapTransformOptions options)
    {
        if (options == WICBitmapTransformRotate0)
            return true;
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (FAILED(wic->CreateBitmapFlipRotator(rotator.GetAddressOf())) ||
            FAILED(rotator->Initialize(source.Get(), options)))
            return false;
        source = rotator;
        return true;
    }

    // Puts a photo upright: the turn, then the mirror, of each EXIF orientation.
    bool Upright(IWICImagingFactory *wic, ComPtr<IWICBitmapSource> &source, UINT orientation)
    {
        static const WICBitmapTransformOptions kTurn[9] = { WICBitmapTransformRotate0,
            WICBitmapTransformRotate0, WICBitmapTransformRotate0, WICBitmapTransformRotate180, WICBitmapTransformRotate0,
            WICBitmapTransformRotate90, WICBitmapTransformRotate90, WICBitmapTransformRotate270, WICBitmapTransformRotate270 };
        static const WICBitmapTransformOptions kMirror[9] = { WICBitmapTransformRotate0,
            WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate0, WICBitmapTransformFlipVertical,
            WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate0 };
        return Transform(wic, source, kTurn[orientation]) && Transform(wic, source, kMirror[orientation]);
    }

    bool ToPremultipliedBgra(IWICImagingFactory *wic, ComPtr<IWICBitmapSource> &source)
    {
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                0.0, WICBitmapPaletteTypeCustom)))
            return false;
        source = converter;
        return true;
    }

    bool CopyAll(IWICBitmapSource *source, UINT &width, UINT &height, std::vector<BYTE> &pixels)
    {
        if (FAILED(source->GetSize(&width, &height)) || width == 0 || height == 0)
            return false;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        return SUCCEEDED(source->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    }

    // A still picture: scaled down if it is too big, converted to premultiplied BGRA and put upright.
    bool DecodeStill(IWICImagingFactory *wic, IWICBitmapFrameDecode *frame, UINT orientation, UINT maxSide,
        DecodedFrame &out)
    {
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0)
            return false;

        double scale = std::min(1.0, std::min(static_cast<double>(maxSide) / width, static_cast<double>(maxSide) / height));
        scale = std::min(scale, std::sqrt(kMaxPixels / (static_cast<double>(width) * height)));

        ComPtr<IWICBitmapSource> source = frame;
        // Scaled first: a JPEG decoder can then skip most of the work.
        if (scale < 1.0)
        {
            UINT scaledWidth = std::max(1u, static_cast<UINT>(width * scale));
            UINT scaledHeight = std::max(1u, static_cast<UINT>(height * scale));
            ComPtr<IWICBitmapScaler> scaler;
            if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())) ||
                FAILED(scaler->Initialize(source.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant)))
                return false;
            source = scaler;
        }
        if (!ToPremultipliedBgra(wic, source) || !Upright(wic, source, orientation) ||
            !CopyAll(source.Get(), out.width, out.height, out.pixels))
            return false;

        // Orientations 5 to 8 turn the picture a quarter.
        bool turned = orientation >= 5;
        out.sourceWidth = turned ? height : width;
        out.sourceHeight = turned ? width : height;
        out.delayMs = 0;
        return true;
    }

    // An animated GIF being played. Each frame covers part of the GIF and is drawn over what the frames before it
    // left on a canvas, which the frame then clears or restores as it asks (its "disposal") before the next one.
    struct Animation
    {
        ComPtr<IWICBitmapDecoder> decoder;
        UINT count = 0;
        UINT next = 0;    // the frame Draw makes
        UINT width = 0;   // the canvas
        UINT height = 0;
        std::vector<BYTE> canvas;
        std::vector<BYTE> saved;  // the canvas before the last frame, for disposal 3
        UINT disposal = 0;        // what the last frame asked for, and where it was
        RECT area = {};

        bool Active() const { return decoder != nullptr; }
        bool Draw(IWICImagingFactory *wic, DecodedFrame &out);
    };

    bool Animation::Draw(IWICImagingFactory *wic, DecodedFrame &out)
    {
        if (next == 0)
        {
            std::fill(canvas.begin(), canvas.end(), BYTE(0));
            disposal = 0;
        }
        else if (disposal == 2)  // back to the background, which browsers make transparent
        {
            for (LONG y = area.top; y < area.bottom; y++)
                std::fill(canvas.begin() + (static_cast<size_t>(y) * width + area.left) * 4,
                    canvas.begin() + (static_cast<size_t>(y) * width + area.right) * 4, BYTE(0));
        }
        else if (disposal == 3 && saved.size() == canvas.size())  // back to what was there before
            canvas.swap(saved);

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(next, frame.GetAddressOf())))
            return false;
        ComPtr<IWICMetadataQueryReader> reader;
        frame->GetMetadataQueryReader(reader.GetAddressOf());
        UINT left = ReadNumber(reader.Get(), L"/imgdesc/Left", 0);
        UINT top = ReadNumber(reader.Get(), L"/imgdesc/Top", 0);
        UINT delay = ReadNumber(reader.Get(), L"/grctlext/Delay", 0) * 10;  // in hundredths of a second
        UINT frameDisposal = ReadNumber(reader.Get(), L"/grctlext/Disposal", 0);

        ComPtr<IWICBitmapSource> source = frame;
        UINT frameWidth = 0, frameHeight = 0;
        std::vector<BYTE> pixels;
        if (!ToPremultipliedBgra(wic, source) || !CopyAll(source.Get(), frameWidth, frameHeight, pixels))
            return false;

        if (frameDisposal == 3)
            saved = canvas;

        // The frame over the canvas, where it is opaque. GIF pixels are either opaque or transparent, but the blend
        // is the usual one anyway.
        UINT right = std::min(width, left + frameWidth);
        UINT bottom = std::min(height, top + frameHeight);
        for (UINT y = top; y < bottom; y++)
        {
            const BYTE *from = pixels.data() + static_cast<size_t>(y - top) * frameWidth * 4;
            BYTE *to = canvas.data() + (static_cast<size_t>(y) * width + left) * 4;
            for (UINT x = left; x < right; x++, from += 4, to += 4)
            {
                UINT alpha = from[3];
                if (alpha == 255)
                    std::copy(from, from + 4, to);
                else if (alpha != 0)
                {
                    for (int c = 0; c < 4; c++)
                        to[c] = static_cast<BYTE>(from[c] + to[c] * (255 - alpha) / 255);
                }
            }
        }

        out.width = out.sourceWidth = width;
        out.height = out.sourceHeight = height;
        out.pixels = canvas;
        out.delayMs = delay < kMinDelayMs ? kDefaultDelayMs : delay;

        disposal = frameDisposal;
        area = { static_cast<LONG>(std::min(left, width)), static_cast<LONG>(std::min(top, height)),
            static_cast<LONG>(right), static_cast<LONG>(bottom) };
        if (area.right < area.left)
            area.right = area.left;
        if (area.bottom < area.top)
            area.bottom = area.top;
        next = (next + 1) % count;
        return true;
    }

    // Decodes the first picture of a file. A GIF with several frames becomes an animation, played by 'animation',
    // unless it is too big for one: then only its first frame is shown.
    bool DecodeFirst(IWICImagingFactory *wic, const std::wstring &path, UINT maxSide, Animation &animation,
        DecodedFrame &out)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf())))
            return false;
        GUID container = {};
        UINT count = 0;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetContainerFormat(&container)) || FAILED(decoder->GetFrameCount(&count)) || count == 0 ||
            FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
            return false;

        if (container == GUID_ContainerFormatGif && count > 1)
        {
            UINT width = 0, height = 0;
            frame->GetSize(&width, &height);
            ComPtr<IWICMetadataQueryReader> reader;
            decoder->GetMetadataQueryReader(reader.GetAddressOf());
            width = ReadNumber(reader.Get(), L"/logscrdesc/Width", width);
            height = ReadNumber(reader.Get(), L"/logscrdesc/Height", height);
            if (width > 0 && height > 0 && width <= maxSide && height <= maxSide &&
                static_cast<double>(width) * height <= kMaxPixels)
            {
                animation.decoder = decoder;
                animation.count = count;
                animation.width = width;
                animation.height = height;
                animation.canvas.assign(static_cast<size_t>(width) * height * 4, 0);
                if (animation.Draw(wic, out))
                    return true;
                animation = Animation();
            }
        }

        return DecodeStill(wic, frame.Get(), ExifOrientation(frame.Get(), container), maxSide, out);
    }

    // The extensions WIC's decoders take, lowercase with their dot: ".jpg", ".png"...
    std::vector<std::wstring> DecoderExtensions()
    {
        std::vector<std::wstring> extensions;
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IEnumUnknown> decoders;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) ||
            FAILED(wic->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, decoders.GetAddressOf())))
            return extensions;

        ComPtr<IUnknown> item;
        ULONG fetched = 0;
        while (decoders->Next(1, item.ReleaseAndGetAddressOf(), &fetched) == S_OK)
        {
            ComPtr<IWICBitmapDecoderInfo> info;
            UINT length = 0;
            if (FAILED(item.As(&info)) || FAILED(info->GetFileExtensions(0, nullptr, &length)) || length == 0)
                continue;
            std::wstring list(length, L'\0');
            if (FAILED(info->GetFileExtensions(length, &list[0], &length)))
                continue;
            list.resize(wcsnlen(list.c_str(), list.size()));
            CharLowerBuffW(&list[0], static_cast<DWORD>(list.size()));

            // A comma-separated list: ".jpeg,.jpe,.jpg,.jfif,.exif".
            size_t start = 0;
            while (start < list.size())
            {
                size_t end = list.find(L',', start);
                if (end == std::wstring::npos)
                    end = list.size();
                std::wstring extension = list.substr(start, end - start);
                extension.erase(0, extension.find_first_not_of(L' '));
                extension.erase(extension.find_last_not_of(L' ') + 1);
                if (extension.size() > 1 && extension[0] == L'.')
                    extensions.push_back(extension);
                start = end + 1;
            }
        }
        return extensions;
    }
}

bool IsDecodableImage(const std::string &path)
{
    // Listed once: installing a codec while Corpus runs takes a restart to show.
    static const std::vector<std::wstring> extensions = DecoderExtensions();

    std::wstring wide = Utf8ToWide(path);
    size_t dot = wide.find_last_of(L'.');
    size_t slash = wide.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return false;
    std::wstring extension = wide.substr(dot);
    CharLowerBuffW(&extension[0], static_cast<DWORD>(extension.size()));
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

// ---- ImageDecoder ----

ImageDecoder::~ImageDecoder()
{
    Stop();
}

void ImageDecoder::Start(HWND hWnd)
{
    if (m_thread.joinable())
        return;
    m_shared->hWnd = hWnd;
    m_shared->running = true;
    m_thread = std::thread(&ImageDecoder::Run, m_shared);
}

void ImageDecoder::Stop()
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

void ImageDecoder::Open(const std::wstring &path, UINT maxSide)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->request++;
    m_shared->path = path;
    m_shared->maxSide = maxSide;
    m_shared->wantNext = false;
    m_shared->status = Status::kNothing;
    m_shared->frame = DecodedFrame();
    m_shared->wake.notify_all();
}

ImageDecoder::Status ImageDecoder::Take(DecodedFrame &frame)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    Status status = m_shared->status;
    if (status != Status::kNothing)
    {
        frame = std::move(m_shared->frame);
        m_shared->frame = DecodedFrame();
        m_shared->status = Status::kNothing;
    }
    return status;
}

void ImageDecoder::WantNext()
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->wantNext = true;
    m_shared->wake.notify_all();
}

void ImageDecoder::Run(std::shared_ptr<Shared> shared)
{
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    Animation animation;
    unsigned current = 0;
    for (;;)
    {
        std::wstring path;
        UINT maxSide = 0;
        bool nextFrame = false;
        {
            std::unique_lock<std::mutex> lock(shared->mutex);
            shared->wake.wait(lock, [&]
            {
                return shared->stop || shared->request != current || (shared->wantNext && animation.Active());
            });
            if (shared->stop)
                break;
            if (shared->request != current)
            {
                current = shared->request;
                path = shared->path;
                maxSide = shared->maxSide;
            }
            else
                nextFrame = true;
            shared->wantNext = false;
        }

        DecodedFrame frame;
        Status status = Status::kFrame;
        if (nextFrame)
        {
            // A frame that can't be decoded ends the animation on the one shown.
            if (!animation.Draw(wic.Get(), frame))
            {
                animation = Animation();
                continue;
            }
        }
        else
        {
            animation = Animation();
            if (path.empty())
                continue;
            if (!wic || !DecodeFirst(wic.Get(), path, maxSide, animation, frame))
                status = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? Status::kMissing : Status::kFailed;
        }

        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->stop)
            break;
        // Opened something else meanwhile: this one isn't wanted anymore.
        if (shared->request != current)
            continue;
        shared->status = status;
        shared->frame = std::move(frame);
        PostMessage(shared->hWnd, kImageDecoded, 0, 0);
    }

    animation = Animation();
    wic.Reset();
    if (com)
        CoUninitialize();
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->running = false;
    shared->done.notify_all();
}
