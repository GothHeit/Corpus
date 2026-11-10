#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "board_frames.hpp"
#include "ui_util.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <unordered_set>
#include "../../include/board.hpp"

using Microsoft::WRL::ComPtr;

namespace
{
    ComPtr<IWICImagingFactory> Wic()
    {
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
        return wic;
    }
}

namespace
{
    std::wstring FramesFolder(const std::wstring &thumbnailFolder)
    {
        std::wstring folder = thumbnailFolder;
        if (!folder.empty() && folder.back() != L'\\')
            folder += L'\\';
        return folder + L"board-frames\\";
    }

    std::wstring FrameName(std::int64_t fileId, double seconds)
    {
        // Milliseconds are as fine as a pause gets.
        char name[64];
        std::snprintf(name, sizeof name, "%lld-%lld.png", static_cast<long long>(fileId), static_cast<long long>(seconds * 1000.0 + 0.5));
        return Utf8ToWide(name);
    }
}

std::wstring BoardFramePath(const std::wstring &thumbnailFolder, std::int64_t fileId, double seconds)
{
    return FramesFolder(thumbnailFolder) + FrameName(fileId, seconds);
}

void SweepBoardFrames(const std::wstring &thumbnailFolder, const std::vector<board*> &boards)
{
    std::unordered_set<std::wstring> keep;
    for (const board *b : boards)
    {
        for (const board_item &item : b->get_items())
        {
            if (item.frame >= 0.0)
                keep.insert(FrameName(item.file_id, item.frame));
        }
    }
    std::wstring folder = FramesFolder(thumbnailFolder);
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW((folder + L"*.png").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::wstring> gone;
    do
    {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !keep.count(found.cFileName))
            gone.push_back(folder + found.cFileName);
    } while (FindNextFileW(find, &found));
    FindClose(find);
    for (const std::wstring &path : gone)
        DeleteFileW(path.c_str());
}

bool SaveBoardFrame(const std::wstring &target, const std::vector<BYTE> &pixels, UINT width, UINT height)
{
    if (width == 0 || height == 0 || pixels.size() < static_cast<size_t>(width) * height * 4)
        return false;
    size_t slash = target.find_last_of(L'\\');
    if (slash != std::wstring::npos)
        CreateDirectoryW(target.substr(0, slash).c_str(), nullptr);
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    ComPtr<IWICFormatConverter> converter;
    // Written beside it first, so a reader never finds half a file.
    std::wstring partial = target + L".part";
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    bool ok = wic &&
        SUCCEEDED(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4, static_cast<UINT>(pixels.size()),
            const_cast<BYTE*>(pixels.data()), bitmap.GetAddressOf())) &&
        SUCCEEDED(wic->CreateStream(stream.GetAddressOf())) && SUCCEEDED(stream->InitializeFromFilename(partial.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf())) && SUCCEEDED(frame->Initialize(options.Get())) &&
        SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(wic->CreateFormatConverter(converter.GetAddressOf())) &&
        SUCCEEDED(converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    stream.Reset();
    if (ok)
        ok = MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok)
        DeleteFileW(partial.c_str());
    return ok;
}

bool LoadBoardFrame(const std::wstring &source, std::vector<BYTE> &pixels, UINT &width, UINT &height)
{
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (!wic || FAILED(wic->CreateDecoderFromFilename(source.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > 16384 || height > 16384 || FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom)))
        return false;
    pixels.resize(static_cast<size_t>(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
}
