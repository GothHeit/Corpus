#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "html_canvas.hpp"
#include "ui_util.hpp"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace
{
    // A picture is read whole into memory before it is decoded, like a comic book's page.
    const size_t kMaxImageBytes = 64u * 1024 * 1024;
    // Past this many bytes of pictures made into bitmaps, the others go before another one is made.
    const size_t kMaxBitmapBytes = 96u * 1024 * 1024;

    // The font CSS names in general terms, as the fonts every Windows has.
    const wchar_t *GenericFamily(const std::string &name)
    {
        if (name == "serif")
            return L"Georgia";
        if (name == "sans-serif" || name == "system-ui" || name == "ui-sans-serif")
            return L"Segoe UI";
        if (name == "monospace" || name == "ui-monospace")
            return L"Consolas";
        if (name == "cursive")
            return L"Segoe Script";
        if (name == "fantasy")
            return L"Impact";
        return nullptr;
    }

    std::string Lowercase(std::string text)
    {
        for (char &c : text)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return text;
    }

    int HexDigit(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    D2D1_COLOR_F Color(const litehtml::web_color &color)
    {
        return D2D1::ColorF(color.red / 255.0f, color.green / 255.0f, color.blue / 255.0f, color.alpha / 255.0f);
    }

    D2D1_RECT_F Rect(const litehtml::position &pos)
    {
        return D2D1::RectF(pos.left(), pos.top(), pos.right(), pos.bottom());
    }

    ComPtr<IStream> MemoryStream(const std::vector<BYTE> &bytes)
    {
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
        return stream;
    }
}

HtmlCanvas::HtmlCanvas(IDWriteFactory *dwrite, IWICImagingFactory *wic, ReadFile read)
    : m_dwrite(dwrite), m_wic(wic), m_read(std::move(read))
{
    m_dwrite->GetSystemFontCollection(m_systemFonts.GetAddressOf());
}

// The documents laid out on the canvas go first: litehtml hands their fonts back as they go.
HtmlCanvas::~HtmlCanvas() = default;

std::string HtmlCanvas::Resolve(const std::string &base, const std::string &href)
{
    std::string link = href.substr(0, href.find_first_of("#?"));
    if (link.empty())
        return href.empty() || href[0] == '#' ? base : std::string();
    // A scheme ("http:", "data:") before any slash: not a file of the book.
    size_t colon = link.find(':');
    if (colon != std::string::npos && colon < link.find('/'))
        return std::string();

    std::string decoded;
    for (size_t i = 0; i < link.size(); i++)
    {
        if (link[i] == '%' && i + 2 < link.size() && HexDigit(link[i + 1]) >= 0 && HexDigit(link[i + 2]) >= 0)
        {
            decoded += static_cast<char>(HexDigit(link[i + 1]) * 16 + HexDigit(link[i + 2]));
            i += 2;
        }
        else
            decoded += link[i] == '\\' ? '/' : link[i];
    }

    std::string joined;
    if (decoded[0] == '/')
        joined = decoded.substr(1);
    else
    {
        size_t slash = base.find_last_of('/');
        joined = (slash == std::string::npos ? std::string() : base.substr(0, slash + 1)) + decoded;
    }

    // "a/./b/../c" is "a/c"; a ".." past the top of the book goes nowhere.
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= joined.size())
    {
        size_t end = joined.find('/', start);
        if (end == std::string::npos)
            end = joined.size();
        std::string part = joined.substr(start, end - start);
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        start = end + 1;
    }
    std::string path;
    for (const std::string &part : parts)
        path += (path.empty() ? "" : "/") + part;
    return path;
}

void HtmlCanvas::SetDocument(const std::string &base, float width, float height, float fontSize)
{
    m_base = base;
    m_width = width;
    m_height = height;
    m_fontSize = fontSize;
}

