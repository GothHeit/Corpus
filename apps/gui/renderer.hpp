#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// The type faces of the GUI: Geist, and Geist Mono for counts, sizes and paths.
enum class Font
{
    kBody,           // 14 regular
    kBodyStrong,     // 14 semibold: the title
    kCaption,        // 12 regular: file names, chips
    kCaptionStrong,  // 12 semibold: section titles
    kMono,           // 12 Geist Mono
    kCount
};

// Draws the window with Direct2D and DirectWrite, and measures text for the components.
//
// Coordinates are physical pixels: the render target works at 96 DPI, and components scale with
// ScaleForWindow. Fonts come from the .exe's resources (see LoadFonts).
// Collects lines, circles and rounded rectangles into one Direct2D path, built once and drawn anywhere with one
// call. Coordinates are relative to where it is drawn.
class PathBuilder
{
public:
    // 'filled': the path is for FillPath, otherwise for DrawPath.
    PathBuilder(ID2D1Factory *factory, bool filled);

    void Polyline(const D2D1_POINT_2F *points, size_t count, bool closed = false);
    void Line(D2D1_POINT_2F from, D2D1_POINT_2F to);
    void Circle(D2D1_POINT_2F center, float radius);
    void Rect(const D2D1_RECT_F &rect);
    void RoundedRect(const D2D1_RECT_F &rect, float radius);
    // Ends the path. Returns nullptr if Direct2D failed.
    ComPtr<ID2D1PathGeometry> Finish();

private:
    ComPtr<ID2D1PathGeometry> m_path;
    ComPtr<ID2D1GeometrySink> m_sink;
    D2D1_FIGURE_BEGIN m_begin;
};

class Renderer
{
public:
    Renderer() = default;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    // Creates the factories and loads the fonts. Returns false if Direct2D or DirectWrite isn't available.
    bool Init(HWND hWnd);
    void Resize(UINT width, UINT height);

    // Painting happens between these two. BeginDraw returns false if there is nowhere to draw.
    bool BeginDraw();
    void EndDraw();

    void Clear(const D2D1_COLOR_F &color);
    void FillRect(const D2D1_RECT_F &rect, const D2D1_COLOR_F &color);
    // Shades from 'top' at the top edge to 'bottom' at the bottom one.
    void FillGradient(const D2D1_RECT_F &rect, const D2D1_COLOR_F &top, const D2D1_COLOR_F &bottom);
    void FillRoundedRect(const D2D1_RECT_F &rect, float radius, const D2D1_COLOR_F &color);
    // Strokes are centered on the edge of the rectangle.
    void DrawRoundedRect(const D2D1_RECT_F &rect, float radius, const D2D1_COLOR_F &color, float width, bool dashed = false);
    void FillCircle(D2D1_POINT_2F center, float radius, const D2D1_COLOR_F &color);
    void DrawCircle(D2D1_POINT_2F center, float radius, const D2D1_COLOR_F &color, float width);
    void DrawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, const D2D1_COLOR_F &color, float width);
    // Draws a line through the points, with rounded joins. The figure is closed if 'closed'.
    void DrawPolyline(const D2D1_POINT_2F *points, size_t count, const D2D1_COLOR_F &color, float width, bool closed = false);
    void FillPolygon(const D2D1_POINT_2F *points, size_t count, const D2D1_COLOR_F &color);

    // Draws a path from a PathBuilder with its origin at 'at'. Lines have rounded ends and joins.
    PathBuilder BeginPath(bool filled) { return PathBuilder(m_d2dFactory.Get(), filled); }
    void DrawPath(ID2D1Geometry *path, D2D1_POINT_2F at, const D2D1_COLOR_F &color, float width);
    void FillPath(ID2D1Geometry *path, D2D1_POINT_2F at, const D2D1_COLOR_F &color);

    // ---- Images ----

    // A bitmap from premultiplied BGRA pixels, 4 bytes each, rows top to bottom. Only valid between BeginDraw and
    // EndDraw, as it belongs to the render target; returns nullptr outside them or if Direct2D fails.
    ComPtr<ID2D1Bitmap> CreateBitmap(UINT width, UINT height, const void *pixels);
    // Stretches the bitmap over 'rect', with rounded corners.
    void DrawBitmap(ID2D1Bitmap *bitmap, const D2D1_RECT_F &rect, float radius, float opacity);
    // Stretches the bitmap over 'rect', square. 'smooth' resamples with high quality, for a picture shown smaller
    // than it is; 'blocks' draws each pixel as a square, for one zoomed in; otherwise the pixels are blended linearly.
    void DrawImage(ID2D1Bitmap *bitmap, const D2D1_RECT_F &rect, bool smooth, bool blocks = false);
    // The longest side a bitmap can have on this graphics card.
    UINT MaxBitmapSize() const;
    // Changes whenever the render target is made again: bitmaps made before then can no longer be drawn.
    unsigned Generation() const { return m_generation; }

    // ---- Layers ----

    // Draws into memory instead of the window, for what goes over a video (which the window can't draw over):
    // between BeginLayer and EndLayer, everything lands in a transparent bitmap the size of 'rect', with the
    // window's coordinates, so the same painting code draws into either. Can be used inside BeginDraw and
    // EndDraw. Returns false if Direct2D fails.
    bool BeginLayer(const RECT &rect);
    // Ends the layer, with its pixels in 'pixels': premultiplied BGRA, 4 bytes each, rows top to bottom.
    bool EndLayer(std::vector<BYTE> &pixels);

    // Nothing is drawn outside the rectangle until the matching PopClip.
    void PushClip(const RECT &rect);
    void PopClip();

    // ---- Text ----

    // Lays out text in a box. Text that doesn't fit is cut with an ellipsis if 'ellipsis' is set; up to
    // 'maxLines' lines (1 = no wrapping). Returns nullptr if DirectWrite fails.
    ComPtr<IDWriteTextLayout> Layout(const std::wstring &text, Font font, float maxWidth, int maxLines = 1,
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool ellipsis = true);
    void DrawLayout(IDWriteTextLayout *layout, float x, float y, const D2D1_COLOR_F &color);

    // One line of text, vertically centered in the rectangle.
    void DrawTextLine(const std::wstring &text, Font font, const RECT &rect, const D2D1_COLOR_F &color,
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);

    float TextWidth(const std::wstring &text, Font font);
    // Height of one line, which is also the step between lines.
    float LineHeight(Font font);

