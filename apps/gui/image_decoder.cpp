#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "image_decoder.hpp"
#include "psd.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace
{
    // A picture is scaled down to at most this many pixels: 256 MB in memory, and again as a bitmap. Photos of
    // 64 megapixels and less are shown as they are.
    const double kMaxPixels = 64.0 * 1024 * 1024;
    // An animation keeps several canvases at once (the decoder's, the frame on screen and the next one), so a bigger
    // one than this shows its first frame as a still picture.
    const double kMaxAnimationPixels = 16.0 * 1024 * 1024;
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

    // A still picture: scaled down if it is too big, converted to premultiplied BGRA and put upright. 'maxWidth'
    // is for the picture upright.
    bool DecodeStill(IWICImagingFactory *wic, IWICBitmapSource *frame, UINT orientation, UINT maxSide,
        DecodedFrame &out, UINT maxWidth = UINT_MAX)
    {
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0)
            return false;

        double scale = std::min(1.0, std::min(static_cast<double>(maxSide) / width, static_cast<double>(maxSide) / height));
        scale = std::min(scale, static_cast<double>(maxWidth) / (orientation >= 5 ? height : width));
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
        if (!ToPremultipliedBgra(wic, source))
            return false;
        // A flip-rotator reads its source in an order WIC decoders are slow at, so a photo to be turned is decoded
        // into memory first.
        if (orientation != 1)
        {
            ComPtr<IWICBitmap> decoded;
            if (FAILED(wic->CreateBitmapFromSource(source.Get(), WICBitmapCacheOnLoad, decoded.GetAddressOf())))
                return false;
            source = decoded;
        }
        if (!Upright(wic, source, orientation) || !CopyAll(source.Get(), out.width, out.height, out.pixels))
            return false;

        // Orientations 5 to 8 turn the picture a quarter.
        bool turned = orientation >= 5;
        out.sourceWidth = turned ? height : width;
        out.sourceHeight = turned ? width : height;
        out.delayMs = 0;
        return true;
    }

    // A picture in memory as a WIC source. WIC copies the pixels.
    bool SourceFromMemory(IWICImagingFactory *wic, UINT width, UINT height, REFWICPixelFormatGUID format,
        std::vector<BYTE> &pixels, ComPtr<IWICBitmapSource> &out)
    {
        ComPtr<IWICBitmap> bitmap;
        if (width == 0 || height == 0 || pixels.size() < static_cast<size_t>(width) * height * 4 ||
            FAILED(wic->CreateBitmapFromMemory(width, height, format, width * 4, static_cast<UINT>(pixels.size()),
                pixels.data(), bitmap.GetAddressOf())))
            return false;
        out = bitmap;
        return true;
    }

    // The JPEG thumbnail inside a Photoshop file. Photoshop 4 stored its channels blue first, so those are swapped.
    // 'thumb' has to outlive 'out' when the JPEG is decoded straight from its bytes.
    bool PsdThumbnailSource(IWICImagingFactory *wic, psd_thumbnail &thumb, ComPtr<IWICBitmapSource> &out)
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (thumb.jpeg.empty() || FAILED(wic->CreateStream(stream.GetAddressOf())) ||
            FAILED(stream->InitializeFromMemory(thumb.jpeg.data(), static_cast<DWORD>(thumb.jpeg.size()))) ||
            FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf())) ||
            FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
            return false;
        ComPtr<IWICBitmapSource> source = frame;
        if (!thumb.bgr)
        {
            out = source;
            return true;
        }
        ComPtr<IWICFormatConverter> converter;
        UINT width = 0, height = 0;
        std::vector<BYTE> pixels;
        if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                0.0, WICBitmapPaletteTypeCustom)) ||
            !CopyAll(converter.Get(), width, height, pixels))
            return false;
        for (size_t i = 0; i < pixels.size(); i += 4)
            std::swap(pixels[i], pixels[i + 2]);
        return SourceFromMemory(wic, width, height, GUID_WICPixelFormat32bppBGRA, pixels, out);
    }

    // The merged image of a Photoshop file; false with the reason in 'status' (blank counts as no_composite).
    bool PsdCompositeSource(IWICImagingFactory *wic, const std::string &path, psd_status &status,
        ComPtr<IWICBitmapSource> &out)
    {
        psd_image image;
        status = psd_decode_composite_file(path, image, static_cast<uint64_t>(kMaxPixels));
        if (status == psd_status::ok && image.blank)
            status = psd_status::no_composite;
        return status == psd_status::ok &&
            SourceFromMemory(wic, image.width, image.height, GUID_WICPixelFormat32bppRGBA, image.rgba, out);
    }

    // An animated GIF being played. Each frame covers part of the GIF and is drawn over what the frames before it
    // left on a canvas, which the frame then clears or restores as it asks (its "disposal") before the next one.
    struct Animation
    {
        ComPtr<IWICBitmapDecoder> decoder;
        UINT count = 0;
        UINT next = 0;    // the frame Draw makes
        UINT plays = 1;   // how many times it plays through; 0 for ever
        UINT played = 0;
        bool ended = false;  // the last frame of the last play is drawn
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
        if (next == 0 && plays != 0 && ++played >= plays)
            ended = true;
        return true;
    }

    // How many times a GIF plays through, from its NETSCAPE2.0 extension: 0 for ever. Without one it plays once, as
    // in browsers, and a loop count of n plays it n + 1 times.
    UINT GifPlays(IWICBitmapDecoder *decoder)
    {
        ComPtr<IWICMetadataQueryReader> reader;
        if (FAILED(decoder->GetMetadataQueryReader(reader.GetAddressOf())))
            return 1;
        UINT plays = 1;
        PROPVARIANT application, data;
        PropVariantInit(&application);
        PropVariantInit(&data);
        if (SUCCEEDED(reader->GetMetadataByName(L"/appext/Application", &application)) &&
            application.vt == (VT_UI1 | VT_VECTOR) && application.caub.cElems == 11 &&
            (memcmp(application.caub.pElems, "NETSCAPE2.0", 11) == 0 || memcmp(application.caub.pElems, "ANIMEXTS1.0", 11) == 0) &&
            SUCCEEDED(reader->GetMetadataByName(L"/appext/Data", &data)) && data.vt == (VT_UI1 | VT_VECTOR) &&
            data.caub.cElems >= 4 && data.caub.pElems[0] >= 3 && data.caub.pElems[1] == 1)
        {
            // The sub-block: its size, 1, then the loop count, low byte first.
            UINT loops = MAKEWORD(data.caub.pElems[2], data.caub.pElems[3]);
            plays = loops == 0 ? 0 : loops + 1;
        }
        PropVariantClear(&application);
        PropVariantClear(&data);
        return plays;
    }

    // The colour depth of each icon in an .ico, in the order WIC lists them, from the file's directory: WIC decodes
    // every one to 32 bits per pixel and doesn't say. An entry that gives no bit count says how many colours it has.
    std::vector<UINT> IconDepths(const std::wstring &path)
    {
        std::vector<UINT> depths;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return depths;
        BYTE header[6];
        DWORD read = 0;
        if (ReadFile(file, header, sizeof(header), &read, nullptr) && read == sizeof(header))
        {
            UINT count = MAKEWORD(header[4], header[5]);
            std::vector<BYTE> entries(static_cast<size_t>(count) * 16);
            if (!entries.empty() && ReadFile(file, entries.data(), static_cast<DWORD>(entries.size()), &read, nullptr) &&
                read == entries.size())
            {
                for (UINT i = 0; i < count; i++)
                {
                    const BYTE *entry = entries.data() + i * 16;
                    UINT bits = MAKEWORD(entry[6], entry[7]);
                    UINT colors = entry[2];
                    if (bits == 0)
                        bits = colors == 0 ? 8 : colors <= 2 ? 1 : colors <= 16 ? 4 : 8;
                    depths.push_back(bits);
                }
            }
        }
        CloseHandle(file);
        return depths;
    }

    // Decodes the first picture of a file. A GIF with several frames becomes an animation, played by 'animation',
    // unless it is too big for one: then only its first frame is shown. An icon shows its largest size.
    bool DecodeFirst(IWICImagingFactory *wic, const std::wstring &path, UINT maxSide, Animation &animation,
        DecodedFrame &out)
    {
        if (IsPsd(WideToUtf8(path)))
            return DecodePsd(wic, WideToUtf8(path), maxSide, out);
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
                static_cast<double>(width) * height <= kMaxAnimationPixels)
            {
                animation.decoder = decoder;
                animation.count = count;
                animation.plays = GifPlays(decoder.Get());
                animation.width = width;
                animation.height = height;
                animation.canvas.assign(static_cast<size_t>(width) * height * 4, 0);
                if (animation.Draw(wic, out))
                    return true;
                animation = Animation();
            }
        }

        // An .ico holds the same icon at several sizes, often the smallest first, and sometimes a size in several
        // colour depths: the largest wins, with the most colours.
        if (container == GUID_ContainerFormatIco)
        {
            std::vector<UINT> depths = IconDepths(path);
            UINT largest = 0;
            UINT deepest = 0;
            for (UINT i = 0; i < count; i++)
            {
                ComPtr<IWICBitmapFrameDecode> size;
                UINT width = 0, height = 0;
                if (FAILED(decoder->GetFrame(i, size.GetAddressOf())) || FAILED(size->GetSize(&width, &height)))
                    continue;
                UINT bits = i < depths.size() ? depths[i] : 0;
                if (width * height > largest || (width * height == largest && bits > deepest))
                {
                    largest = width * height;
                    deepest = bits;
                    frame = size;
                }
            }
        }

        return DecodeStill(wic, frame.Get(), ExifOrientation(frame.Get(), container), maxSide, out);
    }

    // The extensions WIC's decoders take, lowercase with their dot: ".jpg", ".png"...
    std::unordered_set<std::string> DecoderExtensions()
    {
        std::unordered_set<std::string> extensions;
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
                    extensions.insert(WideToUtf8(extension));
                start = end + 1;
            }
        }
        return extensions;
    }
}

