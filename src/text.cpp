#include <algorithm>
#include <cstdint>
#include <iterator>

#include "../include/text.hpp"

// One run of Unicode's simple lowercase mapping: the capitals from first to last, every one (step 1) or every
// other one (step 2, capitals and small letters alternating), are delta code points away from their small letter.
struct lower_run
{
    char32_t first, last;
    std::int32_t delta;
    std::uint8_t step;
};

// Every capital and title case letter of Unicode 15.1 above U+00FF, in order. Two differ from Unicode so that
// more spellings match: İ (U+0130) folds to a plain i, and final sigma (U+03C2) to sigma, so a word matches
// whichever form it ends in.
static const lower_run lower_runs[] = {
    {0x0100, 0x012E, 1, 2}, {0x0130, 0x0130, -199, 1}, {0x0132, 0x0136, 1, 2}, {0x0139, 0x0147, 1, 2},
    {0x014A, 0x0176, 1, 2}, {0x0178, 0x0178, -121, 1}, {0x0179, 0x017D, 1, 2}, {0x0181, 0x0181, 210, 1},
    {0x0182, 0x0184, 1, 2}, {0x0186, 0x0186, 206, 1}, {0x0187, 0x0187, 1, 1}, {0x0189, 0x018A, 205, 1},
    {0x018B, 0x018B, 1, 1}, {0x018E, 0x018E, 79, 1}, {0x018F, 0x018F, 202, 1}, {0x0190, 0x0190, 203, 1},
    {0x0191, 0x0191, 1, 1}, {0x0193, 0x0193, 205, 1}, {0x0194, 0x0194, 207, 1}, {0x0196, 0x0196, 211, 1},
    {0x0197, 0x0197, 209, 1}, {0x0198, 0x0198, 1, 1}, {0x019C, 0x019C, 211, 1}, {0x019D, 0x019D, 213, 1},
    {0x019F, 0x019F, 214, 1}, {0x01A0, 0x01A4, 1, 2}, {0x01A6, 0x01A6, 218, 1}, {0x01A7, 0x01A7, 1, 1},
    {0x01A9, 0x01A9, 218, 1}, {0x01AC, 0x01AC, 1, 1}, {0x01AE, 0x01AE, 218, 1}, {0x01AF, 0x01AF, 1, 1},
    {0x01B1, 0x01B2, 217, 1}, {0x01B3, 0x01B5, 1, 2}, {0x01B7, 0x01B7, 219, 1}, {0x01B8, 0x01B8, 1, 1},
    {0x01BC, 0x01BC, 1, 1}, {0x01C4, 0x01C4, 2, 1}, {0x01C5, 0x01C5, 1, 1}, {0x01C7, 0x01C7, 2, 1},
    {0x01C8, 0x01C8, 1, 1}, {0x01CA, 0x01CA, 2, 1}, {0x01CB, 0x01DB, 1, 2}, {0x01DE, 0x01EE, 1, 2},
    {0x01F1, 0x01F1, 2, 1}, {0x01F2, 0x01F4, 1, 2}, {0x01F6, 0x01F6, -97, 1}, {0x01F7, 0x01F7, -56, 1},
    {0x01F8, 0x021E, 1, 2}, {0x0220, 0x0220, -130, 1}, {0x0222, 0x0232, 1, 2}, {0x023A, 0x023A, 10795, 1},
    {0x023B, 0x023B, 1, 1}, {0x023D, 0x023D, -163, 1}, {0x023E, 0x023E, 10792, 1}, {0x0241, 0x0241, 1, 1},
    {0x0243, 0x0243, -195, 1}, {0x0244, 0x0244, 69, 1}, {0x0245, 0x0245, 71, 1}, {0x0246, 0x024E, 1, 2},
    {0x0370, 0x0372, 1, 2}, {0x0376, 0x0376, 1, 1}, {0x037F, 0x037F, 116, 1}, {0x0386, 0x0386, 38, 1},
    {0x0388, 0x038A, 37, 1}, {0x038C, 0x038C, 64, 1}, {0x038E, 0x038F, 63, 1}, {0x0391, 0x03A1, 32, 1},
    {0x03A3, 0x03AB, 32, 1}, {0x03C2, 0x03C2, 1, 1}, {0x03CF, 0x03CF, 8, 1}, {0x03D8, 0x03EE, 1, 2},
    {0x03F4, 0x03F4, -60, 1}, {0x03F7, 0x03F7, 1, 1}, {0x03F9, 0x03F9, -7, 1}, {0x03FA, 0x03FA, 1, 1},
    {0x03FD, 0x03FF, -130, 1}, {0x0400, 0x040F, 80, 1}, {0x0410, 0x042F, 32, 1}, {0x0460, 0x0480, 1, 2},
    {0x048A, 0x04BE, 1, 2}, {0x04C0, 0x04C0, 15, 1}, {0x04C1, 0x04CD, 1, 2}, {0x04D0, 0x052E, 1, 2},
    {0x0531, 0x0556, 48, 1}, {0x10A0, 0x10C5, 7264, 1}, {0x10C7, 0x10C7, 7264, 1}, {0x10CD, 0x10CD, 7264, 1},
    {0x13A0, 0x13EF, 38864, 1}, {0x13F0, 0x13F5, 8, 1}, {0x1C90, 0x1CBA, -3008, 1}, {0x1CBD, 0x1CBF, -3008, 1},
    {0x1E00, 0x1E94, 1, 2}, {0x1E9E, 0x1E9E, -7615, 1}, {0x1EA0, 0x1EFE, 1, 2}, {0x1F08, 0x1F0F, -8, 1},
    {0x1F18, 0x1F1D, -8, 1}, {0x1F28, 0x1F2F, -8, 1}, {0x1F38, 0x1F3F, -8, 1}, {0x1F48, 0x1F4D, -8, 1},
    {0x1F59, 0x1F5F, -8, 2}, {0x1F68, 0x1F6F, -8, 1}, {0x1F88, 0x1F8F, -8, 1}, {0x1F98, 0x1F9F, -8, 1},
    {0x1FA8, 0x1FAF, -8, 1}, {0x1FB8, 0x1FB9, -8, 1}, {0x1FBA, 0x1FBB, -74, 1}, {0x1FBC, 0x1FBC, -9, 1},
    {0x1FC8, 0x1FCB, -86, 1}, {0x1FCC, 0x1FCC, -9, 1}, {0x1FD8, 0x1FD9, -8, 1}, {0x1FDA, 0x1FDB, -100, 1},
    {0x1FE8, 0x1FE9, -8, 1}, {0x1FEA, 0x1FEB, -112, 1}, {0x1FEC, 0x1FEC, -7, 1}, {0x1FF8, 0x1FF9, -128, 1},
    {0x1FFA, 0x1FFB, -126, 1}, {0x1FFC, 0x1FFC, -9, 1}, {0x2126, 0x2126, -7517, 1}, {0x212A, 0x212A, -8383, 1},
    {0x212B, 0x212B, -8262, 1}, {0x2132, 0x2132, 28, 1}, {0x2160, 0x216F, 16, 1}, {0x2183, 0x2183, 1, 1},
    {0x24B6, 0x24CF, 26, 1}, {0x2C00, 0x2C2F, 48, 1}, {0x2C60, 0x2C60, 1, 1}, {0x2C62, 0x2C62, -10743, 1},
    {0x2C63, 0x2C63, -3814, 1}, {0x2C64, 0x2C64, -10727, 1}, {0x2C67, 0x2C6B, 1, 2}, {0x2C6D, 0x2C6D, -10780, 1},
    {0x2C6E, 0x2C6E, -10749, 1}, {0x2C6F, 0x2C6F, -10783, 1}, {0x2C70, 0x2C70, -10782, 1}, {0x2C72, 0x2C72, 1, 1},
    {0x2C75, 0x2C75, 1, 1}, {0x2C7E, 0x2C7F, -10815, 1}, {0x2C80, 0x2CE2, 1, 2}, {0x2CEB, 0x2CED, 1, 2},
    {0x2CF2, 0x2CF2, 1, 1}, {0xA640, 0xA66C, 1, 2}, {0xA680, 0xA69A, 1, 2}, {0xA722, 0xA72E, 1, 2},
    {0xA732, 0xA76E, 1, 2}, {0xA779, 0xA77B, 1, 2}, {0xA77D, 0xA77D, -35332, 1}, {0xA77E, 0xA786, 1, 2},
    {0xA78B, 0xA78B, 1, 1}, {0xA78D, 0xA78D, -42280, 1}, {0xA790, 0xA792, 1, 2}, {0xA796, 0xA7A8, 1, 2},
    {0xA7AA, 0xA7AA, -42308, 1}, {0xA7AB, 0xA7AB, -42319, 1}, {0xA7AC, 0xA7AC, -42315, 1},
    {0xA7AD, 0xA7AD, -42305, 1}, {0xA7AE, 0xA7AE, -42308, 1}, {0xA7B0, 0xA7B0, -42258, 1},
    {0xA7B1, 0xA7B1, -42282, 1}, {0xA7B2, 0xA7B2, -42261, 1}, {0xA7B3, 0xA7B3, 928, 1}, {0xA7B4, 0xA7C2, 1, 2},
    {0xA7C4, 0xA7C4, -48, 1}, {0xA7C5, 0xA7C5, -42307, 1}, {0xA7C6, 0xA7C6, -35384, 1}, {0xA7C7, 0xA7C9, 1, 2},
    {0xA7D0, 0xA7D0, 1, 1}, {0xA7D6, 0xA7D8, 1, 2}, {0xA7F5, 0xA7F5, 1, 1}, {0xFF21, 0xFF3A, 32, 1},
    {0x10400, 0x10427, 40, 1}, {0x104B0, 0x104D3, 40, 1}, {0x10570, 0x1057A, 39, 1}, {0x1057C, 0x1058A, 39, 1},
    {0x1058C, 0x10592, 39, 1}, {0x10594, 0x10595, 39, 1}, {0x10C80, 0x10CB2, 64, 1}, {0x118A0, 0x118BF, 32, 1},
    {0x16E40, 0x16E5F, 32, 1}, {0x1E900, 0x1E921, 34, 1},
};

