#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "renderer.hpp"
#include "resource.h"
#include "theme.hpp"
#include "ui_util.hpp"
#include <cmath>

namespace
{
    struct FontStyle
    {
        const wchar_t *family;
        const wchar_t *fallbackFamily;  // when the bundled fonts can't be loaded
        DWRITE_FONT_WEIGHT weight;
        int size;
    };

    // In the order of the Font enum.
    const FontStyle kFontStyles[] =
    {
        { L"Geist",      L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,    theme::kBodySize },
        { L"Geist",      L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, theme::kBodySize },
        { L"Geist",      L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,    theme::kCaptionSize },
        { L"Geist Mono", L"Consolas", DWRITE_FONT_WEIGHT_NORMAL,    theme::kCaptionSize },
    };
    static_assert(sizeof(kFontStyles) / sizeof(kFontStyles[0]) == static_cast<size_t>(Font::kCount),
        "one style per font");

    const int kFontResources[] = { IDR_FONT_GEIST, IDR_FONT_GEIST_SEMIBOLD, IDR_FONT_GEIST_MONO };

    // Snaps to whole pixels, so text and edges stay sharp.
    float Snap(float value)
    {
        return std::floor(value + 0.5f);
    }
}

bool Renderer::Init(HWND hWnd)
{
    m_hWnd = hWnd;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()))))
        return false;

    D2D1_STROKE_STYLE_PROPERTIES round = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    m_d2dFactory->CreateStrokeStyle(round, nullptr, 0, m_roundStroke.GetAddressOf());

    // Dashes and gaps are in multiples of the line width.
    const float dashes[] = { 3.0f, 3.0f };
    D2D1_STROKE_STYLE_PROPERTIES dashed = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
        D2D1_DASH_STYLE_CUSTOM);
    m_d2dFactory->CreateStrokeStyle(dashed, dashes, 2, m_dashedStroke.GetAddressOf());

    LoadFonts();
    return true;
}

// Loads Geist and Geist Mono from the .exe's resources into a font collection of our own, so they don't have
// to be installed. That takes Windows 10 1703 or later; on older versions the text falls back to Segoe UI and
// Consolas.
void Renderer::LoadFonts()
{
    ComPtr<IDWriteFactory5> factory;
    if (FAILED(m_dwriteFactory.As(&factory)))
        return;

    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    if (FAILED(factory->CreateInMemoryFontFileLoader(loader.GetAddressOf())) ||
        FAILED(factory->RegisterFontFileLoader(loader.Get())))
        return;

    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(factory->CreateFontSetBuilder(builder.GetAddressOf())))
        return;

    for (int id : kFontResources)
    {
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
        HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
        const void *data = loaded ? LockResource(loaded) : nullptr;
        if (!data)
            return;

        // With no owner, DirectWrite keeps its own copy of the data.
        ComPtr<IDWriteFontFile> fontFile;
        if (FAILED(loader->CreateInMemoryFontFileReference(factory.Get(), data, SizeofResource(nullptr, resource),
                nullptr, fontFile.GetAddressOf())) ||
            FAILED(builder->AddFontFile(fontFile.Get())))
            return;
    }

    ComPtr<IDWriteFontSet> fontSet;
    ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(builder->CreateFontSet(fontSet.GetAddressOf())) ||
        FAILED(factory->CreateFontCollectionFromFontSet(fontSet.Get(), collection.GetAddressOf())))
        return;

    for (const wchar_t *family : { L"Geist", L"Geist Mono" })
    {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(collection->FindFamilyName(family, &index, &exists)) || !exists)
            return;
    }

    m_fonts = collection;
    m_bundledFonts = true;
}

void Renderer::Resize(UINT width, UINT height)
{
    if (m_target)
        m_target->Resize(D2D1::SizeU(width, height));
}

void Renderer::ReleaseTarget()
{
    m_brush.Reset();
    m_target.Reset();
}

