#include "../include/audio_depth.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "long_path.hpp"

namespace
{
/// Reads exactly n bytes at the stream's position; false if the file ends first.
bool take(std::istream &in, uint8_t *out, size_t n)
{
    in.read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(n));
    return static_cast<size_t>(in.gcount()) == n;
}

uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t le32(const uint8_t *p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
unsigned be16(const uint8_t *p) { return unsigned(p[0]) << 8 | p[1]; }
unsigned le16(const uint8_t *p) { return unsigned(p[1]) << 8 | p[0]; }

// A 5-bit field in STREAMINFO says "bits per sample minus one".
audio_depth read_flac(std::istream &in)
{
    audio_depth depth;
    uint8_t block[4 + 34];
    // The first metadata block must be STREAMINFO (type 0, 34 bytes).
    if (!take(in, block, sizeof block) || (block[0] & 0x7F) != 0 || (block[1] | block[2]) != 0 || block[3] != 34)
        return depth;
    // STREAMINFO bytes 10..13: rate (20 bits), channels - 1 (3 bits), bits per sample - 1 (5 bits), ...
    const uint8_t *info = block + 4;
    depth.bits = ((info[12] & 1) << 4 | info[13] >> 4) + 1;
    return depth;
}

audio_depth read_wav(std::istream &in)
{
    audio_depth depth;
    uint8_t head[8];
    for (int chunks = 0; chunks < 64 && take(in, head, 8); chunks++)
    {
        uint32_t size = le32(head + 4);
        if (std::memcmp(head, "fmt ", 4) != 0)
        {
            in.seekg(std::streamoff(size) + (size & 1), std::ios::cur);
            continue;
        }
        uint8_t fmt[40];
        size_t have = size < sizeof fmt ? size : sizeof fmt;
        if (have < 16 || !take(in, fmt, have))
            return depth;
        unsigned tag = le16(fmt);
        unsigned container = le16(fmt + 14);
        unsigned bits = container;
        if (tag == 0xFFFE && have >= 26)
        {
            // WAVE_FORMAT_EXTENSIBLE: the valid bits can be fewer than the container; the real format is the first
            // two bytes of the sub-format GUID.
            unsigned valid = le16(fmt + 18);
            if (valid > 0 && valid <= container)
                bits = valid;
            tag = have >= 26 ? le16(fmt + 24) : tag;
        }
        if (tag == 1 || tag == 3)
        {
            depth.bits = static_cast<int>(bits);
            depth.is_float = tag == 3;
        }
        return depth;
    }
    return depth;
}

// An 80-bit extended float is not needed: COMM stores the sample size as a plain 16-bit integer.
audio_depth read_aiff(std::istream &in, bool compressed)
{
    audio_depth depth;
    uint8_t head[8];
    for (int chunks = 0; chunks < 64 && take(in, head, 8); chunks++)
    {
        uint32_t size = be32(head + 4);
        if (std::memcmp(head, "COMM", 4) != 0)
        {
            in.seekg(std::streamoff(size) + (size & 1), std::ios::cur);
            continue;
        }
        uint8_t comm[22];
        size_t want = compressed ? 22 : 18;
        if (size < want || !take(in, comm, want))
            return depth;
        depth.bits = static_cast<int>(be16(comm + 6));
        if (compressed)
        {
            // AIFC: only uncompressed PCM ('NONE', 'sowt') and float ('fl32', 'fl64') declare a depth.
            if (!std::memcmp(comm + 18, "fl32", 4) || !std::memcmp(comm + 18, "FL32", 4))
                depth.is_float = true, depth.bits = 32;
            else if (!std::memcmp(comm + 18, "fl64", 4) || !std::memcmp(comm + 18, "FL64", 4))
                depth.is_float = true, depth.bits = 64;
            else if (std::memcmp(comm + 18, "NONE", 4) && std::memcmp(comm + 18, "sowt", 4) &&
                     std::memcmp(comm + 18, "twos", 4) && std::memcmp(comm + 18, "in24", 4) &&
                     std::memcmp(comm + 18, "in32", 4))
                depth.bits = 0;
        }
        return depth;
    }
    return depth;
}

// Walks the boxes of an MP4 between [pos, end) looking for the ALAC cookie. Returns the depth, or 0.
int find_alac(std::istream &in, uint64_t pos, uint64_t end, int level)
{
    uint8_t head[8];
    for (int boxes = 0; boxes < 256 && pos + 8 <= end; boxes++)
    {
        in.clear();
        in.seekg(std::streamoff(pos));
        if (!take(in, head, 8))
            return 0;
        uint64_t size = be32(head);
        uint64_t header = 8;
        if (size == 1)
        {
            uint8_t big[8];
            if (!take(in, big, 8))
                return 0;
            size = uint64_t(be32(big)) << 32 | be32(big + 4);
            header = 16;
        }
        else if (size == 0)
            size = end - pos;
        if (size < header || pos + size > end)
            return 0;
        uint64_t body = pos + header;
        uint64_t next = pos + size;
        if (level < 8 && (!std::memcmp(head + 4, "moov", 4) || !std::memcmp(head + 4, "trak", 4) ||
                          !std::memcmp(head + 4, "mdia", 4) || !std::memcmp(head + 4, "minf", 4) ||
                          !std::memcmp(head + 4, "stbl", 4)))
        {
            if (int bits = find_alac(in, body, next, level + 1))
                return bits;
        }
        else if (!std::memcmp(head + 4, "stsd", 4))
        {
            // version/flags (4), entry count (4), then the entries: the 'alac' sample entry is a 28-byte audio entry
            // header (after its own 8-byte box header) followed by the 'alac' cookie box.
            if (int bits = find_alac(in, body + 8, next, level + 1))
                return bits;
        }
        else if (!std::memcmp(head + 4, "alac", 4) && size >= header + 28 + 8)
        {
            uint64_t child = body + 28;
            // The sample entry holds the cookie box; the cookie box itself holds version/flags, frameLength (4),
            // compatibleVersion (1), bitDepth (1).
            in.clear();
            in.seekg(std::streamoff(child));
            uint8_t box[8 + 4 + 4 + 1 + 1];
            if (take(in, box, sizeof box) && !std::memcmp(box + 4, "alac", 4) && be32(box) >= sizeof box)
                return box[17];
        }
        pos = next;
    }
    return 0;
}

audio_depth read_alac(std::istream &in)
{
    audio_depth depth;
    in.clear();
    in.seekg(0, std::ios::end);
    std::streamoff length = in.tellg();
    if (length > 0)
        depth.bits = find_alac(in, 0, uint64_t(length), 0);
    return depth;
}

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

audio_depth audio_read_depth(std::istream &in)
{
    uint8_t magic[12];
    if (!take(in, magic, 4))
        return {};
    if (!std::memcmp(magic, "ID3", 3))
    {
        // An ID3v2 tag may precede the FLAC marker: 10-byte header (already read: "ID3" and the version), then
        // revision, flags and a syncsafe size, plus a 10-byte footer when flag bit 4 is set.
        uint8_t rest[6];
        if (!take(in, rest, 6))
            return {};
        uint32_t size = uint32_t(rest[2] & 0x7F) << 21 | uint32_t(rest[3] & 0x7F) << 14 |
                        uint32_t(rest[4] & 0x7F) << 7 | (rest[5] & 0x7F);
        if (rest[1] & 0x10)
            size += 10;
        in.seekg(std::streamoff(size), std::ios::cur);
        if (!take(in, magic, 4) || std::memcmp(magic, "fLaC", 4) != 0)
            return {};
        return read_flac(in);
    }
    if (!std::memcmp(magic, "fLaC", 4))
        return read_flac(in);
    if (!take(in, magic + 4, 8))
        return {};
    if (!std::memcmp(magic, "RIFF", 4) && !std::memcmp(magic + 8, "WAVE", 4))
        return read_wav(in);
    if (!std::memcmp(magic, "FORM", 4) && (!std::memcmp(magic + 8, "AIFF", 4) || !std::memcmp(magic + 8, "AIFC", 4)))
        return read_aiff(in, magic[11] == 'C');
    if (!std::memcmp(magic + 4, "ftyp", 4))
        return read_alac(in);
    return {};
}

audio_depth audio_read_depth_file(const std::string &path)
{
    std::ifstream in = open_file(path);
    return in ? audio_read_depth(in) : audio_depth{};
}