void HtmlCanvas::SetTarget(ID2D1RenderTarget *target, float scale)
{
    m_target = target;
    m_scale = scale;
    m_brush.Reset();
    if (m_target)
        m_target->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f), m_brush.GetAddressOf());
}

void HtmlCanvas::ForgetBitmaps()
{
    for (auto &image : m_images)
        image.second.bitmap.Reset();
    m_imageBytes = 0;
}

ID2D1SolidColorBrush *HtmlCanvas::Brush(const litehtml::web_color &color)
{
    m_brush->SetColor(Color(color));
    return m_brush.Get();
}

// ---- Fonts and text ----

litehtml::uint_ptr HtmlCanvas::create_font(const litehtml::font_description &descr, const litehtml::document *,
    litehtml::font_metrics *fm)
{
    std::unique_ptr<Font> &font = m_fonts[descr.hash()];
    if (!font)
    {
        font.reset(new Font());
        font->size = descr.size > 0 ? descr.size : m_fontSize;
        font->decoration = descr.decoration_line;
        DWRITE_FONT_WEIGHT weight = static_cast<DWRITE_FONT_WEIGHT>(
            descr.weight > 0 ? std::max(100, std::min(950, descr.weight)) : 400);
        DWRITE_FONT_STYLE style = descr.style == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC :
            DWRITE_FONT_STYLE_NORMAL;

        // The first of the families listed that Windows has; the book's own fonts aren't loaded.
        ComPtr<IDWriteFontFamily> family;
        std::wstring familyName;
        size_t start = 0;
        while (!family && start <= descr.family.size())
        {
            size_t end = descr.family.find(',', start);
            if (end == std::string::npos)
                end = descr.family.size();
            std::string name = descr.family.substr(start, end - start);
            start = end + 1;
            size_t first = name.find_first_not_of(" \t\"'");
            size_t last = name.find_last_not_of(" \t\"'");
            if (first == std::string::npos)
                continue;
            name = name.substr(first, last - first + 1);
            const wchar_t *generic = GenericFamily(Lowercase(name));
            std::wstring wide = generic ? generic : Utf8ToWide(name);
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (m_systemFonts && SUCCEEDED(m_systemFonts->FindFamilyName(wide.c_str(), &index, &exists)) && exists &&
                SUCCEEDED(m_systemFonts->GetFontFamily(index, family.GetAddressOf())))
                familyName = wide;
        }
        if (!family)
        {
            familyName = L"Georgia";
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (m_systemFonts && SUCCEEDED(m_systemFonts->FindFamilyName(familyName.c_str(), &index, &exists)) && exists)
                m_systemFonts->GetFontFamily(index, family.GetAddressOf());
        }

        DWRITE_FONT_METRICS metrics = {};
        ComPtr<IDWriteFont> face;
        if (family && SUCCEEDED(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, style, face.GetAddressOf())))
            face->GetMetrics(&metrics);
        float em = metrics.designUnitsPerEm ? static_cast<float>(metrics.designUnitsPerEm) : 0.0f;
        font->ascent = em > 0.0f ? metrics.ascent * font->size / em : font->size * 0.8f;
        font->descent = em > 0.0f ? metrics.descent * font->size / em : font->size * 0.2f;
        font->xHeight = em > 0.0f ? metrics.xHeight * font->size / em : font->size * 0.5f;
        float gap = em > 0.0f ? metrics.lineGap * font->size / em : 0.0f;

        m_dwrite->CreateTextFormat(familyName.c_str(), nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL, font->size,
            L"", font->format.GetAddressOf());
        if (font->format)
        {
            font->format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            // The baseline exactly 'ascent' under the top, as litehtml places it.
            font->format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, font->ascent + font->descent, font->ascent);
        }
        font->widths.reserve(1024);
        // The gap between lines goes on top, like in a browser.
        font->textAscent = font->ascent;
        font->ascent += gap;
    }
    if (fm)
    {
        fm->font_size = font->size;
        fm->ascent = font->ascent;
        fm->descent = font->descent;
        fm->height = font->ascent + font->descent;
        fm->x_height = font->xHeight;
        fm->ch_width = text_width("0", reinterpret_cast<litehtml::uint_ptr>(font.get()));
        fm->draw_spaces = false;
    }
    return reinterpret_cast<litehtml::uint_ptr>(font.get());
}

