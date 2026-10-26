// Unit tests for the PSD reader, on tiny files built here: raw and PackBits data, depths 1/8/16/32, the color
// modes, transparency, the JPEG thumbnail resource, PSB, and truncated or hostile files.
//
// Usage: corpus-psd-test

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../include/psd.hpp"

static int failures = 0;

static void check(bool ok, const std::string &what)
{
    if(!ok)
    {
        std::cout << "FAIL: " << what << '\n';
        failures++;
    }
}

typedef std::vector<uint8_t> bytes;

static void put16(bytes &b, uint32_t v) { b.push_back(v >> 8); b.push_back(v); }
static void put32(bytes &b, uint32_t v) { put16(b, v >> 16); put16(b, v & 0xFFFF); }
static void put_text(bytes &b, const char *s) { while(*s) b.push_back((uint8_t)*s++); }

/// PackBits for one row, mixing literal and repeat runs.
static bytes pack(const bytes &row)
{
    bytes out;
    size_t i = 0;
    while(i < row.size())
    {
        size_t run = 1;
        while(i + run < row.size() && row[i + run] == row[i] && run < 128) run++;
        if(run >= 2)
        {
            out.push_back((uint8_t)(1 - (int)run));
            out.push_back(row[i]);
            i += run;
        }
        else
        {
            size_t n = 0;
            while(i + n < row.size() && n < 128 && (i + n + 1 >= row.size() || row[i + n] != row[i + n + 1])) n++;
            out.push_back((uint8_t)(n - 1));
            out.insert(out.end(), row.begin() + i, row.begin() + i + n);
            i += n;
        }
    }
    return out;
}

struct spec
{
    bool psb = false;
    uint32_t w = 2, h = 2, channels = 3, depth = 8, mode = 3;
    int layers = 0;             // written as the layer count (negative = transparency)
    bool rle = false;
    int compression = -1;       // overrides rle when set
    bytes palette;              // color mode data
    bytes thumbnail;            // JPEG bytes: adds resource 1036
    bool bgrThumbnail = false;
    bool noImage = false;       // omit the image data section
    bytes planes;               // channels * h * rowBytes of raw data
};

static bytes build(const spec &s)
{
    bytes b;
    put_text(b, "8BPS");
    put16(b, s.psb ? 2 : 1);
    b.insert(b.end(), 6, 0);
    put16(b, s.channels);
    put32(b, s.h);
    put32(b, s.w);
    put16(b, s.depth);
    put16(b, s.mode);
    put32(b, (uint32_t)s.palette.size());
    b.insert(b.end(), s.palette.begin(), s.palette.end());

    bytes res;
    // An unrelated resource first (odd size, to exercise the padding), then the thumbnail.
    put_text(res, "8BIM"); put16(res, 1005); res.push_back(0); res.push_back(0); put32(res, 3); res.insert(res.end(), {1, 2, 3, 0});
    if(!s.thumbnail.empty())
    {
        put_text(res, "8BIM"); put16(res, s.bgrThumbnail ? 1033 : 1036);
        res.push_back(2); res.push_back('h'); res.push_back('i'); res.push_back(0);   // Pascal name "hi", padded
        put32(res, 28 + (uint32_t)s.thumbnail.size());
        put32(res, 1); put32(res, 160); put32(res, 120); put32(res, 640); put32(res, 76800);
        put32(res, (uint32_t)s.thumbnail.size()); put16(res, 24); put16(res, 1);
        res.insert(res.end(), s.thumbnail.begin(), s.thumbnail.end());
        if(s.thumbnail.size() & 1) res.push_back(0);
    }
    put32(b, (uint32_t)res.size());
    b.insert(b.end(), res.begin(), res.end());

    bytes layer;    // layer info: length + count (no layer records: the reader only needs the count)
    if(s.layers != 0)
    {
        bytes info;
        put16(info, (uint32_t)(s.layers & 0xFFFF));
        if(s.psb) { put32(layer, 0); put32(layer, (uint32_t)info.size()); } else put32(layer, (uint32_t)info.size());
        layer.insert(layer.end(), info.begin(), info.end());
    }
    if(s.psb) { put32(b, 0); put32(b, (uint32_t)layer.size()); } else put32(b, (uint32_t)layer.size());
    b.insert(b.end(), layer.begin(), layer.end());

    if(s.noImage) return b;
    int comp = s.compression >= 0 ? s.compression : (s.rle ? 1 : 0);
    put16(b, comp);
    size_t rowBytes = (s.w * s.depth + 7) / 8;
    if(comp == 1)
    {
        bytes data;
        for(uint32_t r = 0; r < s.channels * s.h; r++)
        {
            bytes row(s.planes.begin() + r * rowBytes, s.planes.begin() + (r + 1) * rowBytes);
            bytes p = pack(row);
            if(s.psb) put32(b, (uint32_t)p.size()); else put16(b, (uint32_t)p.size());
            data.insert(data.end(), p.begin(), p.end());
        }
        b.insert(b.end(), data.begin(), data.end());
    }
    else
        b.insert(b.end(), s.planes.begin(), s.planes.end());
    return b;
}