bool DecodePicture(IWICImagingFactory *wic, IStream *stream, UINT maxWidth, UINT maxSide, DecodedFrame &out)
{
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    GUID container = {};
    if (FAILED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        FAILED(decoder->GetContainerFormat(&container)) || FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
        return false;
    return DecodeStill(wic, frame.Get(), ExifOrientation(frame.Get(), container), maxSide, out, maxWidth);
}

bool IsPsd(const std::string &path)
{
    std::string extension;
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("\\/");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return false;
    for (size_t i = dot + 1; i < path.size(); i++)
        extension += static_cast<char>(path[i] >= 'A' && path[i] <= 'Z' ? path[i] - 'A' + 'a' : path[i]);
    return extension == "psd" || extension == "psb";
}

bool DecodePsd(IWICImagingFactory *wic, const std::string &path, UINT maxSide, DecodedFrame &out)
{
    ComPtr<IWICBitmapSource> source;
    psd_status status = psd_status::ok;
    if (PsdCompositeSource(wic, path, status, source) && DecodeStill(wic, source.Get(), 1, maxSide, out))
        return true;

    // No usable merged image: the thumbnail inside the file, and why it is all there is.
    psd_thumbnail thumb;
    if (status == psd_status::ok)
        status = psd_status::unsupported;
    if (psd_read_thumbnail_file(path, thumb) != psd_status::ok || !PsdThumbnailSource(wic, thumb, source) ||
        !DecodeStill(wic, source.Get(), 1, maxSide, out))
        return false;
    out.note = strings::kPsdPreviewOnly;
    out.note += L": ";
    const char *reason = psd_describe(status);
    out.note.append(reason, reason + strlen(reason));
    return true;
}

bool DecodePsdThumbnail(IWICImagingFactory *wic, const std::string &path, UINT size, DecodedFrame &out)
{
    ComPtr<IWICBitmapSource> source;
    psd_thumbnail thumb;
    bool haveThumb = psd_read_thumbnail_file(path, thumb) == psd_status::ok;
    // Sharp enough for the tile: the embedded thumbnail (usually 160 pixels wide) does the job on small tiles.
    if (haveThumb && std::max(thumb.width, thumb.height) >= size)
    {
        if (PsdThumbnailSource(wic, thumb, source) && DecodeStill(wic, source.Get(), 1, size, out))
            return true;
    }
    psd_status status = psd_status::ok;
    if (PsdCompositeSource(wic, path, status, source) && DecodeStill(wic, source.Get(), 1, size, out))
        return true;
    return haveThumb && PsdThumbnailSource(wic, thumb, source) && DecodeStill(wic, source.Get(), 1, size, out);
}

bool IsDecodableImage(const std::string &path)
{
    // Listed once: installing a codec while Corpus runs takes a restart to show.
    static const std::unordered_set<std::string> extensions = DecoderExtensions();

    // Run over every file on the grid when a picture opens, so it works on the UTF-8 path as it is. Extensions are
    // ASCII, so lowering ASCII letters is enough to match them.
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("\\/");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return false;
    std::string extension = path.substr(dot);
    for (char &c : extension)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return extensions.count(extension) != 0;
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
            // The last frame stays up, and the file is let go.
            if (animation.ended)
            {
                frame.last = true;
                animation = Animation();
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
