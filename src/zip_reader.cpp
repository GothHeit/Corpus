#include <cstring>
#include <filesystem>
#include <fstream>

#include "../include/zip_reader.hpp"
#include "long_path.hpp"

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

std::uint32_t le16(const std::uint8_t *p) { return p[0] | (p[1] << 8); }
std::uint32_t le32(const std::uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

// ---- inflate: canonical Huffman decoding in the style of zlib's "puff" ----

const int max_bits = 15;

struct bit_reader
{
    const std::uint8_t *data;
    std::size_t size;
    std::size_t pos = 0;
    std::uint32_t bits = 0;
    int count = 0;
    bool overrun = false;

    int need(int n)
    {
        while(count < n)
        {
            if(pos >= size)
            {
                overrun = true;
                return 0;
            }
            bits |= static_cast<std::uint32_t>(data[pos++]) << count;
            count += 8;
        }
        int v = static_cast<int>(bits & ((1u << n) - 1));
        bits >>= n;
        count -= n;
        return v;
    }
};

struct huffman
{
    short counts[max_bits + 1];
    short symbols[288];

    /// Builds the tables from code lengths. False if the lengths over-subscribe the code (an incomplete code is
    /// allowed, as deflate allows it for a single distance code).
    bool build(const short *lengths, int n)
    {
        std::memset(counts, 0, sizeof counts);
        for(int i = 0; i < n; i++)
            counts[lengths[i]]++;
        int left = 1;
        for(int len = 1; len <= max_bits; len++)
        {
            left <<= 1;
            left -= counts[len];
            if(left < 0)
                return false;
        }
        short offsets[max_bits + 1];
        offsets[1] = 0;
        for(int len = 1; len < max_bits; len++)
            offsets[len + 1] = static_cast<short>(offsets[len] + counts[len]);
        for(int i = 0; i < n; i++)
        {
            if(lengths[i])
                symbols[offsets[lengths[i]]++] = static_cast<short>(i);
        }
        return true;
    }

    int decode(bit_reader &in) const
    {
        int code = 0, first = 0, index = 0;
        for(int len = 1; len <= max_bits; len++)
        {
            code |= in.need(1);
            if(in.overrun)
                return -1;
            int count = counts[len];
            if(code - count < first)
                return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const short length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
    131, 163, 195, 227, 258 };
const short length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const short dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const short dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13 };

bool inflate_codes(bit_reader &in, std::string &out, std::size_t max_bytes, const huffman &lit, const huffman &dist)
{
    while(true)
    {
        int sym = lit.decode(in);
        if(sym < 0)
            return false;
        if(sym < 256)
        {
            if(out.size() >= max_bytes)
                return false;
            out.push_back(static_cast<char>(sym));
        }
        else if(sym == 256)
        {
            return true;
        }
        else
        {
            sym -= 257;
            if(sym >= 29)
                return false;
            std::size_t len = length_base[sym] + in.need(length_extra[sym]);
            int d = dist.decode(in);
            if(d < 0 || d >= 30)
                return false;
            std::size_t back = dist_base[d] + in.need(dist_extra[d]);
            if(in.overrun || back > out.size() || out.size() + len > max_bytes)
                return false;
            std::size_t from = out.size() - back;
            for(std::size_t i = 0; i < len; i++)
                out.push_back(out[from + i]);
        }
    }
}

bool inflate_fixed(bit_reader &in, std::string &out, std::size_t max_bytes)
{
    short lengths[288];
    int i = 0;
    for(; i < 144; i++) lengths[i] = 8;
    for(; i < 256; i++) lengths[i] = 9;
    for(; i < 280; i++) lengths[i] = 7;
    for(; i < 288; i++) lengths[i] = 8;
    huffman lit, dist;
    lit.build(lengths, 288);
    for(i = 0; i < 30; i++)
        lengths[i] = 5;
    dist.build(lengths, 30);
    return inflate_codes(in, out, max_bytes, lit, dist);
}

bool inflate_dynamic(bit_reader &in, std::string &out, std::size_t max_bytes)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen = in.need(5) + 257;
    int ndist = in.need(5) + 1;
    int ncode = in.need(4) + 4;
    if(in.overrun || nlen > 286 || ndist > 30)
        return false;
    short lengths[320] = {};
    for(int i = 0; i < ncode; i++)
        lengths[order[i]] = static_cast<short>(in.need(3));
    huffman code_lengths;
    if(in.overrun || !code_lengths.build(lengths, 19))
        return false;
    int index = 0;
    short all[320] = {};
    while(index < nlen + ndist)
    {
        int sym = code_lengths.decode(in);
        if(sym < 0)
            return false;
        if(sym < 16)
        {
            all[index++] = static_cast<short>(sym);
            continue;
        }
        short value = 0;
        int repeat;
        if(sym == 16)
        {
            if(index == 0)
                return false;
            value = all[index - 1];
            repeat = 3 + in.need(2);
        }
        else if(sym == 17)
        {
            repeat = 3 + in.need(3);
        }
        else
        {
            repeat = 11 + in.need(7);
        }
        if(in.overrun || index + repeat > nlen + ndist)
            return false;
        while(repeat--)
            all[index++] = value;
    }
    if(all[256] == 0)
        return false;
    huffman lit, dist;
    if(!lit.build(all, nlen) || !dist.build(all + nlen, ndist))
        return false;
    return inflate_codes(in, out, max_bytes, lit, dist);
}

} // namespace

