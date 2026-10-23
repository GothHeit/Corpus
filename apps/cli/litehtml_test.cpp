// Checks that the vendored litehtml builds, links and lays out HTML: a short page goes through a stub container
// whose fonts are fixed-width (8 px a byte, 16 px a line), so the expected sizes are easy to work out.
//
// Usage: corpus-litehtml-test

#include <iostream>
#include <string>
#include <vector>

#include <litehtml.h>

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if(!ok)
    {
        std::cout << "FAIL: " << what << '\n';
        failures++;
    }
}

class stub_container : public litehtml::document_container
{
public:
    std::string caption;
    std::vector<std::string> drawn;

    litehtml::uint_ptr create_font(const litehtml::font_description &, const litehtml::document *,
        litehtml::font_metrics *fm) override
    {
        if(fm)
        {
            fm->font_size = 16;
            fm->height = 16;
            fm->ascent = 12;
            fm->descent = 4;
            fm->x_height = 8;
            fm->ch_width = 8;
        }
        return 1;
    }
    void delete_font(litehtml::uint_ptr) override {}
    litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr) override
    {
        return 8.0f * static_cast<float>(std::string(text).size());
    }
    void draw_text(litehtml::uint_ptr, const char *text, litehtml::uint_ptr, litehtml::web_color,
        const litehtml::position &) override
    {
        drawn.emplace_back(text);
    }
    litehtml::pixel_t pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    const char *get_default_font_name() const override { return "serif"; }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker &) override {}
    void load_image(const char *, const char *, bool) override {}
    void get_image_size(const char *, const char *, litehtml::size &sz) override { sz = litehtml::size(); }
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer &, const std::string &,
        const std::string &) override {}
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer &, const litehtml::web_color &) override {}
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::linear_gradient &) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::radial_gradient &) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer &,
        const litehtml::background_layer::conic_gradient &) override {}
    void draw_borders(litehtml::uint_ptr, const litehtml::borders &, const litehtml::position &, bool) override {}
    void set_caption(const char *text) override { caption = text; }
    void set_base_url(const char *) override {}
    void link(const std::shared_ptr<litehtml::document> &, const litehtml::element::ptr &) override {}
    void on_anchor_click(const char *, const litehtml::element::ptr &) override {}
    void on_mouse_event(const litehtml::element::ptr &, litehtml::mouse_event) override {}
    void set_cursor(const char *) override {}
    void transform_text(litehtml::string &, litehtml::text_transform) override {}
    void import_css(litehtml::string &, const litehtml::string &, litehtml::string &) override {}
    void set_clip(const litehtml::position &, const litehtml::border_radiuses &) override {}
    void del_clip() override {}
    void get_viewport(litehtml::position &viewport) const override { viewport = litehtml::position(0, 0, 400, 600); }
    litehtml::element::ptr create_element(const char *, const litehtml::string_map &,
        const std::shared_ptr<litehtml::document> &) override
    {
        return nullptr;
    }
    void get_media_features(litehtml::media_features &media) const override
    {
        media.type = litehtml::media_type_screen;
        media.width = media.device_width = 400;
        media.height = media.device_height = 600;
        media.color = 8;
        media.resolution = 96;
    }
    void get_language(litehtml::string &language, litehtml::string &culture) const override
    {
        language = "en";
        culture.clear();
    }
};

int main()
{
    stub_container container;
    // Ten 4-letter words: 10 * 32 px plus 9 spaces of 8 px = 392 px on one line.
    auto doc = litehtml::document::createFromString(
        "<html><head><title>Chapter 1</title><style>p { margin: 0 }</style></head>"
        "<body style=\"margin: 0\"><p>aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj</p></body></html>",
        &container);
    check(doc != nullptr, "the document is parsed");
    if(!doc)
    {
        std::cout << "FAILED\n";
        return 1;
    }
    check(container.caption == "Chapter 1", "the title reaches the container");

    doc->render(400);
    check(doc->height() == 16, "the paragraph fits on one line at 400 px");

    // At 100 px three words (3 * 32 + 2 * 8 = 112 px) are too wide, so two go on each line: five lines.
    doc->render(100);
    check(doc->height() == 5 * 16, "the paragraph wraps into five lines at 100 px");

    litehtml::position clip(0, 0, 100, 1000);
    doc->draw(0, 0, 0, &clip);
    std::string words;
    for(const std::string &text : container.drawn)
        if(text != " ")
            words += text;
    check(words == "aaaabbbbccccddddeeeeffffgggghhhhiiiijjjj", "every word is drawn, in order");

    std::cout << (failures ? "FAILED" : "OK") << '\n';
    return failures ? 1 : 0;
}
