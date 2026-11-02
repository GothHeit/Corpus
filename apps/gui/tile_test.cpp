// Tests the viewer's tiled decoding of pictures too big for one bitmap, on pictures drawn here: a 30000x20000 PNG
// and turned (EXIF orientations 6, 8 and 3) TIFFs, one of them big. Checks the overview ImageDecoder makes, then tiles from TileDecoder at
// several levels against the pattern they were drawn from, the partial tiles at the edges, tiles asked for out of
// range, and that a picture opened later drops the tiles queued for the one before.
//
// Windows only. Usage: corpus-tile-test [folder for the pictures]

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "image_decoder.hpp"
#include <psapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if (!ok)
    {
        printf("FAIL: %s\n", what.c_str());
        failures++;
    }
}

static double Seconds(std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

static SIZE_T PeakMegabytes()
{
    PROCESS_MEMORY_COUNTERS counters = {};
    K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    return counters.PeakWorkingSetSize / (1024 * 1024);
}

// Red grows to the right, green downwards, blue makes a checkerboard of 64 pixel squares.
static void Pattern(UINT x, UINT y, UINT width, UINT height, BYTE &r, BYTE &g, BYTE &b)
{
    r = static_cast<BYTE>(static_cast<unsigned long long>(x) * 255 / width);
    g = static_cast<BYTE>(static_cast<unsigned long long>(y) * 255 / height);
    b = ((x / 64 + y / 64) & 1) ? 255 : 0;
}

// Draws the pattern into a file with WIC, a band of rows at a time.
static bool Draw(IWICImagingFactory *wic, const wchar_t *path, REFGUID container, UINT width, UINT height, UINT orientation)
{
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(wic->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(container, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf())))
        return false;
    if (container == GUID_ContainerFormatTiff)
    {
        PROPBAG2 name = {};
        name.pstrName = const_cast<LPOLESTR>(L"TiffCompressionMethod");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_UI1;
        value.bVal = WICTiffCompressionZIP;
        options->Write(1, &name, &value);
    }
    if (FAILED(frame->Initialize(options.Get())) ||
        FAILED(frame->SetSize(width, height)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat24bppBGR)
        return false;
    if (orientation != 1)
    {
        ComPtr<IWICMetadataQueryWriter> writer;
        PROPVARIANT value;
        PropVariantInit(&value);
        value.vt = VT_UI2;
        value.uiVal = static_cast<USHORT>(orientation);
        if (FAILED(frame->GetMetadataQueryWriter(writer.GetAddressOf())) ||
            FAILED(writer->SetMetadataByName(L"/ifd/{ushort=274}", &value)))
            return false;
    }
    const UINT band = 64;
    std::vector<BYTE> rows(static_cast<size_t>(width) * band * 3);
    for (UINT y = 0; y < height; y += band)
    {
        UINT count = std::min(band, height - y);
        for (UINT row = 0; row < count; row++)
            for (UINT x = 0; x < width; x++)
            {
                BYTE *at = rows.data() + (static_cast<size_t>(row) * width + x) * 3;
                Pattern(x, y + row, width, height, at[2], at[1], at[0]);
            }
        if (FAILED(frame->WritePixels(count, width * 3, static_cast<UINT>(rows.size()), rows.data())))
            return false;
    }
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()) && SUCCEEDED(stream->Commit(STGC_DEFAULT));
}

// What the upright picture holds at (x, y): the pattern as drawn, turned the way the orientation says.
static void Expected(UINT orientation, UINT rawWidth, UINT rawHeight, UINT x, UINT y, BYTE &r, BYTE &g, BYTE &b)
{
    UINT w = rawWidth - 1, h = rawHeight - 1, rx = x, ry = y;
    switch (orientation)
    {
    case 2: rx = w - x; break;
    case 3: rx = w - x; ry = h - y; break;
    case 4: ry = h - y; break;
    case 5: rx = y; ry = x; break;
    case 6: rx = y; ry = h - x; break;
    case 7: rx = w - y; ry = h - x; break;
    case 8: rx = w - y; ry = x; break;
    default: break;
    }
    Pattern(rx, ry, rawWidth, rawHeight, r, g, b);
}