// Fonts stay until the canvas goes (create_font).
void HtmlCanvas::delete_font(litehtml::uint_ptr)
{
}

litehtml::pixel_t HtmlCanvas::text_width(const char *text, litehtml::uint_ptr handle)
{
    Font *font = reinterpret_cast<Font *>(handle);
    if (!font || !font->format || !*text)
        return 0.0f;
    auto known = font->widths.find(text);
    if (known != font->widths.end())
        return known->second;
    std::wstring wide = Utf8ToWide(text);
    float width = 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics = {};
    if (SUCCEEDED(m_dwrite->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), font->format.Get(),
            1e6f, 1e6f, layout.GetAddressOf())) && SUCCEEDED(layout->GetMetrics(&metrics)))
        width = metrics.widthIncludingTrailingWhitespace;
    font->widths.emplace(text, width);
    return width;
}

void HtmlCanvas::draw_text(litehtml::uint_ptr, const char *text, litehtml::uint_ptr handle, litehtml::web_color color,
    const litehtml::position &pos)
{
    if (m_boxes)
    {
        m_boxes->push_back({ pos.top(), pos.bottom() });
        return;
    }
    Font *font = reinterpret_cast<Font *>(handle);
    if (!Drawing() || !font || !font->format)
        return;
    std::wstring wide = Utf8ToWide(text);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwrite->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), font->format.Get(),
            1e6f, 1e6f, layout.GetAddressOf())))
        return;
    // litehtml puts the baseline 'descent' above the bottom of the box; the layout puts it 'textAscent' under its
    // top.
    float baseline = pos.bottom() - font->descent;
    ID2D1SolidColorBrush *brush = Brush(color);
    m_target->DrawTextLayout(D2D1::Point2F(pos.left(), baseline - font->textAscent), layout.Get(), brush,
        D2D1_DRAW_TEXT_OPTIONS_NONE);

    float thickness = std::max(1.0f / m_scale, font->size / 16.0f);
    float right = pos.left() + text_width(text, handle);
    if (font->decoration & litehtml::text_decoration_line_underline)
    {
        float y = baseline + std::max(thickness, font->descent * 0.4f);
        m_target->FillRectangle(D2D1::RectF(pos.left(), y, right, y + thickness), brush);
    }
    if (font->decoration & litehtml::text_decoration_line_line_through)
    {
        float y = baseline - font->xHeight / 2.0f;
        m_target->FillRectangle(D2D1::RectF(pos.left(), y - thickness / 2.0f, right, y + thickness / 2.0f), brush);
    }
    if (font->decoration & litehtml::text_decoration_line_overline)
    {
        float y = baseline - font->textAscent;
        m_target->FillRectangle(D2D1::RectF(pos.left(), y, right, y + thickness), brush);
    }
}

void HtmlCanvas::transform_text(litehtml::string &text, litehtml::text_transform tt)
{
    if (tt == litehtml::text_transform_none || text.empty())
        return;
    std::wstring wide = Utf8ToWide(text);
    if (tt == litehtml::text_transform_capitalize)
    {
        // Each word begins with a capital.
        for (size_t i = 0; i < wide.size(); i++)
        {
            if (i == 0 || iswspace(wide[i - 1]))
                LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, &wide[i], 1,
                    &wide[i], 1, nullptr, nullptr, 0);
        }
    }
    else
    {
        DWORD flags = (tt == litehtml::text_transform_uppercase ? LCMAP_UPPERCASE : LCMAP_LOWERCASE) |
            LCMAP_LINGUISTIC_CASING;
        int length = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, wide.c_str(), static_cast<int>(wide.size()),
            nullptr, 0, nullptr, nullptr, 0);
        if (length <= 0)
            return;
        std::wstring mapped(static_cast<size_t>(length), L'\0');
        LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, wide.c_str(), static_cast<int>(wide.size()), &mapped[0], length,
            nullptr, nullptr, 0);
        wide = mapped;
    }
    text = WideToUtf8(wide);
}

