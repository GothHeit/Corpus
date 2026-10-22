#include "../include/text.hpp"

// Simple lowercase mapping for the scripts most file and tag names use. Each range below is one of two
// layouts in Unicode: a block of capitals followed by its small letters at a fixed offset, or capitals and
// small letters alternating (capital first, on the even or odd code points).
static char32_t to_lower(char32_t c)
{
    // Capital on the even (or odd) code point, small letter right after it.
    auto pair = [c](char32_t first, char32_t last) { return c >= first && c <= last && (c - first) % 2 == 0; };

    if(c < 0x80)
        return c >= 'A' && c <= 'Z' ? c + 32 : c;
    if(c < 0x100)
        return c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
    if(c < 0x250)
    {
        // Latin Extended-A and the regular parts of Extended-B.
        if(c == 0x130)
            return 'i';
        if(c == 0x178)
            return 0xFF;
        // Digraphs: capital, title case (Dž) and small letter in a row.
        if(c == 0x1C4 || c == 0x1C7 || c == 0x1CA || c == 0x1F1)
            return c + 2;
        if(c == 0x1C5 || c == 0x1C8 || c == 0x1CB || c == 0x1F2)
            return c + 1;
        if(pair(0x100, 0x12F) || pair(0x132, 0x137) || pair(0x139, 0x148) || pair(0x14A, 0x177)
            || pair(0x179, 0x17E) || pair(0x1CD, 0x1DC) || pair(0x1DE, 0x1EF) || pair(0x1F8, 0x21F)
            || pair(0x222, 0x233) || pair(0x246, 0x24F))
            return c + 1;
        return c;
    }
    if(c >= 0x370 && c < 0x400)
    {
        // Greek. Final sigma folds to sigma, so a word matches whichever form it ends in.
        if(c == 0x386)
            return 0x3AC;
        if(c >= 0x388 && c <= 0x38A)
            return c + 37;
        if(c == 0x38C)
            return 0x3CC;
        if(c == 0x38E || c == 0x38F)
            return c + 63;
        if((c >= 0x391 && c <= 0x3A1) || (c >= 0x3A3 && c <= 0x3AB))
            return c + 32;
        if(c == 0x3C2)
            return 0x3C3;
        if(pair(0x3D8, 0x3EF))
            return c + 1;
        return c;
    }
    if(c >= 0x400 && c < 0x530)
    {
        // Cyrillic.
        if(c < 0x410)
            return c + 80;
        if(c < 0x430)
            return c + 32;
        if(c == 0x4C0)
            return 0x4CF;
        if(pair(0x460, 0x481) || pair(0x48A, 0x4BF) || pair(0x4C1, 0x4CE) || pair(0x4D0, 0x52F))
            return c + 1;
        return c;
    }
    if(c >= 0x531 && c <= 0x556)
        return c + 48;
    if(c >= 0x1E00 && c < 0x1F00)
    {
        // Latin Extended Additional (Vietnamese and others).
        if(c == 0x1E9E)
            return 0xDF;
        return pair(0x1E00, 0x1E95) || pair(0x1EA0, 0x1EFF) ? c + 1 : c;
    }
    if(c >= 0xFF21 && c <= 0xFF3A)
        return c + 32;
    return c;
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
