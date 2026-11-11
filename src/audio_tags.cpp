#include "../include/audio_tags.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "long_path.hpp"

namespace
{
const size_t kMaxText = 512;             // bytes kept per field
const uint64_t kMaxFrame = 64 * 1024;    // a text frame/comment/atom longer than this is not a title
const uint64_t kMaxUnsync = 16u << 20;   // the biggest ID3v2 tag read whole to undo unsynchronisation
const size_t kMaxComment = 256 * 1024;   // the part of an Ogg comment packet that is kept

typedef std::vector<uint8_t> bytes;

uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t le32(const uint8_t *p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
uint32_t be24(const uint8_t *p) { return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]; }
uint32_t syncsafe(const uint8_t *p)
{
    return uint32_t(p[0] & 0x7F) << 21 | uint32_t(p[1] & 0x7F) << 14 | uint32_t(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}

/// Random access over a stream: reads n bytes at an absolute offset, false if the file ends first.
struct source
{
    std::istream &in;
    uint64_t length = 0;

    explicit source(std::istream &s) : in(s)
    {
        in.clear();
        in.seekg(0, std::ios::end);
        std::streamoff end = in.tellg();
        length = end > 0 ? uint64_t(end) : 0;
    }

    bool read(uint64_t pos, void *out, size_t n)
    {
        if (pos > length || n > length - pos)
            return false;
        in.clear();
        in.seekg(std::streamoff(pos), std::ios::beg);
        in.read(static_cast<char *>(out), std::streamsize(n));
        return in && size_t(in.gcount()) == n;
    }
};

// ---- Text ----

void append_utf8(std::string &out, uint32_t c)
{
    if (c < 0x80)
        out += char(c);
    else if (c < 0x800)
    {
        out += char(0xC0 | c >> 6);
        out += char(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        out += char(0xE0 | c >> 12);
        out += char(0x80 | (c >> 6 & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
    else
    {
        out += char(0xF0 | c >> 18);
        out += char(0x80 | (c >> 12 & 0x3F));
        out += char(0x80 | (c >> 6 & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

/// Checks UTF-8 and drops what isn't (a stray byte becomes nothing), so the catalog never stores broken text.
std::string clean_utf8(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size();)
    {
        uint8_t c = uint8_t(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; k++)
            ok = (uint8_t(s[i + k]) & 0xC0) == 0x80;
        if (ok && n > 1)
        {
            // No overlong forms, surrogates or values past U+10FFFF.
            uint32_t v = n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
            for (size_t k = 1; k < n; k++)
                v = v << 6 | (uint8_t(s[i + k]) & 0x3F);
            ok = v >= (n == 2 ? 0x80u : n == 3 ? 0x800u : 0x10000u) && v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF);
        }
        if (ok)
        {
            out.append(s, i, n);
            i += n;
        }
        else
            i++;
    }
    return out;
}

/// Trims spaces and control characters from both ends, turns inner control characters into spaces, and cuts the
/// text to kMaxText bytes on a character boundary.
std::string tidy(const std::string &raw)
{
    std::string s = clean_utf8(raw);
    for (char &c : s)
        if (uint8_t(c) < 0x20 || c == 0x7F)
            c = ' ';
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos)
        return std::string();
    size_t b = s.find_last_not_of(' ');
    s = s.substr(a, b - a + 1);
    s.erase(std::unique(s.begin(), s.end(), [](char x, char y) { return x == ' ' && y == ' '; }), s.end());
    if (s.size() > kMaxText)
    {
        size_t cut = kMaxText;
        while (cut > 0 && (uint8_t(s[cut]) & 0xC0) == 0x80)
            cut--;
        s.resize(cut);
    }
    return s;
}

std::string latin1_to_utf8(const uint8_t *p, size_t n)
{
    std::string out;
    for (size_t i = 0; i < n; i++)
        append_utf8(out, p[i]);
    return out;
}

std::string utf16_to_utf8(const uint8_t *p, size_t n, bool big)
{
    std::string out;
    auto unit = [&](size_t i) { return big ? unsigned(p[i]) << 8 | p[i + 1] : unsigned(p[i + 1]) << 8 | p[i]; };
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        uint32_t c = unit(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n)
        {
            uint32_t low = unit(i + 2);
            if (low >= 0xDC00 && low < 0xE000)
            {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        if (c >= 0xD800 && c < 0xE000)
            continue;
        append_utf8(out, c);
    }
    return out;
}

/// The strings of an ID3 text frame body (encoding byte first). A v2.4 frame can hold several, separated by NULs.
std::vector<std::string> id3_strings(const uint8_t *p, size_t n)
{
    std::vector<std::string> out;
    if (n < 2)
        return out;
    int enc = p[0];
    p++;
    n--;
    bool wide = enc == 1 || enc == 2;
    size_t step = wide ? 2 : 1;
    size_t start = 0;
    while (start < n)
    {
        size_t end = start;
        while (end + step <= n && !(p[end] == 0 && (!wide || p[end + 1] == 0)))
            end += step;
        const uint8_t *s = p + start;
        size_t len = end - start;
        std::string text;
        if (enc == 0)
            text = latin1_to_utf8(s, len);
        else if (enc == 3)
            text.assign(reinterpret_cast<const char *>(s), len);
        else if (wide)
        {
            bool big = enc == 2;
            if (len >= 2 && s[0] == 0xFE && s[1] == 0xFF)
            {
                big = true;
                s += 2;
                len -= 2;
            }
            else if (len >= 2 && s[0] == 0xFF && s[1] == 0xFE)
            {
                big = false;
                s += 2;
                len -= 2;
            }
            text = utf16_to_utf8(s, len, big);
        }
        text = tidy(text);
        if (!text.empty())
            out.push_back(text);
        start = end + step;
    }
    return out;
}

std::string join(const std::vector<std::string> &parts)
{
    std::string out;
    for (const std::string &p : parts)
        out += (out.empty() ? "" : "; ") + p;
    return tidy(out);
}

/// Keeps the first non-empty value of each field.
void fill(audio_tags &into, const audio_tags &from)
{
    if (into.title.empty())
        into.title = from.title;
    if (into.artist.empty())
        into.artist = from.artist;
    if (into.album.empty())
        into.album = from.album;
    if (into.genre.empty())
        into.genre = from.genre;
}

// ---- Genres ----

/// The genre names of ID3v1 and Winamp, which ID3v2 ("(17)", "17") and MP4 ('gnre') refer to by number.
const char *const kGenres[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal", "New Age",
    "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska",
    "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk", "Fusion",
    "Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise", "Alternative Rock",
    "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
    "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy",
    "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave",
    "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro",
    "Musical", "Rock & Roll", "Hard Rock", "Folk", "Folk-Rock", "National Folk", "Swing", "Fast Fusion", "Bebop",
    "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock",
    "Symphonic Rock", "Slow Rock", "Big Band", "Chorus", "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson",
    "Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire", "Slow Jam",
    "Club", "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet",
    "Punk Rock", "Drum Solo", "A Cappella", "Euro-House", "Dance Hall", "Goa", "Drum & Bass", "Club-House",
    "Hardcore", "Terror", "Indie", "BritPop", "Afro-Punk", "Polsk Punk", "Beat", "Christian Gangsta Rap",
    "Heavy Metal", "Black Metal", "Crossover", "Contemporary Christian", "Christian Rock", "Merengue", "Salsa",
    "Thrash Metal", "Anime", "JPop", "Synthpop", "Abstract", "Art Rock", "Baroque", "Bhangra", "Big Beat",
    "Breakbeat", "Chillout", "Downtempo", "Dub", "EBM", "Eclectic", "Electro", "Electroclash", "Emo", "Experimental",
    "Garage", "Global", "IDM", "Illbient", "Industro-Goth", "Jam Band", "Krautrock", "Leftfield", "Lounge",
    "Math Rock", "New Romantic", "Nu-Breakz", "Post-Punk", "Post-Rock", "Psytrance", "Shoegaze", "Space Rock",
    "Trop Rock", "World Music", "Neoclassical", "Audiobook", "Audio Theatre", "Neue Deutsche Welle", "Podcast",
    "Indie Rock", "G-Funk", "Dubstep", "Garage Rock", "Psybient",
};

/// The name of genre number n, or empty when there is none (255 is "no genre" in ID3v1).
std::string genre_name(unsigned n)
{
    return n < sizeof(kGenres) / sizeof(kGenres[0]) ? kGenres[n] : std::string();
}

bool all_digits(const std::string &s)
{
    return !s.empty() && s.size() <= 3 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// One TCON value: "Rock", "17", "(17)", "(17)Rock" (a number refined by text: the text wins), "(RX)" or "(CR)",
/// and "((text" for a text that really starts with "(".
std::string id3_genre(const std::string &value)
{
    std::string s = value;
    std::string number;
    while (s.size() >= 2 && s[0] == '(' && s[1] != '(')
    {
        size_t close = s.find(')');
        if (close == std::string::npos)
            break;
        std::string inside = s.substr(1, close - 1);
        if (number.empty())
            number = inside;
        s.erase(0, close + 1);
    }
    if (s.size() >= 2 && s[0] == '(' && s[1] == '(')
        s.erase(0, 1);
    if (s.empty())
        s = number;
    if (all_digits(s))
        return genre_name(unsigned(std::stoi(s)));
    if (s == "RX")
        return "Remix";
    if (s == "CR")
        return "Cover";
    return s;
}

// ---- ID3 ----

/// ID3v2 at 'pos' in a file (the tag may also sit inside a WAV/AIFF chunk, 'limit' bounds it). Frames are walked in
/// place and only the text frames asked for are read, so a tag holding a big cover costs one header read.
audio_tags read_id3v2(source &src, uint64_t pos, uint64_t limit)
{
    audio_tags tags;
    audio_tags band;  // TPE2, the album artist, as a fallback
    uint8_t head[10];
    if (!src.read(pos, head, 10) || std::memcmp(head, "ID3", 3) != 0 || head[3] < 2 || head[3] > 4 ||
        (head[6] | head[7] | head[8] | head[9]) & 0x80)
        return tags;
    int version = head[3];
    uint8_t flags = head[5];
    uint64_t size = syncsafe(head + 6);
    uint64_t start = pos + 10;
    if (start + size > limit)
        size = limit > start ? limit - start : 0;

    // v2.3 can carry one "unsynchronisation" over the whole tag (0xFF 0x00 -> 0xFF); undo it in memory. (v2.4 does it
    // per frame, which is handled below.)
    bytes whole;
    bool inMemory = false;
    if ((flags & 0x80) && version <= 3 && size <= kMaxUnsync)
    {
        bytes raw(size);
        if (size && !src.read(start, raw.data(), size))
            return tags;
        for (size_t i = 0; i < raw.size(); i++)
        {
            whole.push_back(raw[i]);
            if (raw[i] == 0xFF && i + 1 < raw.size() && raw[i + 1] == 0)
                i++;
        }
        inMemory = true;
        size = whole.size();
    }
    auto fetch = [&](uint64_t offset, void *out, size_t n) {
        if (offset + n > size)
            return false;
        if (inMemory)
        {
            std::memcpy(out, whole.data() + offset, n);
            return true;
        }
        return src.read(start + offset, out, n);
    };

    uint64_t at = 0;
    // An extended header comes first (v2.3: size without itself, v2.4: syncsafe size with itself).
    if (flags & 0x40)
    {
        uint8_t ext[4];
        if (!fetch(0, ext, 4))
            return tags;
        at = version == 4 ? syncsafe(ext) : uint64_t(be32(ext)) + 4;
    }

    const size_t idLen = version == 2 ? 3 : 4;
    const size_t headLen = version == 2 ? 6 : 10;
    while (at + headLen <= size)
    {
        uint8_t fh[10];
        if (!fetch(at, fh, headLen) || fh[0] == 0)
            break;
        uint64_t len = version == 2 ? be24(fh + 3) : version == 3 ? be32(fh + 4) : syncsafe(fh + 4);
        std::string id(reinterpret_cast<char *>(fh), idLen);
        uint16_t frameFlags = version == 2 ? 0 : uint16_t(fh[8] << 8 | fh[9]);
        uint64_t body = at + headLen;
        if (body + len > size)
            break;
        at = body + len;

        bool wantTitle = id == "TIT2" || id == "TT2";
        bool wantArtist = id == "TPE1" || id == "TP1";
        bool wantBand = id == "TPE2" || id == "TP2";
        bool wantAlbum = id == "TALB" || id == "TAL";
        bool wantGenre = id == "TCON" || id == "TCO";
        if (!(wantTitle || wantArtist || wantBand || wantAlbum || wantGenre) || len == 0 || len > kMaxFrame)
            continue;
        // Compressed or encrypted frames can't be read here.
        if (version == 3 && (frameFlags & 0x00C0))
            continue;
        if (version == 4 && (frameFlags & 0x000C))
            continue;
        uint64_t skip = 0;
        if (version == 3 && (frameFlags & 0x0020))
            skip = 1;  // grouping identity byte
        if (version == 4)
        {
            if (frameFlags & 0x0040)
                skip += 1;
            if (frameFlags & 0x0001)
                skip += 4;  // data length indicator
        }
        if (skip >= len)
            continue;
        bytes data(len - skip);
        if (!fetch(body + skip, data.data(), data.size()))
            break;
        if (version == 4 && (frameFlags & 0x0002))  // this frame is unsynchronised
        {
            bytes plain;
            for (size_t i = 0; i < data.size(); i++)
            {
                plain.push_back(data[i]);
                if (data[i] == 0xFF && i + 1 < data.size() && data[i + 1] == 0)
                    i++;
            }
            data.swap(plain);
        }
        std::vector<std::string> values = id3_strings(data.data(), data.size());
        if (values.empty())
            continue;
        if (wantTitle && tags.title.empty())
            tags.title = values[0];
        else if (wantArtist && tags.artist.empty())
            tags.artist = join(values);
        else if (wantBand && band.artist.empty())
            band.artist = join(values);
        else if (wantAlbum && tags.album.empty())
            tags.album = values[0];
        else if (wantGenre && tags.genre.empty())
        {
            std::vector<std::string> genres;
            for (const std::string &v : values)
            {
                std::string g = id3_genre(v);
                if (!g.empty())
                    genres.push_back(g);
            }
            tags.genre = join(genres);
        }
    }
    if (tags.artist.empty())
        tags.artist = band.artist;
    return tags;
}

/// The 128 bytes at the end of an MP3 that old taggers still write.
audio_tags read_id3v1(source &src)
{
    audio_tags tags;
    uint8_t block[128];
    if (src.length < 128 || !src.read(src.length - 128, block, 128) || std::memcmp(block, "TAG", 3) != 0)
        return tags;
    auto field = [&](size_t offset) {
        size_t n = 30;
        while (n > 0 && (block[offset + n - 1] == 0 || block[offset + n - 1] == ' '))
            n--;
        size_t nul = 0;
        while (nul < n && block[offset + nul] != 0)
            nul++;
        return tidy(latin1_to_utf8(block + offset, nul));
    };
    tags.title = field(3);
    tags.artist = field(33);
    tags.album = field(63);
    tags.genre = genre_name(block[127]);
    return tags;
}

// ---- Vorbis comments ----

/// A Vorbis comment block (vendor string, then "NAME=value" entries, little-endian lengths) in a buffer that may be
/// cut short; whatever is complete is read.
audio_tags read_vorbis_comments(const uint8_t *p, size_t n)
{
    audio_tags tags;
    std::vector<std::string> artists, bands, genres;
    if (n < 4)
        return tags;
    uint64_t vendor = le32(p);
    size_t at = 4;
    if (vendor > n - at)
        return tags;
    at += size_t(vendor);
    if (at + 4 > n)
        return tags;
    uint32_t count = le32(p + at);
    at += 4;
    for (uint32_t i = 0; i < count && at + 4 <= n; i++)
    {
        uint64_t len = le32(p + at);
        at += 4;
        if (len > n - at)
            break;
        const char *entry = reinterpret_cast<const char *>(p + at);
        at += size_t(len);
        if (len > kMaxFrame)
            continue;
        const char *eq = static_cast<const char *>(std::memchr(entry, '=', size_t(len)));
        if (!eq)
            continue;
        std::string name(entry, size_t(eq - entry));
        for (char &c : name)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        std::string value = tidy(std::string(eq + 1, size_t(entry + len - eq - 1)));
        if (value.empty())
            continue;
        if (name == "TITLE" && tags.title.empty())
            tags.title = value;
        else if (name == "ARTIST")
            artists.push_back(value);
        else if (name == "ALBUMARTIST" || name == "ALBUM ARTIST")
            bands.push_back(value);
        else if (name == "ALBUM" && tags.album.empty())
            tags.album = value;
        else if (name == "GENRE")
            genres.push_back(value);
    }
    tags.artist = join(artists.empty() ? bands : artists);
    tags.genre = join(genres);
    return tags;
}

audio_tags read_flac(source &src)
{
    uint64_t at = 4;
    for (int blocks = 0; blocks < 256; blocks++)
    {
        uint8_t h[4];
        if (!src.read(at, h, 4))
            break;
        uint32_t len = be24(h + 1);
        if ((h[0] & 0x7F) == 4)
        {
            bytes data(std::min<size_t>(len, kMaxComment));
            if (!data.empty() && src.read(at + 4, data.data(), data.size()))
                return read_vorbis_comments(data.data(), data.size());
            break;
        }
        if (h[0] & 0x80)
            break;
        at += 4 + uint64_t(len);
    }
    return audio_tags();
}

/// Ogg: pages hold the packets of the logical streams; the comment header is the second packet of a Vorbis or Opus
/// stream (after the identification header), and a later one in Ogg FLAC.
audio_tags read_ogg(source &src)
{
    uint64_t at = 0;
    bytes packet;
    int index = 0;           // the packet being collected
    bool continued = false;  // the last page ended inside a packet
    enum { kUnknown, kVorbis, kOpus, kFlac } kind = kUnknown;
    for (int pages = 0; pages < 128; pages++)
    {
        uint8_t h[27];
        if (!src.read(at, h, 27) || std::memcmp(h, "OggS", 4) != 0)
            break;
        size_t segments = h[26];
        uint8_t lacing[255];
        if (!src.read(at + 27, lacing, segments))
            break;
        uint64_t body = at + 27 + segments;
        size_t total = 0;
        for (size_t i = 0; i < segments; i++)
            total += lacing[i];
        if (!(h[5] & 1) && continued)
            packet.clear();  // the stream is not what we think; start over
        size_t used = 0;
        for (size_t i = 0; i < segments; i++)
        {
            size_t take = lacing[i];
            if (packet.size() < kMaxComment)
            {
                size_t room = std::min(take, kMaxComment - packet.size());
                size_t before = packet.size();
                packet.resize(before + room);
                if (!src.read(body + used, packet.data() + before, room))
                    return audio_tags();
            }
            used += take;
            if (take == 255)
            {
                continued = true;
                continue;
            }
            continued = false;
            // A packet is complete.
            if (index == 0)
            {
                if (packet.size() >= 7 && packet[0] == 1 && !std::memcmp(packet.data() + 1, "vorbis", 6))
                    kind = kVorbis;
                else if (packet.size() >= 8 && !std::memcmp(packet.data(), "OpusHead", 8))
                    kind = kOpus;
                else if (packet.size() >= 5 && packet[0] == 0x7F && !std::memcmp(packet.data() + 1, "FLAC", 4))
                    kind = kFlac;
                else
                    return audio_tags();
            }
            else if (kind == kVorbis && packet.size() >= 7 && packet[0] == 3 && !std::memcmp(packet.data() + 1, "vorbis", 6))
                return read_vorbis_comments(packet.data() + 7, packet.size() - 7);
            else if (kind == kOpus && packet.size() >= 8 && !std::memcmp(packet.data(), "OpusTags", 8))
                return read_vorbis_comments(packet.data() + 8, packet.size() - 8);
            else if (kind == kFlac && packet.size() >= 4 && (packet[0] & 0x7F) == 4)
                return read_vorbis_comments(packet.data() + 4, packet.size() - 4);
            else if (kind != kFlac || index > 8)
                return audio_tags();
            index++;
            packet.clear();
        }
        at = body + total;
    }
    return audio_tags();
}

// ---- RIFF / AIFF ----

audio_tags read_riff(source &src, bool little, uint64_t first)
{
    audio_tags tags;
    uint64_t at = first;
    for (int chunks = 0; chunks < 256; chunks++)
    {
        uint8_t h[8];
        if (!src.read(at, h, 8))
            break;
        uint64_t size = little ? le32(h + 4) : be32(h + 4);
        uint64_t body = at + 8;
        uint64_t end = std::min<uint64_t>(body + size, src.length);
        if (!std::memcmp(h, "id3 ", 4) || !std::memcmp(h, "ID3 ", 4))
            fill(tags, read_id3v2(src, body, end));
        else if (little && !std::memcmp(h, "LIST", 4) && size >= 4 && size <= (1u << 20))
        {
            bytes list(size_t(end - body));
            if (list.size() >= 4 && src.read(body, list.data(), list.size()) && !std::memcmp(list.data(), "INFO", 4))
            {
                audio_tags info;
                for (size_t i = 4; i + 8 <= list.size();)
                {
                    uint64_t len = le32(list.data() + i + 4);
                    if (len > list.size() - i - 8)
                        break;
                    const uint8_t *value = list.data() + i + 8;
                    std::string text = tidy(std::string(reinterpret_cast<const char *>(value),
                        strnlen(reinterpret_cast<const char *>(value), size_t(len))));
                    if (!std::memcmp(list.data() + i, "INAM", 4))
                        info.title = text;
                    else if (!std::memcmp(list.data() + i, "IART", 4))
                        info.artist = text;
                    else if (!std::memcmp(list.data() + i, "IPRD", 4))
                        info.album = text;
                    else if (!std::memcmp(list.data() + i, "IGNR", 4))
                        info.genre = text;
                    i += 8 + size_t(len) + (len & 1);
                }
                fill(tags, info);
            }
        }
        at = body + size + (size & 1);
    }
    return tags;
}

// ---- MP4 ----

struct atom
{
    uint64_t start = 0, body = 0, end = 0;
    char type[4] = {};
};

bool read_atom(source &src, uint64_t at, uint64_t limit, atom &out)
{
    uint8_t h[16];
    if (at + 8 > limit || !src.read(at, h, 8))
        return false;
    uint64_t size = be32(h);
    uint64_t header = 8;
    if (size == 1)
    {
        if (at + 16 > limit || !src.read(at + 8, h + 8, 8))
            return false;
        size = uint64_t(be32(h + 8)) << 32 | be32(h + 12);
        header = 16;
    }
    else if (size == 0)
        size = limit - at;
    if (size < header || size > limit - at)
        return false;
    out.start = at;
    out.body = at + header;
    out.end = at + size;
    std::memcpy(out.type, h + 4, 4);
    return true;
}

bool is(const atom &a, const char *type) { return std::memcmp(a.type, type, 4) == 0; }

/// The value of an ilst item: its 'data' atom (version/flags, locale, then the value). False when there is none.
bool item_data(source &src, const atom &item, bytes &value, uint8_t &type)
{
    atom data;
    for (uint64_t at = item.body; read_atom(src, at, item.end, data); at = data.end)
    {
        if (!is(data, "data") || data.end - data.body < 8 || data.end - data.body > kMaxFrame)
            continue;
        bytes v(size_t(data.end - data.body));
        if (!src.read(data.body, v.data(), v.size()))
            return false;
        type = v[3];
        value.assign(v.begin() + 8, v.end());
        return true;
    }
    return false;
}

/// The text of an ilst item, when it is UTF-8 text (type 1).
std::string item_text(source &src, const atom &item)
{
    bytes v;
    uint8_t type = 0;
    if (!item_data(src, item, v, type) || type != 1)
        return std::string();
    return tidy(std::string(v.begin(), v.end()));
}

/// 'gnre': the ID3v1 genre number plus one, as a 16-bit big-endian number.
std::string item_genre_number(source &src, const atom &item)
{
    bytes v;
    uint8_t type = 0;
    if (!item_data(src, item, v, type) || v.size() != 2)
        return std::string();
    unsigned n = unsigned(v[0]) << 8 | v[1];
    return n > 0 ? genre_name(n - 1) : std::string();
}

audio_tags read_ilst(source &src, const atom &ilst)
{
    audio_tags tags;
    std::string band, genreNumber;
    atom item;
    for (uint64_t at = ilst.body; read_atom(src, at, ilst.end, item); at = item.end)
    {
        // The names start with 0xA9 ("(c)"): nam, ART, alb, gen. aART is the album artist, gnre a genre number.
        static const char nam[4] = { char(0xA9), 'n', 'a', 'm' };
        static const char art[4] = { char(0xA9), 'A', 'R', 'T' };
        static const char alb[4] = { char(0xA9), 'a', 'l', 'b' };
        static const char gen[4] = { char(0xA9), 'g', 'e', 'n' };
        if (!std::memcmp(item.type, nam, 4))
            tags.title = item_text(src, item);
        else if (!std::memcmp(item.type, art, 4))
            tags.artist = item_text(src, item);
        else if (!std::memcmp(item.type, alb, 4))
            tags.album = item_text(src, item);
        else if (!std::memcmp(item.type, gen, 4))
            tags.genre = item_text(src, item);
        else if (is(item, "gnre"))
            genreNumber = item_genre_number(src, item);
        else if (is(item, "aART"))
            band = item_text(src, item);
    }
    if (tags.artist.empty())
        tags.artist = band;
    if (tags.genre.empty())
        tags.genre = genreNumber;
    return tags;
}

/// moov > udta > meta (a full box: 4 bytes first) > ilst, found by walking the atoms and skipping the big ones.
audio_tags read_mp4(source &src)
{
    atom top;
    for (uint64_t at = 0; read_atom(src, at, src.length, top); at = top.end)
    {
        if (!is(top, "moov"))
            continue;
        atom udta;
        for (uint64_t a = top.body; read_atom(src, a, top.end, udta); a = udta.end)
        {
            if (!is(udta, "udta"))
                continue;
            atom meta;
            for (uint64_t b = udta.body; read_atom(src, b, udta.end, meta); b = meta.end)
            {
                if (!is(meta, "meta") || meta.end - meta.body < 4)
                    continue;
                atom ilst;
                for (uint64_t c = meta.body + 4; read_atom(src, c, meta.end, ilst); c = ilst.end)
                    if (is(ilst, "ilst"))
                        return read_ilst(src, ilst);
            }
        }
    }
    return audio_tags();
}

audio_tags clip(audio_tags t)
{
    t.title = tidy(t.title);
    t.artist = tidy(t.artist);
    t.album = tidy(t.album);
    t.genre = tidy(t.genre);
    return t;
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
}  // namespace

audio_tags audio_read_tags(std::istream &in)
{
    try
    {
        source src(in);
        uint8_t magic[12] = {};
        if (!src.read(0, magic, 4))
            return audio_tags();
        src.read(0, magic, std::min<uint64_t>(12, src.length));

        if (!std::memcmp(magic, "ID3", 3))
        {
            audio_tags tags = read_id3v2(src, 0, src.length);
            if (tags.title.empty())
                fill(tags, read_id3v1(src));
            return clip(tags);
        }
        if (!std::memcmp(magic, "fLaC", 4))
            return clip(read_flac(src));
        if (!std::memcmp(magic, "OggS", 4))
            return clip(read_ogg(src));
        if (src.length >= 12 && !std::memcmp(magic, "RIFF", 4) && !std::memcmp(magic + 8, "WAVE", 4))
            return clip(read_riff(src, true, 12));
        if (src.length >= 12 && !std::memcmp(magic, "FORM", 4) && (!std::memcmp(magic + 8, "AIFF", 4) || !std::memcmp(magic + 8, "AIFC", 4)))
            return clip(read_riff(src, false, 12));
        if (src.length >= 12 && !std::memcmp(magic + 4, "ftyp", 4))
            return clip(read_mp4(src));
        // An MP3 without an ID3v2 tag may still have the old v1 block at its end.
        return clip(read_id3v1(src));
    }
    catch (...)
    {
        return audio_tags();
    }
}

bool audio_read_tags_file(const std::string &path, audio_tags &out)
{
    out = audio_tags();
    std::ifstream in = open_file(path);
    if (!in)
        return false;
    out = audio_read_tags(in);
    return true;
}

audio_tags audio_read_tags_file(const std::string &path)
{
    audio_tags tags;
    audio_read_tags_file(path, tags);
    return tags;
}