static psd_status decode(const bytes &b, psd_image &img, uint64_t max_pixels = 100000000)
{
    std::istringstream in(std::string(b.begin(), b.end()));
    return psd_decode_composite(in, img, max_pixels);
}

static bool pixel(const psd_image &img, uint32_t x, uint32_t y, int r, int g, int bl, int a = 255)
{
    const uint8_t *p = &img.rgba[((size_t)y * img.width + x) * 4];
    return p[0] == r && p[1] == g && p[2] == bl && p[3] == a;
}

/// A 2x2 RGB image: red, green / blue, white (planes: R, G, B).
static spec rgb2x2()
{
    spec s;
    s.planes = {255, 0, 0, 255,     // R
                0, 255, 0, 255,     // G
                0, 0, 255, 255};    // B
    return s;
}

static void test_rgb(bool rle, bool psb)
{
    std::string name = std::string(rle ? "rle" : "raw") + (psb ? " psb" : " psd");
    spec s = rgb2x2();
    s.rle = rle;
    s.psb = psb;
    psd_image img;
    check(decode(build(s), img) == psd_status::ok, name + " rgb decodes");
    check(img.width == 2 && img.height == 2 && img.rgba.size() == 16, name + " rgb size");
    check(pixel(img, 0, 0, 255, 0, 0) && pixel(img, 1, 0, 0, 255, 0) && pixel(img, 0, 1, 0, 0, 255) && pixel(img, 1, 1, 255, 255, 255),
          name + " rgb pixels");
    check(!img.has_alpha && !img.blank, name + " rgb flags");
    psd_info info;
    bytes file = build(s);
    std::istringstream in(std::string(file.begin(), file.end()));
    check(psd_read_info(in, info) == psd_status::ok && info.large == psb && info.depth == 8 && info.channels == 3, name + " info");
}