static char32_t to_lower(char32_t c)
{
    if(c < 0x100)
        return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ? c + 32 : c;
    const lower_run *end = std::end(lower_runs);
    const lower_run *run = std::lower_bound(std::begin(lower_runs), end, c,
        [](const lower_run &r, char32_t value) { return r.last < value; });
    if(run == end || c < run->first || (c - run->first) % run->step != 0)
        return c;
    return static_cast<char32_t>(static_cast<std::int32_t>(c) + run->delta);
}

static void append_utf8(std::string &out, char32_t c)
{
    if(c < 0x80)
        out += static_cast<char>(c);
    else if(c < 0x800)
    {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else if(c < 0x10000)
    {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string fold_case(const std::string &utf8)
{
    std::string out;
    out.reserve(utf8.size());
    for(std::size_t i = 0; i < utf8.size(); )
    {
        unsigned char lead = static_cast<unsigned char>(utf8[i]);
        if(lead < 0x80)
        {
            out += static_cast<char>(lead >= 'A' && lead <= 'Z' ? lead + 32 : lead);
            i++;
            continue;
        }
        // Decode one sequence; anything malformed (stray continuation byte, cut short, overlong) is copied.
        int length = lead >= 0xF0 && lead < 0xF5 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC2 && lead < 0xE0 ? 2 : 0;
        if(lead >= 0xF5)
            length = 0;
        char32_t c = length == 2 ? lead & 0x1F : length == 3 ? lead & 0x0F : lead & 0x07;
        bool valid = length > 0 && i + length <= utf8.size();
        for(int k = 1; valid && k < length; k++)
        {
            unsigned char next = static_cast<unsigned char>(utf8[i + k]);
            valid = (next & 0xC0) == 0x80;
            c = (c << 6) | (next & 0x3F);
        }
        if(valid && ((length == 3 && c < 0x800) || (length == 4 && (c < 0x10000 || c > 0x10FFFF))))
            valid = false;
        if(!valid)
        {
            out += utf8[i];
            i++;
            continue;
        }
        append_utf8(out, to_lower(c));
        i += length;
    }
    return out;
}