bool Renderer::BeginDraw()
{
    if (!m_d2dFactory)
        return false;

    if (!m_target)
    {
        RECT client;
        GetClientRect(m_hWnd, &client);

        // 96 DPI: one unit is one pixel. IMMEDIATELY: don't wait for the next refresh of the screen.
        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96.0f, 96.0f);
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties = D2D1::HwndRenderTargetProperties(
            m_hWnd, D2D1::SizeU(client.right, client.bottom), D2D1_PRESENT_OPTIONS_IMMEDIATELY);

        if (FAILED(m_d2dFactory->CreateHwndRenderTarget(properties, hwndProperties, m_target.GetAddressOf())) ||
            FAILED(m_target->CreateSolidColorBrush(theme::kText1, m_brush.GetAddressOf())))
        {
            ReleaseTarget();
            return false;
        }
    }

    m_target->BeginDraw();
    return true;
}

void Renderer::EndDraw()
{
    // The graphics device went away (a driver update, a remote desktop session...): start over on the next paint.
    HRESULT result = m_target->EndDraw();
    if (result == static_cast<HRESULT>(D2DERR_RECREATE_TARGET))
    {
        ReleaseTarget();
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

// ---- Shapes ----

void Renderer::Clear(const D2D1_COLOR_F &color)
{
    m_target->Clear(color);
}

void Renderer::FillRect(const D2D1_RECT_F &rect, const D2D1_COLOR_F &color)
{
    m_brush->SetColor(color);
    m_target->FillRectangle(rect, m_brush.Get());
}

void Renderer::FillRoundedRect(const D2D1_RECT_F &rect, float radius, const D2D1_COLOR_F &color)
{
    m_brush->SetColor(color);
    m_target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_brush.Get());
}

void Renderer::DrawRoundedRect(const D2D1_RECT_F &rect, float radius, const D2D1_COLOR_F &color, float width, bool dashed)
{
    m_brush->SetColor(color);
    m_target->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_brush.Get(), width,
        dashed ? m_dashedStroke.Get() : nullptr);
}

void Renderer::FillCircle(D2D1_POINT_2F center, float radius, const D2D1_COLOR_F &color)
{
    m_brush->SetColor(color);
    m_target->FillEllipse(D2D1::Ellipse(center, radius, radius), m_brush.Get());
}

void Renderer::DrawCircle(D2D1_POINT_2F center, float radius, const D2D1_COLOR_F &color, float width)
{
    m_brush->SetColor(color);
    m_target->DrawEllipse(D2D1::Ellipse(center, radius, radius), m_brush.Get(), width);
}

void Renderer::DrawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, const D2D1_COLOR_F &color, float width)
{
    m_brush->SetColor(color);
    m_target->DrawLine(from, to, m_brush.Get(), width, m_roundStroke.Get());
}

void Renderer::DrawPolyline(const D2D1_POINT_2F *points, size_t count, const D2D1_COLOR_F &color, float width, bool closed)
{
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (count < 2 || FAILED(m_d2dFactory->CreatePathGeometry(path.GetAddressOf())) ||
        FAILED(path->Open(sink.GetAddressOf())))
        return;

    sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(points + 1, static_cast<UINT32>(count - 1));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close()))
        return;

    m_brush->SetColor(color);
    m_target->DrawGeometry(path.Get(), m_brush.Get(), width, m_roundStroke.Get());
}

void Renderer::FillPolygon(const D2D1_POINT_2F *points, size_t count, const D2D1_COLOR_F &color)
{
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (count < 3 || FAILED(m_d2dFactory->CreatePathGeometry(path.GetAddressOf())) ||
        FAILED(path->Open(sink.GetAddressOf())))
        return;

    sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(points + 1, static_cast<UINT32>(count - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close()))
        return;

    m_brush->SetColor(color);
    m_target->FillGeometry(path.Get(), m_brush.Get());
}