static void test_depths_and_modes()
{
    psd_image img;
    {   // 16-bit gray, one row of 3: 0, 0x8000, 0xFFFF
        spec s; s.w = 3; s.h = 1; s.channels = 1; s.depth = 16; s.mode = 1;
        s.planes = {0, 0, 0x80, 0, 0xFF, 0xFF};
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 0, 0, 0) && pixel(img, 1, 0, 128, 128, 128) && pixel(img, 2, 0, 255, 255, 255), "16-bit gray");
        s.rle = true;
        check(decode(build(s), img) == psd_status::ok && pixel(img, 1, 0, 128, 128, 128), "16-bit gray rle");
    }
    {   // 16-bit RGB, one pixel
        spec s; s.w = 1; s.h = 1; s.depth = 16;
        s.planes = {0xFF, 0xFF, 0, 0, 0x80, 0};
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 255, 0, 128), "16-bit rgb");
    }
    {   // 32-bit gray: 0.0 and 1.0 and 0.5 (linear -> sRGB 188)
        spec s; s.w = 3; s.h = 1; s.channels = 1; s.depth = 32; s.mode = 1;
        s.planes = {0, 0, 0, 0,  0x3F, 0x80, 0, 0,  0x3F, 0, 0, 0};
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 0, 0, 0) && pixel(img, 1, 0, 255, 255, 255) && pixel(img, 2, 0, 188, 188, 188), "32-bit gray");
    }
    {   // 32-bit RGB with transparency: alpha 0.5 is 128 (linear), not the sRGB-curved 188
        spec s; s.w = 1; s.h = 1; s.channels = 4; s.depth = 32; s.layers = -1;
        s.planes = {0x3F, 0x80, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0x3F, 0, 0, 0};
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 255, 0, 0, 128), "32-bit alpha is linear");
    }
    {   // CMYK, stored inverted: no ink = white, full K = black, full C = cyan
        spec s; s.w = 3; s.h = 1; s.channels = 4; s.mode = 4;
        s.planes = {255, 0, 255,    // C
                    255, 255, 255,  // M
                    255, 255, 255,  // Y
                    255, 0, 255};   // K
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 255, 255, 255) && pixel(img, 1, 0, 0, 0, 0) && pixel(img, 2, 0, 255, 255, 255), "cmyk");
        s.planes = {0, 255, 255,  255, 255, 255,  255, 255, 255,  255, 255, 255};
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 0, 255, 255), "cmyk cyan");
    }
    {   // indexed
        spec s; s.w = 2; s.h = 1; s.channels = 1; s.mode = 2;
        s.palette.assign(768, 0);
        s.palette[0] = 10; s.palette[256] = 20; s.palette[512] = 30;    // index 0
        s.palette[1] = 40; s.palette[257] = 50; s.palette[513] = 60;    // index 1
        s.planes = {1, 0};
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 40, 50, 60) && pixel(img, 1, 0, 10, 20, 30), "indexed");
    }
    {   // bitmap: 10 pixels wide, row = 2 bytes; 1 = black
        spec s; s.w = 10; s.h = 1; s.channels = 1; s.depth = 1; s.mode = 0;
        s.planes = {0xA0, 0x40};    // 1010 0000 01..
        check(decode(build(s), img) == psd_status::ok && pixel(img, 0, 0, 0, 0, 0) && pixel(img, 1, 0, 255, 255, 255) && pixel(img, 2, 0, 0, 0, 0) &&
              pixel(img, 8, 0, 255, 255, 255) && pixel(img, 9, 0, 0, 0, 0), "bitmap");
        s.rle = true;
        check(decode(build(s), img) == psd_status::ok && pixel(img, 9, 0, 0, 0, 0), "bitmap rle");
    }
}

static void test_alpha_and_blank()
{
    psd_image img;
    spec s; s.w = 2; s.h = 1; s.channels = 4; s.layers = -1;
    s.planes = {255, 0,  0, 255,  0, 0,  128, 0};   // R G B A
    check(decode(build(s), img) == psd_status::ok && img.has_alpha && pixel(img, 0, 0, 255, 0, 0, 128) && pixel(img, 1, 0, 0, 255, 0, 0), "transparency channel");

    s.layers = 2;   // a fourth channel that isn't transparency (a saved selection) stays ignored
    check(decode(build(s), img) == psd_status::ok && !img.has_alpha && pixel(img, 0, 0, 255, 0, 0), "extra channel ignored");

    spec white; white.layers = 3;
    white.planes.assign(12, 255);
    check(decode(build(white), img) == psd_status::ok && img.blank, "white composite over layers is blank");
    white.layers = 0;
    check(decode(build(white), img) == psd_status::ok && !img.blank, "white flat image is not blank");

    spec none = rgb2x2(); none.layers = 3; none.noImage = true;
    check(decode(build(none), img) == psd_status::no_composite, "missing image data section");
}

static void test_thumbnail()
{
    spec s = rgb2x2();
    s.thumbnail = {0xFF, 0xD8, 0x01, 0x02, 0x03, 0xFF, 0xD9};
    bytes file = build(s);
    psd_thumbnail t;
    std::istringstream in(std::string(file.begin(), file.end()));
    check(psd_read_thumbnail(in, t) == psd_status::ok && t.jpeg == s.thumbnail && t.width == 160 && t.height == 120 && !t.bgr, "thumbnail 1036");
    s.bgrThumbnail = true;
    file = build(s);
    std::istringstream in2(std::string(file.begin(), file.end()));
    check(psd_read_thumbnail(in2, t) == psd_status::ok && t.bgr, "thumbnail 1033 is bgr");
    s.thumbnail.clear();
    file = build(s);
    std::istringstream in3(std::string(file.begin(), file.end()));
    check(psd_read_thumbnail(in3, t) == psd_status::no_thumbnail, "no thumbnail");
}

