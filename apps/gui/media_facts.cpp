#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "media_facts.hpp"
#include "file_kind.hpp"
#include "ui_util.hpp"
#include <windows.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <utility>
#include <cwctype>

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

    std::wstring Rate(unsigned long long hz)
    {
        wchar_t text[32];
        if (hz % 1000 == 0)
            swprintf(text, 32, L"%llu kHz", hz / 1000);
        else
            swprintf(text, 32, L"%.1f kHz", static_cast<double>(hz) / 1000.0);
        return text;
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
    if (kind != FileKind::kPicture && kind != FileKind::kVideo && kind != FileKind::kAudio)
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

    if (kind == FileKind::kPicture)
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
        if (w && h)
            Add(line, Dimensions(w, h));
        if (unsigned long long ticks = Number(store.Get(), L"System.Media.Duration"))
            Add(line, Duration(ticks));
    }
    else if (store)
    {
        unsigned long long bits = Number(store.Get(), L"System.Audio.SampleSize");
        unsigned long long hz = Number(store.Get(), L"System.Audio.SampleRate");
        std::wstring quality;
        if (bits)
            quality = std::to_wstring(bits) + L"-bit";
        if (hz)
            quality += (quality.empty() ? L"" : L" / ") + Rate(hz);
        Add(line, quality);
        if (unsigned long long ticks = Number(store.Get(), L"System.Media.Duration"))
            Add(line, Duration(ticks));
    }
    // Just the extension tells nothing the name doesn't.
    return line.size() == format.size() ? std::wstring() : line;
}
