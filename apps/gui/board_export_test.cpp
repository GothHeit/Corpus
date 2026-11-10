// Tests the board's PNG export (board_export.cpp) on pictures drawn here: two solid pictures placed on a canvas, a file
// that is not there (drawn as a plain box), and a picture that fails to decode. Checks the size of the PNG, the
// background and the colors of the files where they were placed, that the export ends with its message, and that
// nothing is written when it is stopped.
//
// Windows only. Usage: corpus-board-export-test [folder for the pictures]

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "board_export.hpp"
#include "ui_util.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static int failures = 0;
static bool gEnded = false, gDone = false;

static void check(bool ok, const std::string &what)
{
    if (!ok)
    {
        printf("FAIL: %s\n", what.c_str());
        failures++;
    }
}

static LRESULT CALLBACK Proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == kBoardExported)
    {
        gEnded = true;
        gDone = wParam != 0;
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// A solid picture as a PNG.
static bool DrawSolid(IWICImagingFactory *wic, const std::wstring &path, UINT width, UINT height, BYTE r, BYTE g, BYTE b)
{
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(wic->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf())) || FAILED(frame->Initialize(options.Get())) ||
        FAILED(frame->SetSize(width, height)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)))
        return false;
    std::vector<BYTE> row(static_cast<size_t>(width) * 4);
    for (UINT x = 0; x < width; x++)
    {
        row[x * 4] = b;
        row[x * 4 + 1] = g;
        row[x * 4 + 2] = r;
        row[x * 4 + 3] = 255;
    }
    for (UINT y = 0; y < height; y++)
    {
        if (FAILED(frame->WritePixels(1, width * 4, static_cast<UINT>(row.size()), row.data())))
            return false;
    }
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

struct Decoded
{
    UINT width = 0, height = 0;
    std::vector<BYTE> pixels;  // BGRA
    bool pixel(UINT x, UINT y, BYTE &r, BYTE &g, BYTE &b) const
    {
        if (x >= width || y >= height)
            return false;
        const BYTE *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
        b = p[0]; g = p[1]; r = p[2];
        return true;
    }
};

static bool Read(IWICImagingFactory *wic, const std::wstring &path, Decoded &out)
{
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&out.width, &out.height)))
        return false;
    out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, out.width * 4, static_cast<UINT>(out.pixels.size()), out.pixels.data()));
}

static bool Run(HWND hWnd, BoardExporter &exporter, const std::wstring &target, std::vector<BoardExportItem> items)
{
    gEnded = gDone = false;
    if (!exporter.Start(hWnd, target, std::move(items)))
        return false;
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    MSG msg;
    while (!gEnded && std::chrono::steady_clock::now() < until)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Sleep(10);
    }
    exporter.Stop();
    return gEnded;
}

static bool Near(BYTE a, int b) { return std::abs(static_cast<int>(a) - b) <= 3; }