static bool WaitFor(ImageDecoder &decoder, DecodedFrame &frame)
{
    auto start = std::chrono::steady_clock::now();
    while (Seconds(start) < 120.0)
    {
        if (decoder.Take(frame) == ImageDecoder::Status::kFrame)
            return true;
        Sleep(10);
    }
    return false;
}

static std::vector<DecodedTile> WaitForTiles(TileDecoder &tiles, size_t count)
{
    std::vector<DecodedTile> got;
    auto start = std::chrono::steady_clock::now();
    while (got.size() < count && Seconds(start) < 120.0)
    {
        tiles.Take(got);
        Sleep(5);
    }
    return got;
}

static TileKey Key(int level, int col, int row)
{
    TileKey key;
    key.level = level;
    key.col = col;
    key.row = row;
    return key;
}

// Compares a tile with the pattern. Level 0 must match exactly; deeper levels are scaled down, so they are compared
// in the middle of the pattern's squares, with room for the averaging.
static void CheckTile(const DecodedTile &tile, UINT orientation, UINT rawWidth, UINT rawHeight, UINT sourceWidth,
    UINT sourceHeight, const std::string &name)
{
    UINT levelWidth = LevelSize(sourceWidth, tile.key.level), levelHeight = LevelSize(sourceHeight, tile.key.level);
    UINT x0 = tile.key.col * kTileSize, y0 = tile.key.row * kTileSize;
    check(tile.width == std::min(kTileSize, levelWidth - x0) && tile.height == std::min(kTileSize, levelHeight - y0),
        name + ": size " + std::to_string(tile.width) + "x" + std::to_string(tile.height));
    check(tile.pixels.size() == static_cast<size_t>(tile.width) * tile.height * 4, name + ": pixel count");
    if (tile.pixels.size() != static_cast<size_t>(tile.width) * tile.height * 4)
        return;
    UINT square = 64u >> tile.key.level;
    int tolerance = tile.key.level == 0 ? 0 : 6;
    int bad = 0, checked = 0;
    for (UINT y = 0; y < tile.height; y++)
        for (UINT x = 0; x < tile.width; x++)
        {
            UINT lx = x0 + x, ly = y0 + y;
            if (tile.key.level > 0 && (lx % square < 3 || lx % square >= square - 3 || ly % square < 3 || ly % square >= square - 3))
                continue;
            BYTE r, g, b;
            Expected(orientation, rawWidth, rawHeight, (lx << tile.key.level) + (1u << tile.key.level) / 2,
                (ly << tile.key.level) + (1u << tile.key.level) / 2, r, g, b);
            const BYTE *at = tile.pixels.data() + (static_cast<size_t>(y) * tile.width + x) * 4;
            checked++;
            if (std::abs(at[0] - b) > tolerance || std::abs(at[1] - g) > tolerance || std::abs(at[2] - r) > tolerance || at[3] != 255)
                bad++;
        }
    check(bad == 0 && checked > 0, name + ": " + std::to_string(bad) + " of " + std::to_string(checked) + " pixels differ");
}

