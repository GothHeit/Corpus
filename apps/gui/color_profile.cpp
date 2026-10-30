#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "color_profile.hpp"
#include <cstring>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace
{
    std::mutex g_mutex;
    std::wstring g_displayProfile;

    UINT BigEndian32(const BYTE *p)
    {
        return static_cast<UINT>(p[0]) << 24 | static_cast<UINT>(p[1]) << 16 | static_cast<UINT>(p[2]) << 8 | p[3];
    }

    // Whether an ICC profile is an RGB one whose description names sRGB, as the profiles cameras and editors embed
    // for it do. Converting from those changes nothing, so it is skipped.
    bool DescribesSrgb(const BYTE *data, size_t size)
    {
        if (size < 132 || std::memcmp(data + 16, "RGB ", 4) != 0)
            return false;
        UINT count = BigEndian32(data + 128);
        for (size_t i = 0; i < count && 132 + 12 * (i + 1) <= size; i++)
        {
            const BYTE *entry = data + 132 + 12 * i;
            if (std::memcmp(entry, "desc", 4) != 0)
                continue;
            size_t offset = BigEndian32(entry + 4), length = BigEndian32(entry + 8);
            if (offset > size || length > size - offset)
                return false;
            // The text is ASCII in a v2 profile, UTF-16 big endian in a v4 one.
            const BYTE *text = data + offset;
            for (size_t k = 0; k + 4 <= length; k++)
            {
                if (std::memcmp(text + k, "sRGB", 4) == 0)
                    return true;
                if (k + 8 <= length && std::memcmp(text + k, "\0s\0R\0G\0B", 8) == 0)
                    return true;
            }
            return false;
        }
        return false;
    }

    // The CMYK profile Windows ships (US Web Coated SWOP), for CMYK pictures that carry none.
    std::wstring StockCmykProfile()
    {
        wchar_t system[MAX_PATH];
        UINT length = GetSystemDirectoryW(system, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return std::wstring();
        std::wstring path = std::wstring(system, length) + L"\\spool\\drivers\\color\\RSWOP.icm";
        return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring() : path;
    }
}

std::wstring MonitorProfile(HWND hWnd)
{
    MONITORINFOEXW monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return std::wstring();
    HDC dc = CreateDCW(L"DISPLAY", monitor.szDevice, nullptr, nullptr);
    if (!dc)
        return std::wstring();
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    BOOL found = GetICMProfileW(dc, &length, path);
    DeleteDC(dc);
    if (!found)
        return std::wstring();
    // Windows' own sRGB profile is what an uncalibrated monitor gets: nothing to convert to.
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"sRGB Color Space Profile.icm") == 0 ? std::wstring() : std::wstring(path);
}

void SetDisplayProfile(const std::wstring &path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_displayProfile = path;
}

std::wstring DisplayProfile()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_displayProfile;
}

ComPtr<IWICColorContext> EmbeddedProfile(IWICImagingFactory *wic, IWICBitmapFrameDecode *frame)
{
    UINT count = 0;
    if (FAILED(frame->GetColorContexts(0, nullptr, &count)) || count == 0)
        return nullptr;
    std::vector<ComPtr<IWICColorContext>> contexts(count);
    std::vector<IWICColorContext*> pointers(count);
    for (UINT i = 0; i < count; i++)
    {
        if (FAILED(wic->CreateColorContext(contexts[i].GetAddressOf())))
            return nullptr;
        pointers[i] = contexts[i].Get();
    }
    if (FAILED(frame->GetColorContexts(count, pointers.data(), &count)))
        return nullptr;

    // An ICC profile wins over an EXIF color space, which only ever says sRGB (1) or Adobe RGB (2).
    ComPtr<IWICColorContext> exif;
    for (UINT i = 0; i < count && i < contexts.size(); i++)
    {
        WICColorContextType type;
        if (FAILED(contexts[i]->GetType(&type)))
            continue;
        if (type == WICColorContextProfile)
        {
            UINT size = 0;
            if (FAILED(contexts[i]->GetProfileBytes(0, nullptr, &size)) || size == 0)
                continue;
            std::vector<BYTE> bytes(size);
            if (FAILED(contexts[i]->GetProfileBytes(size, bytes.data(), &size)))
                continue;
            return DescribesSrgb(bytes.data(), bytes.size()) ? nullptr : contexts[i];
        }
        UINT space = 0;
        if (type == WICColorContextExifColorSpace && SUCCEEDED(contexts[i]->GetExifColorSpace(&space)) && space == 2)
            exif = contexts[i];
    }
    return exif;
}

