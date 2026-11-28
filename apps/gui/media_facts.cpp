#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "media_facts.hpp"
#include "book_pages.hpp"
#include "file_kind.hpp"
#include "ui_util.hpp"
#include "../../include/audio_depth.hpp"
#include <windows.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <utility>
#include <cctype>
#include <cwctype>
#include <memory>
#include "strings.hpp"

using Microsoft::WRL::ComPtr;

namespace
{
    // One number of the property store by its canonical name, 0 when the file has none.
    unsigned long long Number(IPropertyStore *store, const wchar_t *name)
    {
        PROPERTYKEY key;
        PROPVARIANT value;
        PropVariantInit(&value);
        unsigned long long out = 0;
        if (SUCCEEDED(PSGetPropertyKeyFromName(name, &key)) && SUCCEEDED(store->GetValue(key, &value)))
        {
            if (value.vt == VT_UI4)
                out = value.ulVal;
            else if (value.vt == VT_UI8)
                out = value.uhVal.QuadPart;
            else if (value.vt == VT_I4 && value.lVal > 0)
                out = static_cast<unsigned long long>(value.lVal);
        }
        PropVariantClear(&value);
        return out;
    }

    // WIC's size for a picture the property system has none for.
    bool PictureSize(const std::wstring &path, unsigned long long &w, unsigned long long &h)
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        UINT fw = 0, fh = 0;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
            FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
            FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(frame->GetSize(&fw, &fh)) || fw == 0 || fh == 0)
            return false;
        w = fw;
        h = fh;
        return true;
    }

    std::wstring Dimensions(unsigned long long w, unsigned long long h)
    {
        return std::to_wstring(w) + L" × " + std::to_wstring(h);
    }

    // 100-nanosecond units to "4:12" or "1:02:30".
    std::wstring Duration(unsigned long long ticks)
    {
        unsigned long long seconds = (ticks + 5000000ULL) / 10000000ULL;
        wchar_t text[32];
        if (seconds >= 3600)
            swprintf(text, 32, L"%llu:%02llu:%02llu", seconds / 3600, seconds / 60 % 60, seconds % 60);
        else
            swprintf(text, 32, L"%llu:%02llu", seconds / 60, seconds % 60);
        return text;
    }

    // "44.1 kHz", "96 kHz", "22.05 kHz": as many decimals as the rate has.
    std::wstring Rate(unsigned long long hz)
    {
        wchar_t text[32];
        if (hz % 1000 == 0)
            swprintf(text, 32, L"%llu kHz", hz / 1000);
        else if (hz % 100 == 0)
            swprintf(text, 32, L"%.1f kHz", static_cast<double>(hz) / 1000.0);
        else
            swprintf(text, 32, L"%.2f kHz", static_cast<double>(hz) / 1000.0);
        return text;
    }

    // A video's codec from the four letters its file names it by ("avc1"), in the words people know it by.
    std::wstring Codec(unsigned long long fourcc)
    {
        char code[5] = {};
        for (int i = 0; i < 4; i++)
        {
            char c = static_cast<char>((fourcc >> (8 * i)) & 0xFF);
            code[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        std::string name(code);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\0'))
            name.pop_back();
        static const struct { const char *code; const wchar_t *name; } kKnown[] = {
            { "H264", L"H.264" }, { "AVC1", L"H.264" }, { "X264", L"H.264" }, { "AVC3", L"H.264" },
            { "HEVC", L"HEVC" }, { "HVC1", L"HEVC" }, { "HEV1", L"HEVC" }, { "H265", L"HEVC" }, { "X265", L"HEVC" },
            { "AV01", L"AV1" }, { "VP90", L"VP9" }, { "VP09", L"VP9" }, { "VP80", L"VP8" },
            { "MP4V", L"MPEG-4" }, { "XVID", L"MPEG-4" }, { "DIVX", L"MPEG-4" }, { "DX50", L"MPEG-4" },
            { "MPG2", L"MPEG-2" }, { "MJPG", L"MJPEG" }, { "WMV3", L"WMV" }, { "WVC1", L"VC-1" },
            { "APCN", L"ProRes" }, { "APCH", L"ProRes" }, { "APCS", L"ProRes" }, { "APCO", L"ProRes" }, { "AP4H", L"ProRes" },
        };
        for (const auto &known : kKnown)
        {
            if (name == known.code)
                return known.name;
        }
        // An unknown code is shown as it is, when it is letters and digits.
        for (char c : name)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)))
                return std::wstring();
        }
        return std::wstring(name.begin(), name.end());
    }

    // "248 pages", for a book with fixed pages (a PDF or a comic book); nothing for one whose pages flow.
    std::wstring Pages(const std::string &path)
    {
        if (IsReflowable(path))
            return std::wstring();
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))))
            return std::wstring();
        std::unique_ptr<BookPages> book = BookPages::Open(factory.Get(), FileWide(path));
        if (!book || book->Count() == 0)
            return std::wstring();
        return FormatCount(book->Count()) + L" " + (book->Count() == 1 ? strings::kInfoPage : strings::kInfoPages);
    }

    void Add(std::wstring &line, const std::wstring &part)
    {
        if (!part.empty())
            line += (line.empty() ? L"" : L" · ") + part;
    }
}

