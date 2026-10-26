#ifndef PSD_HPP
#define PSD_HPP

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

/// @brief Reader for Photoshop documents (.psd, and .psb "large document" files). It never executes anything
/// from the file and checks every size against the file's length, so truncated or hostile files only come back
/// as an error. Every function takes a seekable binary stream (an ifstream or an istringstream) and reads only
/// what it needs: the thumbnail comes from the first few KB, the composite from the end of the file.

enum class psd_status
{
    ok,
    not_psd,        // no "8BPS" signature or an impossible header
    truncated,      // the file ends before the data a header field promises
    corrupt,        // sizes that don't add up, a compressed row that overflows its row, ...
    unsupported,    // a valid file in a variant this reader doesn't decode (see psd_describe)
    too_large,      // more pixels than max_pixels
    no_thumbnail,   // psd_read_thumbnail: the file has no JPEG thumbnail resource
    no_composite    // psd_decode_composite: the file has no merged image ("maximize compatibility" was off)
};

/// @brief A short English sentence for a status, for a tooltip or a log line.
const char *psd_describe(psd_status status);

enum class psd_mode : uint16_t
{
    bitmap = 0, grayscale = 1, indexed = 2, rgb = 3, cmyk = 4, multichannel = 7, duotone = 8, lab = 9
};

struct psd_info
{
    bool large = false;     // .psb (version 2)
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t channels = 0;
    uint16_t depth = 0;     // bits per channel: 1, 8, 16 or 32
    psd_mode mode = psd_mode::rgb;
    int32_t layers = 0;     // layer count (0 = flat image)
    bool transparency = false;  // the merged image's first extra channel is its transparency
};

/// @brief Reads the header (and the layer count). Fills info on ok.
psd_status psd_read_info(std::istream &in, psd_info &info);

struct psd_thumbnail
{
    std::vector<uint8_t> jpeg;  // a complete JPEG file, hand it to any JPEG decoder
    uint32_t width = 0;         // its size in pixels (usually at most 160 wide)
    uint32_t height = 0;
    bool bgr = false;           // resource 1033 (Photoshop 4): the channels are stored blue first, so swap R and B
};

/// @brief Finds the embedded JPEG thumbnail (image resource 1036, else 1033). Cheap: reads the resource
/// section only. no_thumbnail when the file has none or it isn't JPEG.
psd_status psd_read_thumbnail(std::istream &in, psd_thumbnail &thumb);

struct psd_image
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, straight (not premultiplied) alpha, row 0 on top
    bool has_alpha = false;     // false: every alpha byte is 255
    /// The merged image is a single flat color while the file has layers, which is what Photoshop writes
    /// with "maximize compatibility" off. The caller should prefer the thumbnail or the type icon.
    bool blank = false;
};

/// @brief Decodes the merged ("flattened") image to 8-bit RGBA. Supports raw and PackBits compression, depths
/// 1, 8, 16 and 32, modes bitmap, grayscale, duotone (as gray), indexed, RGB and CMYK (a plain conversion, the
/// embedded color profile is ignored). ZIP-compressed merged images, Lab and multichannel are unsupported.
/// Images over max_pixels come back as too_large without allocating.
psd_status psd_decode_composite(std::istream &in, psd_image &image, uint64_t max_pixels = 100000000);

/// @brief Convenience: opens path and runs the function above. not_psd when the file can't be opened.
psd_status psd_read_thumbnail_file(const std::string &path, psd_thumbnail &thumb);
psd_status psd_decode_composite_file(const std::string &path, psd_image &image, uint64_t max_pixels = 100000000);

#endif