ComPtr<IWICColorContext> ProfileFromBytes(IWICImagingFactory *wic, const std::vector<BYTE> &bytes)
{
    ComPtr<IWICColorContext> context;
    if (bytes.empty() || DescribesSrgb(bytes.data(), bytes.size()) || FAILED(wic->CreateColorContext(context.GetAddressOf())) ||
        FAILED(context->InitializeFromMemory(bytes.data(), static_cast<UINT>(bytes.size()))))
        return nullptr;
    return context;
}

bool IsCmyk(IWICBitmapSource *source)
{
    WICPixelFormatGUID format;
    return SUCCEEDED(source->GetPixelFormat(&format)) && (format == GUID_WICPixelFormat32bppCMYK ||
        format == GUID_WICPixelFormat64bppCMYK || format == GUID_WICPixelFormat40bppCMYKAlpha ||
        format == GUID_WICPixelFormat80bppCMYKAlpha);
}

bool ConvertColors(IWICImagingFactory *wic, ComPtr<IWICBitmapSource> &source, IWICColorContext *from,
    const std::wstring &to)
{
    bool cmyk = IsCmyk(source.Get());
    if (!from && !cmyk && to.empty())
        return false;

    ComPtr<IWICColorContext> input = from, output;
    if (!input)
    {
        std::wstring stock = cmyk ? StockCmykProfile() : std::wstring();
        if ((cmyk && stock.empty()) || FAILED(wic->CreateColorContext(input.GetAddressOf())) ||
            FAILED(cmyk ? input->InitializeFromFilename(stock.c_str()) : input->InitializeFromExifColorSpace(1)))
            return false;
    }
    if (FAILED(wic->CreateColorContext(output.GetAddressOf())) ||
        FAILED(to.empty() ? output->InitializeFromExifColorSpace(1) : output->InitializeFromFilename(to.c_str())))
        return false;

    ComPtr<IWICColorTransform> transform;
    if (FAILED(wic->CreateColorTransformer(transform.GetAddressOf())))
        return false;
    if (FAILED(transform->Initialize(source.Get(), input.Get(), output.Get(), GUID_WICPixelFormat32bppBGRA)))
    {
        // The transform reads few pixel formats (not palettes, for one): those go through plain BGRA first.
        ComPtr<IWICFormatConverter> converter;
        if (cmyk || FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr,
                0.0, WICBitmapPaletteTypeCustom)) ||
            FAILED(transform->Initialize(converter.Get(), input.Get(), output.Get(), GUID_WICPixelFormat32bppBGRA)))
            return false;
    }
    source = transform;
    return true;
}

bool ConvertPixels(IWICImagingFactory *wic, UINT width, UINT height, std::vector<BYTE> &pixels, const std::wstring &to)
{
    ComPtr<IWICBitmap> bitmap;
    if (to.empty() || width == 0 || height == 0 || pixels.size() < static_cast<size_t>(width) * height * 4 ||
        FAILED(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, width * 4,
            static_cast<UINT>(pixels.size()), pixels.data(), bitmap.GetAddressOf())))
        return false;
    ComPtr<IWICBitmapSource> source = bitmap;
    std::vector<BYTE> converted(pixels.size());
    if (!ConvertColors(wic, source, nullptr, to) ||
        FAILED(source->CopyPixels(nullptr, width * 4, static_cast<UINT>(converted.size()), converted.data())))
        return false;
    pixels.swap(converted);
    return true;
}
