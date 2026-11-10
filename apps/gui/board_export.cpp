#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "board_export.hpp"
#include "image_decoder.hpp"
#include "theme.hpp"
#include "ui_util.hpp"
#include <d2d1.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace
{
    const double kMargin = 48.0;        // canvas units around the files
    const double kPixelsPerUnit = 2.0;  // at most
    const double kMaxSide = 8192.0;
    const double kMaxPixels = 64e6;
    const UINT kShellSize = 512;

    // Windows' own thumbnail of a file as premultiplied BGRA rows from the top.
    bool ShellPixels(const std::wstring &path, UINT &width, UINT &height, std::vector<BYTE> &pixels)
    {
        ComPtr<IShellItemImageFactory> factory;
        HBITMAP bitmap = nullptr;
        if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(factory.GetAddressOf()))) ||
            FAILED(factory->GetImage(SIZE{ static_cast<LONG>(kShellSize), static_cast<LONG>(kShellSize) }, SIIGBF_THUMBNAILONLY, &bitmap)))
            return false;
        BITMAP info;
        bool ok = GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth > 0 && info.bmHeight != 0;
        if (ok)
        {
            width = static_cast<UINT>(info.bmWidth);
            height = static_cast<UINT>(info.bmHeight < 0 ? -info.bmHeight : info.bmHeight);
            BITMAPINFO format = {};
            format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            format.bmiHeader.biWidth = static_cast<LONG>(width);
            format.bmiHeader.biHeight = -static_cast<LONG>(height);
            format.bmiHeader.biPlanes = 1;
            format.bmiHeader.biBitCount = 32;
            format.bmiHeader.biCompression = BI_RGB;
            pixels.resize(static_cast<size_t>(width) * height * 4);
            HDC dc = CreateCompatibleDC(nullptr);
            int lines = dc ? GetDIBits(dc, bitmap, 0, height, pixels.data(), &format, DIB_RGB_COLORS) : 0;
            if (dc)
                DeleteDC(dc);
            ok = lines == static_cast<int>(height);
        }
        DeleteObject(bitmap);
        if (!ok)
            return false;
        // As the grid reads them: no alpha at all means opaque; a color brighter than its alpha means straight alpha.
        bool anyAlpha = false, straight = false;
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            BYTE a = pixels[i + 3];
            anyAlpha |= a != 0;
            straight |= pixels[i] > a || pixels[i + 1] > a || pixels[i + 2] > a;
        }
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            if (!anyAlpha)
                pixels[i + 3] = 255;
            else if (straight)
            {
                for (int c = 0; c < 3; c++)
                    pixels[i + c] = static_cast<BYTE>((pixels[i + c] * pixels[i + 3] + 127) / 255);
            }
        }
        return true;
    }

    bool PicturePixels(IWICImagingFactory *wic, const BoardExportItem &item, const std::wstring &path, UINT wantWidth, UINT wantHeight,
        UINT &width, UINT &height, std::vector<BYTE> &pixels)
    {
        DecodedFrame frame;
        UINT side = std::max(wantWidth, wantHeight);
        bool ok = false;
        if (item.psd)
            ok = DecodePsd(wic, item.path, side, frame);
        else if (item.picture)
        {
            ComPtr<IStream> stream;
            ok = SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr,
                stream.GetAddressOf())) && DecodePicture(wic, stream.Get(), wantWidth, side, frame);
        }
        if (!ok || frame.width == 0 || frame.height == 0 || frame.pixels.size() < static_cast<size_t>(frame.width) * frame.height * 4)
            return false;
        width = frame.width;
        height = frame.height;
        pixels = std::move(frame.pixels);
        return true;
    }

    bool WritePng(IWICImagingFactory *wic, IWICBitmap *bitmap, UINT width, UINT height, const std::wstring &target)
    {
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> options;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(wic->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromFilename(target.c_str(), GENERIC_WRITE)) ||
            FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf())) || FAILED(frame->Initialize(options.Get())) ||
            FAILED(frame->SetSize(width, height)))
            return false;
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;  // the background is opaque
        if (FAILED(frame->SetPixelFormat(&format)) ||
            FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
            FAILED(converter->Initialize(bitmap, format, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
            FAILED(frame->WriteSource(converter.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
            return false;
        return true;
    }
}

bool BoardExporter::Start(HWND hWnd, const std::wstring &target, std::vector<BoardExportItem> items)
{
    if (m_running || items.empty())
        return false;
    Stop();
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_running = true;
    m_thread = std::thread(&BoardExporter::Run, hWnd, target, std::move(items), m_cancel, &m_running);
    return true;
}

void BoardExporter::Stop()
{
    m_cancel->store(true);
    if (m_thread.joinable())
        m_thread.join();
}

void BoardExporter::Run(HWND hWnd, std::wstring target, std::vector<BoardExportItem> items, std::shared_ptr<std::atomic<bool>> cancel,
    std::atomic<bool> *running)
{
    bool done = false;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<ID2D1Factory> d2d;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) &&
            SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())))
        {
            double l = 1e300, t = 1e300, r = -1e300, b = -1e300;
            for (const BoardExportItem &item : items)
            {
                l = std::min(l, item.x - item.width / 2);
                t = std::min(t, item.y - item.height / 2);
                r = std::max(r, item.x + item.width / 2);
                b = std::max(b, item.y + item.height / 2);
            }
            l -= kMargin; t -= kMargin; r += kMargin; b += kMargin;
            double w = r - l, h = b - t;
            double scale = std::min({ kPixelsPerUnit, kMaxSide / w, kMaxSide / h, std::sqrt(kMaxPixels / (w * h)) });
            UINT width = std::max(1u, static_cast<UINT>(std::ceil(w * scale))), height = std::max(1u, static_cast<UINT>(std::ceil(h * scale)));

            ComPtr<IWICBitmap> bitmap;
            ComPtr<ID2D1RenderTarget> target2d;
            if (SUCCEEDED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, bitmap.GetAddressOf())) &&
                SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(),
                    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f),
                    target2d.GetAddressOf())))
            {
                target2d->BeginDraw();
                target2d->Clear(theme::kLayer);
                ComPtr<ID2D1SolidColorBrush> brush;
                target2d->CreateSolidColorBrush(theme::kControl, brush.GetAddressOf());
                for (const BoardExportItem &item : items)
                {
                    if (cancel->load())
                        break;
                    D2D1_RECT_F box = D2D1::RectF(static_cast<float>((item.x - item.width / 2 - l) * scale),
                        static_cast<float>((item.y - item.height / 2 - t) * scale), static_cast<float>((item.x + item.width / 2 - l) * scale),
                        static_cast<float>((item.y + item.height / 2 - t) * scale));
                    UINT pw = 0, ph = 0;
                    std::vector<BYTE> pixels;
                    std::wstring path = Utf8ToWide(item.path);
                    UINT wantWidth = std::max(1u, static_cast<UINT>(std::ceil(box.right - box.left)));
                    UINT wantHeight = std::max(1u, static_cast<UINT>(std::ceil(box.bottom - box.top)));
                    bool have = PicturePixels(wic.Get(), item, path, wantWidth, wantHeight, pw, ph, pixels) ||
                        ShellPixels(path, pw, ph, pixels);
                    ComPtr<ID2D1Bitmap> picture;
                    if (have && SUCCEEDED(target2d->CreateBitmap(D2D1::SizeU(pw, ph), pixels.data(), pw * 4,
                            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                            picture.GetAddressOf())))
                    {
                        // The picture keeps its own proportions inside the box (the canvas may not have known them
                        // yet), centred.
                        float boxW = box.right - box.left, boxH = box.bottom - box.top;
                        float fit = std::min(boxW / pw, boxH / ph);
                        float w = pw * fit, h = ph * fit;
                        D2D1_RECT_F drawn = D2D1::RectF(box.left + (boxW - w) / 2, box.top + (boxH - h) / 2,
                            box.left + (boxW + w) / 2, box.top + (boxH + h) / 2);
                        target2d->DrawBitmap(picture.Get(), drawn, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    }
                    else if (brush)
                        target2d->FillRectangle(box, brush.Get());
                }
                bool drawn = SUCCEEDED(target2d->EndDraw());
                if (drawn && !cancel->load())
                {
                    done = WritePng(wic.Get(), bitmap.Get(), width, height, target);
                    if (!done)
                        DeleteFileW(target.c_str());
                }
            }
        }
        CoUninitialize();
    }
    running->store(false);
    if (!cancel->load())
        PostMessage(hWnd, kBoardExported, done ? 1 : 0, 0);
}
