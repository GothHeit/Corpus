#include "../include/psd.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>

#include "long_path.hpp"

namespace
{
const uint32_t kMaxPsdSide = 30000;
const uint32_t kMaxPsbSide = 300000;
const uint32_t kMaxThumbnailBytes = 64u << 20;

/// A bounds-checked cursor over the stream. Every read checks the file's length first.
class reader
{
public:
    explicit reader(std::istream &in) : m_in(in)
    {
        m_in.clear();
        m_in.seekg(0, std::ios::end);
        std::streamoff end = m_in.tellg();
        m_size = end < 0 ? 0 : (uint64_t)end;
        m_in.seekg(0, std::ios::beg);
        m_pos = 0;
    }

    uint64_t size() const { return m_size; }
    uint64_t pos() const { return m_pos; }
    uint64_t left() const { return m_pos <= m_size ? m_size - m_pos : 0; }
    bool seek(uint64_t pos)
    {
        if(pos > m_size) return false;
        m_in.clear();
        m_in.seekg((std::streamoff)pos, std::ios::beg);
        m_pos = pos;
        return (bool)m_in;
    }
    bool skip(uint64_t n) { return n <= left() && seek(m_pos + n); }
    bool read(void *out, uint64_t n)
    {
        if(n > left()) return false;
        if(n)
        {
            m_in.read((char*)out, (std::streamsize)n);
            if(!m_in) return false;
        }
        m_pos += n;
        return true;
    }
    bool u16(uint32_t &v)
    {
        uint8_t b[2];
        if(!read(b, 2)) return false;
        v = (uint32_t)b[0] << 8 | b[1];
        return true;
    }
    bool u32(uint32_t &v)
    {
        uint8_t b[4];
        if(!read(b, 4)) return false;
        v = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
        return true;
    }
    bool u64(uint64_t &v)
    {
        uint32_t hi, lo;
        if(!u32(hi) || !u32(lo)) return false;
        v = (uint64_t)hi << 32 | lo;
        return true;
    }

private:
    std::istream &m_in;
    uint64_t m_size = 0;
    uint64_t m_pos = 0;
};

struct header
{
    psd_info info;
    uint64_t colorDataPos = 0;      // the palette bytes, if any
    uint32_t colorDataSize = 0;
    uint64_t resourcesPos = 0;
    uint64_t resourcesSize = 0;
    uint64_t layersPos = 0;
    uint64_t layersSize = 0;
    uint64_t imagePos = 0;          // the merged image's compression field
};

psd_status read_header(reader &r, header &h)
{
    char sig[4];
    uint32_t version, channels, depth, mode;
    if(!r.read(sig, 4) || std::memcmp(sig, "8BPS", 4) != 0) return psd_status::not_psd;
    if(!r.u16(version) || (version != 1 && version != 2)) return psd_status::not_psd;
    if(!r.skip(6)) return psd_status::truncated;
    psd_info &i = h.info;
    i.large = version == 2;
    if(!r.u16(channels) || !r.u32(i.height) || !r.u32(i.width) || !r.u16(depth) || !r.u16(mode)) return psd_status::truncated;
    uint32_t maxSide = i.large ? kMaxPsbSide : kMaxPsdSide;
    if(channels < 1 || channels > 56 || i.width < 1 || i.height < 1 || i.width > maxSide || i.height > maxSide)
        return psd_status::not_psd;
    if(depth != 1 && depth != 8 && depth != 16 && depth != 32) return psd_status::not_psd;
    i.channels = (uint16_t)channels;
    i.depth = (uint16_t)depth;
    i.mode = (psd_mode)mode;

    // Color mode data, image resources, layer and mask info: each a length and its bytes.
    if(!r.u32(h.colorDataSize)) return psd_status::truncated;
    h.colorDataPos = r.pos();
    if(!r.skip(h.colorDataSize)) return psd_status::truncated;
    uint32_t len32;
    if(!r.u32(len32)) return psd_status::truncated;
    h.resourcesPos = r.pos();
    h.resourcesSize = len32;
    if(!r.skip(len32)) return psd_status::truncated;
    if(i.large) { if(!r.u64(h.layersSize)) return psd_status::truncated; }
    else { if(!r.u32(len32)) return psd_status::truncated; h.layersSize = len32; }
    h.layersPos = r.pos();
    if(!r.skip(h.layersSize)) return psd_status::truncated;
    h.imagePos = r.pos();

    // The layer count is the first field of the layer info; negative means the first extra channel of the merged
    // image holds transparency.
    if(h.layersSize > 0)
    {
        uint64_t infoLen = 0;
        uint32_t count = 0;
        r.seek(h.layersPos);
        bool ok = i.large ? r.u64(infoLen) : (r.u32(len32) && ((infoLen = len32), true));
        if(ok && infoLen >= 2 && infoLen <= h.layersSize && r.u16(count))
        {
            int32_t signedCount = (int16_t)count;
            i.layers = signedCount < 0 ? -signedCount : signedCount;
            i.transparency = signedCount < 0;
        }
    }
    return psd_status::ok;
}

/// PackBits: expands src into exactly dstSize bytes. Anything else is corrupt.
bool unpack_bits(const uint8_t *src, size_t srcSize, uint8_t *dst, size_t dstSize)
{
    size_t s = 0, d = 0;
    while(d < dstSize)
    {
        if(s >= srcSize) return false;
        int8_t n = (int8_t)src[s++];
        if(n >= 0)
        {
            size_t run = (size_t)n + 1;
            if(run > srcSize - s || run > dstSize - d) return false;
            std::memcpy(dst + d, src + s, run);
            s += run;
            d += run;
        }
        else if(n != -128)
        {
            size_t run = (size_t)(1 - n);
            if(s >= srcSize || run > dstSize - d) return false;
            std::memset(dst + d, src[s++], run);
            d += run;
        }
    }
    return true;
}

uint8_t srgb_from_linear(float v)
{
    if(!(v > 0.0f)) return 0;           // also NaN
    if(v >= 1.0f) return 255;
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)(s * 255.0f + 0.5f);
}