std::wstring MediaFacts(const std::string &path)
{
    FileKind kind = KindOf(path);
    bool book = IsBook(path);
    if (kind != FileKind::kPicture && kind != FileKind::kVideo && kind != FileKind::kAudio && !book)
        return std::wstring();

    std::wstring wide = FileWide(path);
    ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreFromParsingName(wide.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(store.GetAddressOf()))))
        store.Reset();

    std::wstring line;
    std::string ext = FileExtension(path);
    std::wstring format;
    for (char c : ext)
        format += static_cast<wchar_t>(std::towupper(static_cast<unsigned char>(c)));
    Add(line, format);

    if (book)
        Add(line, Pages(path));
    else if (kind == FileKind::kPicture)
    {
        unsigned long long w = store ? Number(store.Get(), L"System.Image.HorizontalSize") : 0;
        unsigned long long h = store ? Number(store.Get(), L"System.Image.VerticalSize") : 0;
        if ((w == 0 || h == 0) && !PictureSize(wide, w, h))
            w = h = 0;
        // A photo held sideways (EXIF orientation 5 to 8) is shown upright: its sides trade places.
        unsigned long long turn = store ? Number(store.Get(), L"System.Photo.Orientation") : 0;
        if (turn >= 5 && turn <= 8)
            std::swap(w, h);
        if (w && h)
            Add(line, Dimensions(w, h));
    }
    else if (store && kind == FileKind::kVideo)
    {
        unsigned long long w = Number(store.Get(), L"System.Video.FrameWidth");
        unsigned long long h = Number(store.Get(), L"System.Video.FrameHeight");
        Add(line, Codec(Number(store.Get(), L"System.Video.FourCC")));
        if (w && h)
            Add(line, Dimensions(w, h));
        if (unsigned long long ticks = Number(store.Get(), L"System.Media.Duration"))
            Add(line, Duration(ticks));
    }
    else if (kind == FileKind::kAudio)
    {
        unsigned long long bits = store ? Number(store.Get(), L"System.Audio.SampleSize") : 0;
        unsigned long long hz = store ? Number(store.Get(), L"System.Audio.SampleRate") : 0;
        // Windows has no handler for some lossless files, or reports no depth: their own header may say it.
        if (bits == 0)
        {
            audio_depth depth = audio_read_depth_file(path);
            if (depth.bits > 0)
                bits = static_cast<unsigned long long>(depth.bits);
        }
        std::wstring quality;
        if (bits)
            quality = std::to_wstring(bits) + L"-bit";
        if (hz)
            quality += (quality.empty() ? L"" : L" / ") + Rate(hz);
        Add(line, quality);
        if (unsigned long long ticks = store ? Number(store.Get(), L"System.Media.Duration") : 0)
            Add(line, Duration(ticks));
    }
    // Just the extension tells nothing the name doesn't.
    return line.size() == format.size() ? std::wstring() : line;
}