// ---- Pictures ----

std::string HtmlCanvas::ImagePath(const char *src, const char *baseurl) const
{
    return Resolve(baseurl && *baseurl ? std::string(baseurl) : m_base, src ? src : "");
}

// Reads a picture's size the first time it is asked for; its pixels wait until it is drawn.
HtmlCanvas::Image &HtmlCanvas::FindImage(const std::string &path)
{
    Image &image = m_images[path];
    if (image.read || path.empty())
        return image;
    image.read = true;
    std::vector<BYTE> bytes;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (m_wic && m_read(path, bytes) && bytes.size() <= kMaxImageBytes && (stream = MemoryStream(bytes)) &&
        SUCCEEDED(m_wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf())) &&
        SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())))
        frame->GetSize(&image.width, &image.height);
    return image;
}

void HtmlCanvas::load_image(const char *src, const char *baseurl, bool)
{
    FindImage(ImagePath(src, baseurl));
}

void HtmlCanvas::get_image_size(const char *src, const char *baseurl, litehtml::size &sz)
{
    const Image &image = FindImage(ImagePath(src, baseurl));
    sz.width = static_cast<litehtml::pixel_t>(image.width);
    sz.height = static_cast<litehtml::pixel_t>(image.height);
}

void HtmlCanvas::draw_image(litehtml::uint_ptr, const litehtml::background_layer &layer, const std::string &url,
    const std::string &base_url)
{
    const litehtml::position &box = layer.origin_box;
    if (m_boxes)
    {
        m_boxes->push_back({ box.top(), box.bottom() });
        return;
    }
    if (!Drawing() || box.width <= 0 || box.height <= 0)
        return;
    std::string path = ImagePath(url.c_str(), base_url.c_str());
    Image &image = FindImage(path);
    if (image.width == 0 || image.height == 0)
        return;

    // Decoded as big as it shows, never bigger than it is.
    UINT width = static_cast<UINT>(std::ceil(box.width * m_scale));
    UINT height = static_cast<UINT>(std::ceil(box.height * m_scale));
    if (!image.bitmap || image.bitmap->GetPixelSize().width + 1 < std::min(width, image.width))
    {
        image.bitmap.Reset();
        if (m_imageBytes > kMaxBitmapBytes)
            ForgetBitmaps();
        std::vector<BYTE> bytes;
        ComPtr<IStream> stream;
        DecodedFrame frame;
        if (!m_read(path, bytes) || bytes.size() > kMaxImageBytes || !(stream = MemoryStream(bytes)) ||
            !DecodePicture(m_wic, stream.Get(), width, std::max(width, height), frame) || frame.width == 0)
            return;
        D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(m_target->CreateBitmap(D2D1::SizeU(frame.width, frame.height), frame.pixels.data(), frame.width * 4,
                properties, image.bitmap.GetAddressOf())))
            return;
        m_imageBytes += frame.pixels.size();
    }
    m_target->PushAxisAlignedClip(Rect(layer.clip_box), D2D1_ANTIALIAS_MODE_ALIASED);
    m_target->DrawBitmap(image.bitmap.Get(), Rect(box), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    m_target->PopAxisAlignedClip();
}

// ---- Boxes ----