/// Turns one row of samples (rowBytes, big endian) into 8-bit values.
void row_to_bytes(const uint8_t *row, uint32_t width, uint16_t depth, bool isAlpha, uint8_t *out)
{
    switch(depth)
    {
    case 1:     // 1 = black
        for(uint32_t x = 0; x < width; x++) out[x] = (row[x >> 3] >> (7 - (x & 7))) & 1 ? 0 : 255;
        break;
    case 8:
        std::memcpy(out, row, width);
        break;
    case 16:
        for(uint32_t x = 0; x < width; x++)
        {
            uint32_t v = (uint32_t)row[x * 2] << 8 | row[x * 2 + 1];
            out[x] = (uint8_t)((v * 255 + 32767) / 65535);
        }
        break;
    default:    // 32: big endian float, linear light
        for(uint32_t x = 0; x < width; x++)
        {
            uint32_t bits = (uint32_t)row[x * 4] << 24 | (uint32_t)row[x * 4 + 1] << 16 | (uint32_t)row[x * 4 + 2] << 8 | row[x * 4 + 3];
            float f;
            std::memcpy(&f, &bits, 4);
            // Alpha is coverage, not light: scaled straight, without the sRGB curve.
            out[x] = isAlpha ? (!(f > 0.0f) ? 0 : f >= 1.0f ? 255 : (uint8_t)(f * 255.0f + 0.5f)) : srgb_from_linear(f);
        }
    }
}

/// Resource ids that hold a JPEG thumbnail.
const uint32_t kThumbnail5 = 1036, kThumbnail4 = 1033;
}

const char *psd_describe(psd_status status)
{
    switch(status)
    {
    case psd_status::ok: return "ok";
    case psd_status::not_psd: return "Not a Photoshop file";
    case psd_status::truncated: return "The Photoshop file is cut short";
    case psd_status::corrupt: return "The Photoshop file is damaged";
    case psd_status::unsupported: return "This kind of Photoshop file isn't supported";
    case psd_status::too_large: return "The Photoshop image is too large";
    case psd_status::no_thumbnail: return "The Photoshop file has no thumbnail";
    case psd_status::no_composite: return "The Photoshop file has no merged image";
    }
    return "";
}

psd_status psd_read_info(std::istream &in, psd_info &info)
{
    reader r(in);
    header h;
    psd_status st = read_header(r, h);
    if(st == psd_status::ok) info = h.info;
    return st;
}