void Renderer::DrawPath(ID2D1Geometry *path, D2D1_POINT_2F at, const D2D1_COLOR_F &color, float width)
{
    if (!path)
        return;
    m_brush->SetColor(color);
    m_target->SetTransform(D2D1::Matrix3x2F::Translation(at.x, at.y));
    m_target->DrawGeometry(path, m_brush.Get(), width, m_roundStroke.Get());
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Renderer::FillPath(ID2D1Geometry *path, D2D1_POINT_2F at, const D2D1_COLOR_F &color)
{
    if (!path)
        return;
    m_brush->SetColor(color);
    m_target->SetTransform(D2D1::Matrix3x2F::Translation(at.x, at.y));
    m_target->FillGeometry(path, m_brush.Get());
    m_target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Renderer::PushClip(const RECT &rect)
{
    m_target->PushAxisAlignedClip(ToRectF(rect), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Renderer::PopClip()
{
    m_target->PopAxisAlignedClip();
}

// ---- Paths ----

PathBuilder::PathBuilder(ID2D1Factory *factory, bool filled)
    : m_begin(filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW)
{
    if (!factory || FAILED(factory->CreatePathGeometry(m_path.GetAddressOf())) ||
        FAILED(m_path->Open(m_sink.GetAddressOf())))
    {
        m_path.Reset();
        m_sink.Reset();
        return;
    }
    // Shapes that overlap stay filled where they overlap.
    m_sink->SetFillMode(D2D1_FILL_MODE_WINDING);
}

void PathBuilder::Polyline(const D2D1_POINT_2F *points, size_t count, bool closed)
{
    if (!m_sink || count < 2)
        return;
    m_sink->BeginFigure(points[0], m_begin);
    m_sink->AddLines(points + 1, static_cast<UINT32>(count - 1));
    m_sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
}

void PathBuilder::Line(D2D1_POINT_2F from, D2D1_POINT_2F to)
{
    const D2D1_POINT_2F points[] = { from, to };
    Polyline(points, 2);
}

void PathBuilder::Circle(D2D1_POINT_2F center, float radius)
{
    if (!m_sink)
        return;
    // Two half circles: one arc can't end where it starts.
    D2D1_POINT_2F top = D2D1::Point2F(center.x, center.y - radius);
    D2D1_POINT_2F bottom = D2D1::Point2F(center.x, center.y + radius);
    D2D1_SIZE_F size = D2D1::SizeF(radius, radius);
    m_sink->BeginFigure(top, m_begin);
    m_sink->AddArc(D2D1::ArcSegment(bottom, size, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    m_sink->AddArc(D2D1::ArcSegment(top, size, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    m_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

void PathBuilder::Rect(const D2D1_RECT_F &r)
{
    const D2D1_POINT_2F points[] = { { r.left, r.top }, { r.right, r.top }, { r.right, r.bottom }, { r.left, r.bottom } };
    Polyline(points, 4, true);
}

void PathBuilder::RoundedRect(const D2D1_RECT_F &r, float radius)
{
    if (!m_sink)
        return;
    D2D1_SIZE_F size = D2D1::SizeF(radius, radius);
    auto corner = [&](float x, float y)
    {
        m_sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), size, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    m_sink->BeginFigure(D2D1::Point2F(r.left + radius, r.top), m_begin);
    m_sink->AddLine(D2D1::Point2F(r.right - radius, r.top));
    corner(r.right, r.top + radius);
    m_sink->AddLine(D2D1::Point2F(r.right, r.bottom - radius));
    corner(r.right - radius, r.bottom);
    m_sink->AddLine(D2D1::Point2F(r.left + radius, r.bottom));
    corner(r.left, r.bottom - radius);
    m_sink->AddLine(D2D1::Point2F(r.left, r.top + radius));
    corner(r.left + radius, r.top);
    m_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

ComPtr<ID2D1PathGeometry> PathBuilder::Finish()
{
    if (!m_sink || FAILED(m_sink->Close()))
        return ComPtr<ID2D1PathGeometry>();
    m_sink.Reset();
    return m_path;
}

// ---- Text ----

IDWriteTextFormat *Renderer::Format(Font font)
{
    // The window measures its components while it is being created, before Init.
    if (!m_dwriteFactory)
        return nullptr;

    UINT dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0)
        dpi = 96;
    if (dpi != m_formatDpi)
    {
        for (int i = 0; i < static_cast<int>(Font::kCount); i++)
        {
            m_formats[i].Reset();
            m_ellipses[i].Reset();
            m_lineHeights[i] = 0.0f;
        }
        m_formatDpi = dpi;
    }

    int i = static_cast<int>(font);
    if (!m_formats[i])
    {
        const FontStyle &style = kFontStyles[i];
        if (FAILED(m_dwriteFactory->CreateTextFormat(
                m_bundledFonts ? style.family : style.fallbackFamily, m_fonts.Get(), style.weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.size * dpi / 96.0f, L"",
                m_formats[i].GetAddressOf())))
            return nullptr;
        m_formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_dwriteFactory->CreateEllipsisTrimmingSign(m_formats[i].Get(), m_ellipses[i].GetAddressOf());
    }
    return m_formats[i].Get();
}

float Renderer::LineHeight(Font font)
{
    IDWriteTextFormat *format = Format(font);
    int i = static_cast<int>(font);
    if (m_lineHeights[i] == 0.0f && format)
    {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics;
        if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(L"Ag", 2, format, 1000.0f, 1000.0f, layout.GetAddressOf())) &&
            SUCCEEDED(layout->GetMetrics(&metrics)))
            m_lineHeights[i] = metrics.height;
    }
    return m_lineHeights[i];
}

ComPtr<IDWriteTextLayout> Renderer::Layout(const std::wstring &text, Font font, float maxWidth, int maxLines,
    DWRITE_TEXT_ALIGNMENT align, bool ellipsis)
{
    ComPtr<IDWriteTextLayout> layout;
    IDWriteTextFormat *format = Format(font);
    if (!format)
        return layout;

    // A little more than the lines take, so rounding doesn't push the last one out.
    float maxHeight = LineHeight(font) * maxLines + 1.0f;
    if (maxWidth < 0.0f)
        maxWidth = 0.0f;
    if (FAILED(m_dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
            maxWidth, maxHeight, layout.GetAddressOf())))
        return ComPtr<IDWriteTextLayout>();

    // Long names without spaces still break, at any character, instead of running out of the box.
    if (maxLines > 1)
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
    layout->SetTextAlignment(align);
    if (ellipsis)
    {
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        layout->SetTrimming(&trimming, m_ellipses[static_cast<int>(font)].Get());
    }
    return layout;
}

void Renderer::DrawLayout(IDWriteTextLayout *layout, float x, float y, const D2D1_COLOR_F &color)
{
    if (!layout)
        return;
    m_brush->SetColor(color);
    m_target->DrawTextLayout(D2D1::Point2F(Snap(x), Snap(y)), layout, m_brush.Get());
}

void Renderer::DrawTextLine(const std::wstring &text, Font font, const RECT &rect, const D2D1_COLOR_F &color,
    DWRITE_TEXT_ALIGNMENT align)
{
    float width = static_cast<float>(rect.right - rect.left);
    ComPtr<IDWriteTextLayout> layout = Layout(text, font, width, 1, align);
    float y = rect.top + (rect.bottom - rect.top - LineHeight(font)) / 2.0f;
    DrawLayout(layout.Get(), static_cast<float>(rect.left), y, color);
}

float Renderer::TextWidth(const std::wstring &text, Font font)
{
    ComPtr<IDWriteTextLayout> layout = Layout(text, font, 100000.0f, 1, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    DWRITE_TEXT_METRICS metrics;
    if (!layout || FAILED(layout->GetMetrics(&metrics)))
        return 0.0f;
    return metrics.widthIncludingTrailingWhitespace;
}