void HtmlCanvas::draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker)
{
    if (!marker.image.empty())
    {
        litehtml::background_layer layer;
        layer.origin_box = layer.clip_box = layer.border_box = marker.pos;
        draw_image(hdc, layer, marker.image, marker.baseurl ? marker.baseurl : "");
        return;
    }
    if (!Drawing())
        return;
    const litehtml::position &pos = marker.pos;
    ID2D1SolidColorBrush *brush = Brush(marker.color);
    D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(pos.left() + pos.width / 2.0f, pos.top() + pos.height / 2.0f),
        pos.width / 2.0f, pos.height / 2.0f);
    switch (marker.marker_type)
    {
    case litehtml::list_style_type_none:
        break;
    case litehtml::list_style_type_circle:
        dot.radiusX -= 0.5f;
        dot.radiusY -= 0.5f;
        m_target->DrawEllipse(dot, brush, 1.0f);
        break;
    case litehtml::list_style_type_square:
        m_target->FillRectangle(Rect(pos), brush);
        break;
    default:
        m_target->FillEllipse(dot, brush);
        break;
    }
}

void HtmlCanvas::draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer &layer,
    const litehtml::web_color &color)
{
    if (!Drawing() || color.alpha == 0)
        return;
    m_target->PushAxisAlignedClip(Rect(layer.clip_box), D2D1_ANTIALIAS_MODE_ALIASED);
    m_target->FillRectangle(Rect(layer.border_box), Brush(color));
    m_target->PopAxisAlignedClip();
}

// Plain lines, whatever their style: rules between parts of a chapter, tables.
void HtmlCanvas::draw_borders(litehtml::uint_ptr, const litehtml::borders &borders, const litehtml::position &pos, bool)
{
    if (!Drawing())
        return;
    auto shown = [](const litehtml::border &border)
    {
        return border.width > 0 && border.style != litehtml::border_style_none &&
            border.style != litehtml::border_style_hidden && border.color.alpha > 0;
    };
    float left = pos.left(), top = pos.top(), right = pos.right(), bottom = pos.bottom();
    if (shown(borders.top))
        m_target->FillRectangle(D2D1::RectF(left, top, right, top + borders.top.width), Brush(borders.top.color));
    if (shown(borders.bottom))
        m_target->FillRectangle(D2D1::RectF(left, bottom - borders.bottom.width, right, bottom), Brush(borders.bottom.color));
    if (shown(borders.left))
        m_target->FillRectangle(D2D1::RectF(left, top, left + borders.left.width, bottom), Brush(borders.left.color));
    if (shown(borders.right))
        m_target->FillRectangle(D2D1::RectF(right - borders.right.width, top, right, bottom), Brush(borders.right.color));
}

void HtmlCanvas::set_clip(const litehtml::position &pos, const litehtml::border_radiuses &)
{
    if (Drawing())
        m_target->PushAxisAlignedClip(Rect(pos), D2D1_ANTIALIAS_MODE_ALIASED);
}

void HtmlCanvas::del_clip()
{
    if (Drawing())
        m_target->PopAxisAlignedClip();
}

// ---- The document ----

void HtmlCanvas::import_css(litehtml::string &text, const litehtml::string &url, litehtml::string &baseurl)
{
    std::string path = Resolve(baseurl.empty() ? m_base : baseurl, url);
    std::vector<BYTE> bytes;
    if (path.empty() || !m_read(path, bytes))
        return;
    text.assign(bytes.begin(), bytes.end());
    baseurl = path;
}

void HtmlCanvas::get_viewport(litehtml::position &viewport) const
{
    viewport = litehtml::position(0, 0, m_width, m_height);
}

void HtmlCanvas::get_media_features(litehtml::media_features &media) const
{
    media.type = litehtml::media_type_screen;
    media.width = media.device_width = m_width;
    media.height = media.device_height = m_height;
    media.color = 8;
    media.monochrome = 0;
    media.color_index = 256;
    media.resolution = 96;
}

void HtmlCanvas::get_language(litehtml::string &language, litehtml::string &culture) const
{
    language = "en";
    culture.clear();
}