psd_status psd_read_thumbnail(std::istream &in, psd_thumbnail &thumb)
{
    reader r(in);
    header h;
    psd_status st = read_header(r, h);
    if(st != psd_status::ok) return st;

    uint64_t pos = h.resourcesPos, end = h.resourcesPos + h.resourcesSize;
    while(pos + 12 <= end)
    {
        char sig[4];
        uint32_t id, nameLen, size;
        r.seek(pos);
        if(!r.read(sig, 4) || std::memcmp(sig, "8BIM", 4) != 0) return psd_status::no_thumbnail;
        uint8_t n;
        if(!r.u16(id) || !r.read(&n, 1)) return psd_status::truncated;
        nameLen = ((1 + (uint32_t)n) + 1) & ~1u;    // the Pascal string, padded so length + text is even
        if(!r.skip(nameLen - 1) || !r.u32(size)) return psd_status::truncated;
        uint64_t data = r.pos();
        if(data > end || size > end - data) return psd_status::corrupt;
        if(id == kThumbnail5 || id == kThumbnail4)
        {
            uint32_t format, w, hgt, rowBytes, total, packed, bpp, planes;
            if(size < 28 || !r.u32(format) || !r.u32(w) || !r.u32(hgt) || !r.u32(rowBytes) || !r.u32(total) ||
               !r.u32(packed) || !r.u16(bpp) || !r.u16(planes)) return psd_status::no_thumbnail;
            (void)rowBytes; (void)total; (void)bpp; (void)planes;
            if(format != 1 || packed == 0 || packed > size - 28 || packed > kMaxThumbnailBytes)
                return psd_status::no_thumbnail;    // format 0 is raw RGB: not worth a decoder for a thumbnail
            thumb.jpeg.resize(packed);
            if(!r.read(thumb.jpeg.data(), packed)) return psd_status::truncated;
            if(thumb.jpeg.size() < 4 || thumb.jpeg[0] != 0xFF || thumb.jpeg[1] != 0xD8) return psd_status::no_thumbnail;
            thumb.width = w;
            thumb.height = hgt;
            thumb.bgr = id == kThumbnail4;
            return psd_status::ok;
        }
        pos = data + size + (size & 1);
    }
    return psd_status::no_thumbnail;
}