int wmain(int argc, wchar_t **argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
    check(wic != nullptr, "WIC");
    if (!wic)
        return 1;

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring folder = argc > 1 ? argv[1] : temp;
    if (!folder.empty() && folder.back() != L'\\')
        folder += L'\\';
    std::wstring bigPng = folder + L"corpus-tile-test-big.png";
    std::wstring turnedTiff = folder + L"corpus-tile-test-turned.tif";
    std::wstring turnedSmall = folder + L"corpus-tile-test-turned-small.tif";
    std::wstring upsideDown = folder + L"corpus-tile-test-upside-down.tif";

    HWND hWnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ImageDecoder decoder;
    TileDecoder tiles;
    decoder.Start(hWnd);
    tiles.Start(hWnd);

    struct Picture
    {
        const std::wstring &path;
        REFGUID container;
        UINT width, height, orientation, maxSide;
        const char *name;
    };
    // The turned TIFF is small, so a small maxSide makes it tiled too; the PNG is more than 64 megapixels as well.
    Picture pictures[] = {
        { bigPng, GUID_ContainerFormatPng, 30000, 20000, 1, 16384, "30000x20000 PNG" },
        { turnedTiff, GUID_ContainerFormatTiff, 20000, 12000, 6, 4096, "20000x12000 TIFF, orientation 6" },
        { turnedSmall, GUID_ContainerFormatTiff, 3000, 2000, 8, 1024, "3000x2000 TIFF, orientation 8" },
        { upsideDown, GUID_ContainerFormatTiff, 3000, 2000, 3, 1024, "3000x2000 TIFF, orientation 3" },
    };

    for (const Picture &picture : pictures)
    {
        std::string name = picture.name;
        auto start = std::chrono::steady_clock::now();
        bool drawn = Draw(wic.Get(), picture.path.c_str(), picture.container, picture.width, picture.height, picture.orientation);
        check(drawn, name + ": drawing the file");
        if (!drawn)
            continue;
        printf("%s: drawn in %.1f s\n", picture.name, Seconds(start));

        UINT upW = picture.orientation >= 5 ? picture.height : picture.width;
        UINT upH = picture.orientation >= 5 ? picture.width : picture.height;

        start = std::chrono::steady_clock::now();
        decoder.Open(picture.path, picture.maxSide);
        DecodedFrame frame;
        check(WaitFor(decoder, frame), name + ": overview decoded");
        printf("%s: overview %ux%u of %ux%u, tiled=%d, in %.1f s, peak %lu MB\n", picture.name, frame.width, frame.height,
            frame.sourceWidth, frame.sourceHeight, frame.tiled, Seconds(start), static_cast<unsigned long>(PeakMegabytes()));
        check(frame.tiled && frame.sourceWidth == upW && frame.sourceHeight == upH, name + ": overview is tiled with the upright size");
        check(frame.width <= picture.maxSide && frame.height <= picture.maxSide, name + ": overview fits the bitmap limit");
        std::vector<BYTE>().swap(frame.pixels);

        // Tiles at level 0, 1 and 2: a middle one, then the edge ones (partial), in one request so rows are read together.
        tiles.Open(picture.path, upW, upH);
        std::vector<TileKey> asked;
        for (int level = 0; level <= 2; level++)
        {
            int cols = static_cast<int>((LevelSize(upW, level) + kTileSize - 1) / kTileSize);
            int rows = static_cast<int>((LevelSize(upH, level) + kTileSize - 1) / kTileSize);
            asked.push_back(Key(level, cols / 2, rows / 2));
            asked.push_back(Key(level, cols / 2 + (cols > 2 ? 1 : 0), rows / 2));
            asked.push_back(Key(level, cols - 1, rows - 1));
            asked.push_back(Key(level, 0, 0));
        }
        asked.push_back(Key(0, 100000, 0));  // out of range: comes back empty
        start = std::chrono::steady_clock::now();
        tiles.Want(asked);
        std::vector<DecodedTile> got = WaitForTiles(tiles, asked.size());
        printf("%s: %zu tiles in %.1f s, peak %lu MB\n", picture.name, got.size(), Seconds(start),
            static_cast<unsigned long>(PeakMegabytes()));
        check(got.size() == asked.size(), name + ": every tile answered");
        for (const DecodedTile &tile : got)
        {
            std::string label = name + " level " + std::to_string(tile.key.level) + " tile " + std::to_string(tile.key.col) +
                "," + std::to_string(tile.key.row);
            if (tile.key.col == 100000)
                check(tile.pixels.empty() && tile.width == 0, label + ": out of range is empty");
            else
                CheckTile(tile, picture.orientation, picture.width, picture.height, upW, upH, label);
        }

        // Tiles queued for a picture that is closed never arrive.
        tiles.Want({ Key(0, 0, 0), Key(0, 1, 0), Key(0, 2, 0), Key(0, 3, 0) });
        tiles.Close();
        Sleep(500);
        std::vector<DecodedTile> late;
        tiles.Take(late);
        check(late.empty(), name + ": nothing arrives after Close");
    }

    tiles.Stop();
    decoder.Stop();
    DeleteFileW(bigPng.c_str());
    DeleteFileW(turnedTiff.c_str());
    DeleteFileW(turnedSmall.c_str());
    DeleteFileW(upsideDown.c_str());
    printf(failures == 0 ? "OK\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
