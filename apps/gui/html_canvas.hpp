#ifndef HTML_CANVAS_HPP
#define HTML_CANVAS_HPP

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <litehtml.h>
#include "image_decoder.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// The litehtml::document_container for the EPUB reader: fonts and text widths through DirectWrite, drawing with
// Direct2D. Lengths are in CSS pixels (96 DPI). Style sheets and pictures come from the book, through 'read'.
// Measure reports the laid out lines and pictures so a page can end between two lines.
//
// An object lives on one thread, which has COM started.
class HtmlCanvas : public litehtml::document_container
{
public:
    // Reads the file at 'path' in the book (as Resolve makes them); false when there's none.
    using ReadFile = std::function<bool(const std::string &path, std::vector<BYTE> &bytes)>;

    // From the top to the bottom of a line of text, or of a picture.
    struct Box
    {
        float top;
        float bottom;
    };

    HtmlCanvas(IDWriteFactory *dwrite, IWICImagingFactory *wic, ReadFile read);
    ~HtmlCanvas() override;

    // The path of the file 'href' points to, from the file at 'base': "OEBPS/Images/a.jpg" for "../Images/a.jpg"
    // from "OEBPS/Text/1.xhtml", with %-escapes decoded and the #fragment left out. Empty for a link out of the
    // book (http:, mailto:...).
    static std::string Resolve(const std::string &base, const std::string &href);

    // The document laid out next is the file at 'base' (links in it are from there), in a view 'width' x 'height'
    // CSS pixels big, and its body text is 'fontSize' CSS pixels tall.
    void SetDocument(const std::string &base, float width, float height, float fontSize);

    // While 'boxes' is set, drawing a document only collects the boxes of its lines and pictures in there.
    void Measure(std::vector<Box> *boxes) { m_boxes = boxes; }
    // Draws on 'target' from now on, 'scale' screen pixels a CSS pixel; nullptr stops.
    void SetTarget(ID2D1RenderTarget *target, float scale);
    // The pictures made into bitmaps belong to the target they were made on: a new target makes them again.
    void ForgetBitmaps();

    // ---- litehtml::document_container ----
    litehtml::uint_ptr create_font(const litehtml::font_description &descr, const litehtml::document *doc,
        litehtml::font_metrics *fm) override;
    void delete_font(litehtml::uint_ptr font) override;
    litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr font) override;
    void draw_text(litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr font, litehtml::web_color color,
        const litehtml::position &pos) override;
    litehtml::pixel_t pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
    litehtml::pixel_t get_default_font_size() const override { return m_fontSize; }
    const char *get_default_font_name() const override { return "Georgia"; }
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker) override;
    void load_image(const char *src, const char *baseurl, bool redraw_on_ready) override;
    void get_image_size(const char *src, const char *baseurl, litehtml::size &sz) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer &layer, const std::string &url,
        const std::string &base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
        const litehtml::web_color &color) override;
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::linear_gradient &) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::radial_gradient &) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::conic_gradient &) override {}
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders, const litehtml::position &draw_pos,
        bool root) override;
    void set_caption(const char *) override {}
    void set_base_url(const char *) override {}
    void link(const std::shared_ptr<litehtml::document> &, const litehtml::element::ptr &) override {}
    void on_anchor_click(const char *, const litehtml::element::ptr &) override {}
    void on_mouse_event(const litehtml::element::ptr &, litehtml::mouse_event) override {}
    void set_cursor(const char *) override {}
    void transform_text(litehtml::string &text, litehtml::text_transform tt) override;
    void import_css(litehtml::string &text, const litehtml::string &url, litehtml::string &baseurl) override;
    void set_clip(const litehtml::position &pos, const litehtml::border_radiuses &radius) override;
    void del_clip() override;
    void get_viewport(litehtml::position &viewport) const override;
    litehtml::element::ptr create_element(const char *, const litehtml::string_map &,
        const std::shared_ptr<litehtml::document> &) override
    {
        return nullptr;
    }
    void get_media_features(litehtml::media_features &media) const override;
    void get_language(litehtml::string &language, litehtml::string &culture) const override;

private:
    struct Font
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        float ascent = 0.0f;      // with the gap between lines
        float textAscent = 0.0f;  // the font's own
        float descent = 0.0f;
        float xHeight = 0.0f;
        float size = 0.0f;
        int decoration = 0;  // litehtml::text_decoration_line flags
        std::unordered_map<std::string, float> widths;  // by word, measured once
    };

    struct Image
    {
        bool read = false;   // its size was looked for, found or not
        UINT width = 0;      // its own size; 0 when it can't be decoded
        UINT height = 0;
        DecodedFrame frame;  // decoded at the size it was last drawn at, or not yet
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    };

    std::string ImagePath(const char *src, const char *baseurl) const;
    Image &FindImage(const std::string &path);
    bool Drawing() const { return m_target && !m_boxes; }
    ID2D1SolidColorBrush *Brush(const litehtml::web_color &color);

    IDWriteFactory *m_dwrite;
    IWICImagingFactory *m_wic;
    ReadFile m_read;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_systemFonts;

    std::string m_base;
    float m_width = 0.0f;
    float m_height = 0.0f;
    float m_fontSize = 16.0f;

    // Fonts live as long as the canvas, so the widths of words measured for one chapter serve the next ones.
    std::map<std::string, std::unique_ptr<Font>> m_fonts;  // by litehtml::font_description::hash
    std::map<std::string, Image> m_images;                  // by path
    size_t m_imageBytes = 0;                                // decoded pixels kept

    std::vector<Box> *m_boxes = nullptr;
    ID2D1RenderTarget *m_target = nullptr;
    float m_scale = 1.0f;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;
};

#endif