static void test_rejects()
{
    psd_image img;
    spec s = rgb2x2();
    s.compression = 2;
    check(decode(build(s), img) == psd_status::unsupported, "zip unsupported");
    s.compression = 9;
    check(decode(build(s), img) == psd_status::corrupt, "unknown compression");
    spec lab = rgb2x2(); lab.mode = 9;
    check(decode(build(lab), img) == psd_status::unsupported, "lab unsupported");
    check(decode(build(rgb2x2()), img, 3) == psd_status::too_large, "max_pixels");
    check(decode(bytes{'8', 'B', 'P', 'X'}, img) == psd_status::not_psd, "bad signature");
    check(decode(bytes{}, img) == psd_status::not_psd, "empty file");

    spec big = rgb2x2(); big.w = 30001;       // over the PSD limit
    check(decode(build(big), img) == psd_status::not_psd, "psd width cap");
    spec huge = rgb2x2(); huge.psb = true; huge.w = 300000; huge.h = 300000;
    check(decode(build(huge), img) == psd_status::too_large, "huge psb refused before allocating");
    spec wide = rgb2x2(); wide.w = 30000; wide.h = 30000; wide.rle = false;
    check(decode(build(wide), img) == psd_status::too_large, "30000x30000 refused");
    wide.planes.clear();
    check(decode(build(wide), img, 1000000000) == psd_status::no_composite, "big header with no data is refused, not allocated");

    // A PackBits row mangled so it no longer fits its row.
    spec rle = rgb2x2(); rle.rle = true;
    bytes file = build(rle);
    bytes bad = file;
    for(size_t i = bad.size() - 1; i > 0; i--) if(bad[i] == 0xFF) { bad[i] = 0x7F; break; }  // turns a repeat run into a long literal
    {
        psd_status st = decode(bad, img);
        check(st == psd_status::corrupt || st == psd_status::truncated || st == psd_status::ok, "mangled rle never crashes");
    }
}

/// Every prefix of every kind of file must come back as an error (or, for a cut that keeps the whole image,
/// ok), without crashing; run under a sanitizer this is the memory safety check.
static void test_truncations()
{
    std::vector<spec> specs;
    spec a = rgb2x2(); a.thumbnail = {0xFF, 0xD8, 1, 2, 0xFF, 0xD9}; specs.push_back(a);
    a.rle = true; specs.push_back(a);
    a.psb = true; specs.push_back(a);
    spec c; c.channels = 4; c.mode = 4; c.planes.assign(16, 200); specs.push_back(c);
    spec ix; ix.channels = 1; ix.mode = 2; ix.palette.assign(768, 7); ix.planes.assign(4, 1); specs.push_back(ix);
    spec d16 = rgb2x2(); d16.depth = 16; d16.planes.assign(24, 9); d16.layers = -1; d16.channels = 4; d16.planes.assign(32, 9); specs.push_back(d16);
    for(const spec &s : specs)
    {
        bytes full = build(s);
        for(size_t n = 0; n < full.size(); n++)
        {
            bytes cut(full.begin(), full.begin() + n);
            psd_image img;
            psd_thumbnail t;
            psd_info info;
            std::istringstream i1(std::string(cut.begin(), cut.end())), i2(std::string(cut.begin(), cut.end())), i3(std::string(cut.begin(), cut.end()));
            psd_status st = decode(cut, img);
            psd_read_thumbnail(i2, t);
            psd_read_info(i3, info);
            if(st == psd_status::ok) { check(false, "truncated file decoded ok at " + std::to_string(n)); break; }
        }
        psd_image img;
        check(decode(full, img) == psd_status::ok, "full file decodes");
    }
    // Flip every byte of a file in turn: whatever it becomes, no crash.
    bytes full = build(specs[1]);
    for(size_t i = 0; i < full.size(); i++)
        for(uint8_t v : {(uint8_t)0, (uint8_t)0x80, (uint8_t)0xFF})
        {
            bytes m = full;
            m[i] = v;
            psd_image img;
            psd_thumbnail t;
            std::istringstream in(std::string(m.begin(), m.end()));
            decode(m, img, 1000000);
            psd_read_thumbnail(in, t);
        }
}

int main()
{
    test_rgb(false, false);
    test_rgb(true, false);
    test_rgb(false, true);
    test_rgb(true, true);
    test_depths_and_modes();
    test_alpha_and_blank();
    test_thumbnail();
    test_rejects();
    test_truncations();
    if(failures) { std::cout << failures << " failure(s)\n"; return 1; }
    std::cout << "psd tests ok\n";
    return 0;
}