private:
    IDWriteTextFormat *Format(Font font);
    void LoadFonts();
    void ReleaseTarget();

    HWND m_hWnd = nullptr;
    ComPtr<ID2D1Factory> m_d2dFactory;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteFontCollection> m_fonts;  // the bundled fonts, or nullptr for the system ones
    ComPtr<ID2D1StrokeStyle> m_roundStroke;
    ComPtr<ID2D1StrokeStyle> m_dashedStroke;
    bool m_bundledFonts = false;

    // Text formats are made for one DPI; a change of monitor makes new ones.
    UINT m_formatDpi = 0;
    ComPtr<IDWriteTextFormat> m_formats[static_cast<int>(Font::kCount)];
    ComPtr<IDWriteInlineObject> m_ellipses[static_cast<int>(Font::kCount)];  // the "..." of each format
    float m_lineHeights[static_cast<int>(Font::kCount)] = {};

    // Device resources, recreated when the graphics device is lost.
    ComPtr<ID2D1HwndRenderTarget> m_hwndTarget;
    ComPtr<ID2D1DeviceContext> m_context;  // the same target, on Windows 8 and later: better scaling of pictures
    ComPtr<ID2D1SolidColorBrush> m_brush;
    unsigned m_generation = 0;

    // What is drawn on: the window's target, or a layer's.
    ComPtr<ID2D1RenderTarget> m_target;
    D2D1_MATRIX_3X2_F m_transform = D2D1::Matrix3x2F::Identity();

    // The layer: a bitmap in memory drawn by Direct2D's software renderer, kept for the next one while it is big
    // enough. While it is drawn on, the window's target waits in the saved pointers.
    ComPtr<IWICImagingFactory> m_wic;
    ComPtr<IWICBitmap> m_layerBitmap;
    ComPtr<ID2D1RenderTarget> m_layerTarget;
    ComPtr<ID2D1SolidColorBrush> m_layerBrush;
    UINT m_layerWidth = 0;
    UINT m_layerHeight = 0;
    RECT m_layerRect = {};
    ComPtr<ID2D1RenderTarget> m_savedTarget;
    ComPtr<ID2D1DeviceContext> m_savedContext;
    ComPtr<ID2D1SolidColorBrush> m_savedBrush;
};

#endif