bool inflate_raw(const std::uint8_t *data, std::size_t size, std::string &out, std::size_t max_bytes)
{
    out.clear();
    bit_reader in{data, size};
    bool last;
    do
    {
        last = in.need(1) != 0;
        int type = in.need(2);
        if(in.overrun)
            return false;
        if(type == 0)
        {
            in.bits = 0;
            in.count = 0;
            if(in.pos + 4 > size)
                return false;
            std::size_t len = le16(data + in.pos);
            if(len != (~le16(data + in.pos + 2) & 0xFFFF))
                return false;
            in.pos += 4;
            if(in.pos + len > size || out.size() + len > max_bytes)
                return false;
            out.append(reinterpret_cast<const char*>(data + in.pos), len);
            in.pos += len;
        }
        else if(type == 1)
        {
            if(!inflate_fixed(in, out, max_bytes))
                return false;
        }
        else if(type == 2)
        {
            if(!inflate_dynamic(in, out, max_bytes))
                return false;
        }
        else
        {
            return false;
        }
    } while(!last);
    return true;
}

bool zip_archive::open(const std::string &utf8_path)
{
    list.clear();
    path = utf8_path;
    std::ifstream in = open_file(path);
    if(!in)
        return false;
    in.seekg(0, std::ios::end);
    std::streamoff end = in.tellg();
    if(end < 22)
        return false;
    file_size = static_cast<std::uint64_t>(end);

    // The end-of-central-directory record is in the last 22 + 65535 bytes (the comment may follow it).
    std::size_t tail_size = static_cast<std::size_t>(file_size < 22 + 65535 ? file_size : 22 + 65535);
    std::string tail(tail_size, '\0');
    in.seekg(static_cast<std::streamoff>(file_size - tail_size));
    in.read(&tail[0], static_cast<std::streamsize>(tail_size));
    if(!in)
        return false;
    const std::uint8_t *t = reinterpret_cast<const std::uint8_t*>(tail.data());
    std::size_t eocd = std::string::npos;
    for(std::size_t i = tail_size - 22 + 1; i-- > 0;)
    {
        if(le32(t + i) == 0x06054B50 && i + 22 + le16(t + i + 20) <= tail_size)
        {
            eocd = i;
            break;
        }
    }
    if(eocd == std::string::npos)
        return false;
    std::uint32_t count = le16(t + eocd + 10);
    std::uint64_t dir_size = le32(t + eocd + 12);
    std::uint64_t dir_offset = le32(t + eocd + 16);
    if(count == 0xFFFF || dir_size == 0xFFFFFFFF || dir_offset == 0xFFFFFFFF)
        return false; // zip64
    if(dir_offset + dir_size > file_size || dir_size > (64u << 20))
        return false;

    std::string dir(static_cast<std::size_t>(dir_size), '\0');
    in.seekg(static_cast<std::streamoff>(dir_offset));
    in.read(&dir[0], static_cast<std::streamsize>(dir_size));
    if(!in)
        return false;
    const std::uint8_t *d = reinterpret_cast<const std::uint8_t*>(dir.data());
    std::size_t pos = 0;
    for(std::uint32_t i = 0; i < count; i++)
    {
        if(pos + 46 > dir.size() || le32(d + pos) != 0x02014B50)
            return false;
        std::size_t name_len = le16(d + pos + 28), extra_len = le16(d + pos + 30), comment_len = le16(d + pos + 32);
        if(pos + 46 + name_len + extra_len + comment_len > dir.size())
            return false;
        entry e;
        std::uint32_t flags = le16(d + pos + 8);
        e.method = le16(d + pos + 10);
        e.compressed = le32(d + pos + 20);
        e.size = le32(d + pos + 24);
        e.offset = le32(d + pos + 42);
        e.name.assign(reinterpret_cast<const char*>(d + pos + 46), name_len);
        for(char &c : e.name)
        {
            if(c == '\\')
                c = '/';
        }
        pos += 46 + name_len + extra_len + comment_len;
        if(flags & 1) // encrypted
            continue;
        if(e.offset + 30 + e.compressed > file_size)
            return false;
        list.push_back(std::move(e));
    }
    return true;
}

const zip_archive::entry* zip_archive::find(const std::string &name) const
{
    for(const entry &e : list)
    {
        if(e.name == name)
            return &e;
    }
    return nullptr;
}

bool zip_archive::read(const entry &e, std::string &out, std::size_t max_bytes) const
{
    out.clear();
    if((e.method != 0 && e.method != 8) || e.size > max_bytes || (e.method == 0 && e.compressed > max_bytes) ||
        e.compressed > max_bytes + (max_bytes >> 1) + 1024)
        return false;
    std::ifstream in = open_file(path);
    if(!in)
        return false;
    std::uint8_t header[30];
    in.seekg(static_cast<std::streamoff>(e.offset));
    in.read(reinterpret_cast<char*>(header), sizeof header);
    if(!in || le32(header) != 0x04034B50)
        return false;
    std::uint64_t start = e.offset + 30 + le16(header + 26) + le16(header + 28);
    if(start + e.compressed > file_size)
        return false;
    std::string raw(static_cast<std::size_t>(e.compressed), '\0');
    in.seekg(static_cast<std::streamoff>(start));
    in.read(&raw[0], static_cast<std::streamsize>(e.compressed));
    if(!in)
        return false;
    if(e.method == 0)
    {
        out = std::move(raw);
        return true;
    }
    // The claimed size is a hint only: inflate stops at max_bytes whatever the archive says.
    return inflate_raw(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size(), out, max_bytes);
}