psd_status psd_decode_composite(std::istream &in, psd_image &image, uint64_t max_pixels)
{
    reader r(in);
    header h;
    psd_status st = read_header(r, h);
    if(st != psd_status::ok) return st;
    const psd_info &i = h.info;

    // Which variants we decode: how many color channels, and which depths each mode may have.
    uint32_t colorChannels;
    switch(i.mode)
    {
    case psd_mode::bitmap: colorChannels = 1; if(i.depth != 1) return psd_status::unsupported; break;
    case psd_mode::grayscale: case psd_mode::duotone: colorChannels = 1; if(i.depth == 1) return psd_status::unsupported; break;
    case psd_mode::indexed: colorChannels = 1; if(i.depth != 8 || h.colorDataSize != 768) return psd_status::unsupported; break;
    case psd_mode::rgb: colorChannels = 3; if(i.depth == 1) return psd_status::unsupported; break;
    case psd_mode::cmyk: colorChannels = 4; if(i.depth == 1 || i.depth == 32) return psd_status::unsupported; break;
    default: return psd_status::unsupported;    // multichannel, Lab and anything unknown
    }
    if(i.channels < colorChannels) return psd_status::corrupt;
    if(i.depth == 1 && i.mode != psd_mode::bitmap) return psd_status::unsupported;
    if((uint64_t)i.width * i.height > max_pixels) return psd_status::too_large;

    if(h.imagePos >= r.size() || !r.seek(h.imagePos)) return psd_status::no_composite;
    uint32_t compression;
    if(!r.u16(compression)) return psd_status::no_composite;
    if(compression > 3) return psd_status::corrupt;
    if(compression >= 2) return psd_status::unsupported;    // ZIP, with or without prediction
    bool rle = compression == 1;

    const uint32_t w = i.width, hgt = i.height;
    const uint64_t rowBytes = ((uint64_t)w * i.depth + 7) / 8;
    const bool hasAlpha = i.transparency && i.channels > colorChannels;
    const uint32_t used = colorChannels + (hasAlpha ? 1 : 0);   // channels we read; the rest are skipped

    // RLE: a table of compressed row sizes, channels * height entries, before the data.
    std::vector<uint32_t> counts;
    uint64_t dataPos = r.pos();
    if(rle)
    {
        uint64_t entries = (uint64_t)i.channels * hgt, entrySize = i.large ? 4 : 2;
        if(entries * entrySize > r.left()) return psd_status::truncated;
        std::vector<uint8_t> raw((size_t)(entries * entrySize));
        if(!r.read(raw.data(), raw.size())) return psd_status::truncated;
        counts.resize((size_t)entries);
        for(size_t e = 0; e < counts.size(); e++)
            counts[e] = i.large ? (uint32_t)raw[e * 4] << 24 | (uint32_t)raw[e * 4 + 1] << 16 | (uint32_t)raw[e * 4 + 2] << 8 | raw[e * 4 + 3]
                                : (uint32_t)raw[e * 2] << 8 | raw[e * 2 + 1];
        dataPos = r.pos();
        // A row never packs to more than its raw size plus one count byte per 128; refuse anything wild.
        uint64_t limit = rowBytes + rowBytes / 128 + 2;
        uint64_t total = 0;
        for(uint32_t c : counts)
        {
            if(c > limit) return psd_status::corrupt;
            total += c;
        }
        if(total > r.left()) return psd_status::truncated;
    }
    else if((uint64_t)i.channels * hgt * rowBytes > r.left())
    {
        // Photoshop writes no merged image at all when "maximize compatibility" is off and the file is
        // otherwise complete; a file cut short mid-image is the same to us. Tell them apart by how much is there.
        return r.left() == 0 ? psd_status::no_composite : psd_status::truncated;
    }

    // Channel planes land in rgba slots 0..colorChannels-1 (CMYK uses all four); alpha is kept apart.
    const size_t pixels = (size_t)w * hgt;
    std::vector<uint8_t> planes;
    std::vector<uint8_t> alpha;
    try
    {
        planes.assign(pixels * 4, 0);
        if(hasAlpha) alpha.assign(pixels, 255);
    }
    catch(const std::bad_alloc &) { return psd_status::too_large; }

    std::vector<uint8_t> row((size_t)rowBytes), packed, samples(w);
    uint64_t pos = dataPos;
    for(uint32_t c = 0; c < i.channels; c++)
    {
        for(uint32_t y = 0; y < hgt; y++)
        {
            uint64_t length = rle ? counts[(size_t)c * hgt + y] : rowBytes;
            if(c < used)
            {
                if(!r.seek(pos)) return psd_status::truncated;
                if(rle)
                {
                    packed.resize((size_t)length);
                    if(!r.read(packed.data(), length)) return psd_status::truncated;
                    if(!unpack_bits(packed.data(), packed.size(), row.data(), row.size())) return psd_status::corrupt;
                }
                else if(!r.read(row.data(), rowBytes)) return psd_status::truncated;
                row_to_bytes(row.data(), w, i.depth, c >= colorChannels, samples.data());
                if(c < colorChannels)
                {
                    uint8_t *dst = planes.data() + (size_t)y * w * 4 + c;
                    for(uint32_t x = 0; x < w; x++) dst[x * 4] = samples[x];
                }
                else
                    std::memcpy(alpha.data() + (size_t)y * w, samples.data(), w);
            }
            pos += length;
        }
        if(c + 1 >= used && c + 1 >= colorChannels) break;  // nothing past the last channel we use matters
    }

    // Planes to RGBA, in place.
    uint8_t *p = planes.data();
    if(i.mode == psd_mode::cmyk)
    {
        for(size_t k = 0; k < pixels; k++, p += 4)  // stored inverted: 255 = no ink
        {
            uint32_t kk = p[3];
            p[0] = (uint8_t)(p[0] * kk / 255);
            p[1] = (uint8_t)(p[1] * kk / 255);
            p[2] = (uint8_t)(p[2] * kk / 255);
        }
    }
    else if(i.mode == psd_mode::indexed)
    {
        std::vector<uint8_t> palette(768);
        if(!r.seek(h.colorDataPos) || !r.read(palette.data(), 768)) return psd_status::truncated;
        for(size_t k = 0; k < pixels; k++, p += 4)
        {
            uint8_t idx = p[0];
            p[0] = palette[idx];
            p[1] = palette[256 + idx];
            p[2] = palette[512 + idx];
        }
    }
    else if(colorChannels == 1)
    {
        for(size_t k = 0; k < pixels; k++, p += 4) p[1] = p[2] = p[0];
    }
    p = planes.data();
    bool uniform = true;
    for(size_t k = 0; k < pixels; k++, p += 4)
    {
        p[3] = hasAlpha ? alpha[k] : 255;
        if(uniform && k && std::memcmp(p, planes.data(), 4) != 0) uniform = false;
    }

    image.width = w;
    image.height = hgt;
    image.has_alpha = hasAlpha && std::find_if(alpha.begin(), alpha.end(), [](uint8_t a) { return a != 255; }) != alpha.end();
    // A flat white (or empty) merged image over a layered file is the placeholder, not the picture.
    image.blank = uniform && i.layers > 0 && (planes[3] == 0 || (planes[0] == 255 && planes[1] == 255 && planes[2] == 255));
    image.rgba = std::move(planes);
    return psd_status::ok;
}

namespace
{
std::ifstream open_file(const std::string &utf8_path)
{
    std::filesystem::path path = std::filesystem::u8path(utf8_path);
#ifdef _WIN32
    return std::ifstream(std::filesystem::path(long_path(path)), std::ios::binary);
#else
    return std::ifstream(path, std::ios::binary);
#endif
}
}

psd_status psd_read_thumbnail_file(const std::string &path, psd_thumbnail &thumb)
{
    std::ifstream in = open_file(path);
    return in ? psd_read_thumbnail(in, thumb) : psd_status::not_psd;
}

psd_status psd_decode_composite_file(const std::string &path, psd_image &image, uint64_t max_pixels)
{
    std::ifstream in = open_file(path);
    return in ? psd_decode_composite(in, image, max_pixels) : psd_status::not_psd;
}