int wmain(int argc, wchar_t **argv)
{
    namespace fs = std::filesystem;
    fs::path folder = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / L"corpus-board-export-test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
    check(wic != nullptr, "WIC factory");
    if (!wic)
        return 1;

    WNDCLASSW cls = {};
    cls.lpfnWndProc = Proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"CorpusBoardExportTest";
    RegisterClassW(&cls);
    HWND hWnd = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    check(hWnd != nullptr, "message window");

    std::wstring red = (folder / L"red.png").wstring(), blue = (folder / L"blue.png").wstring();
    check(DrawSolid(wic.Get(), red, 400, 200, 255, 0, 0) && DrawSolid(wic.Get(), blue, 200, 400, 0, 0, 255), "draw the pictures");

    BoardExportItem a;
    a.path = WideToUtf8(red);
    a.x = 0; a.y = 0; a.width = 240; a.height = 120;
    a.picture = true;
    BoardExportItem b;
    b.path = WideToUtf8(blue);
    b.x = 300; b.y = 0; b.width = 120; b.height = 240;
    b.picture = true;
    BoardExportItem gone;
    gone.path = WideToUtf8((folder / L"missing.png").wstring());
    gone.x = 300; gone.y = 400; gone.width = 100; gone.height = 100;
    gone.picture = true;
    BoardExportItem broken;
    fs::path junk = folder / L"junk.png";
    FILE *f = _wfopen(junk.c_str(), L"wb");
    if (f)
    {
        fputs("not a picture", f);
        fclose(f);
    }
    broken.path = WideToUtf8(junk.wstring());
    broken.x = 0; broken.y = 400; broken.width = 100; broken.height = 100;
    broken.picture = true;

    BoardExporter exporter;
    check(!exporter.Start(hWnd, (folder / L"none.png").wstring(), {}), "nothing to draw is refused");

    // Two files: x spans -120..360 and y -120..120 with 48 units around, at 2 pixels a unit.
    std::wstring target = (folder / L"board.png").wstring();
    check(Run(hWnd, exporter, target, { a, b }), "the export ends with its message");
    check(gDone, "the export succeeded");
    Decoded png;
    check(Read(wic.Get(), target, png), "the PNG reads back");
    check(png.width == 1152 && png.height == 672, "size 1152x672, got " + std::to_string(png.width) + "x" + std::to_string(png.height));
    BYTE r, g, bl;
    check(png.pixel(10, 10, r, g, bl) && Near(r, 0x22) && Near(g, 0x1F) && Near(bl, 0x26), "background is the canvas color");
    check(png.pixel(336, 336, r, g, bl) && Near(r, 255) && Near(g, 0) && Near(bl, 0), "the red picture is at the origin");
    check(png.pixel(936, 336, r, g, bl) && Near(r, 0) && Near(g, 0) && Near(bl, 255), "the blue picture is 300 units to the right");
    // Corners of the red picture (240x120 units at 2x = 480x240 px), edge of the box: just outside is background.
    check(png.pixel(336 - 250, 336, r, g, bl) && Near(r, 0x22), "left of the red picture is background");
    check(png.pixel(336 + 100, 336 - 100, r, g, bl) && Near(r, 255), "inside the red picture's top right");

    // A box the canvas never saw the proportions of (square for a 2:1 picture): the picture is fitted, not stretched.
    BoardExportItem square = a;
    square.height = 240;
    std::wstring fitted = (folder / L"board-fit.png").wstring();
    check(Run(hWnd, exporter, fitted, { square }) && gDone, "a square box exports");
    Decoded fit;
    check(Read(wic.Get(), fitted, fit), "the fitted PNG reads back");
    // Box 240x240 units centred on the origin: 480 px square from (96, 96); the 2:1 picture fills its full width and
    // the middle 240 px of its height.
    check(fit.pixel(96 + 240, 96 + 240, r, g, bl) && Near(r, 255), "fitted: the middle is the picture");
    check(fit.pixel(96 + 240, 96 + 40, r, g, bl) && Near(r, 0x22), "fitted: above the picture is background, not stretched");
    check(fit.pixel(96 + 5, 96 + 240, r, g, bl) && Near(r, 255), "fitted: full width");

    // A missing file and a broken picture are boxes, not a failure.
    std::wstring second = (folder / L"board2.png").wstring();
    check(Run(hWnd, exporter, second, { a, gone, broken }), "an export with a missing and a broken file ends");
    check(gDone, "a missing or broken file does not fail the export");
    Decoded other;
    check(Read(wic.Get(), second, other) && other.width > 0, "that PNG reads back");
    BYTE br, bg, bb;
    // Their boxes are the control color, 2 pixels a unit from the corner (-168, -108).
    check(other.pixel(936, 1016, br, bg, bb) && Near(br, 0x2A) && Near(bg, 0x27) && Near(bb, 0x30), "the missing file is a box");
    check(other.pixel(336, 1016, br, bg, bb) && Near(br, 0x2A) && Near(bg, 0x27) && Near(bb, 0x30), "the broken picture is a box");

    // Stopped before it starts writing: no file (a cancelled thread posts nothing, so only the file is checked).
    std::wstring third = (folder / L"board3.png").wstring();
    exporter.Start(hWnd, third, { a, b });
    exporter.Stop();
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
    check(!exporter.Running(), "stopped");

    DestroyWindow(hWnd);
    CoUninitialize();
    if (failures == 0)
        printf("OK\n");
    return failures == 0 ? 0 : 1;
}
