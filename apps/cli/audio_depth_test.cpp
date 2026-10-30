// Unit tests for the audio bit depth reader, on tiny headers built here: FLAC STREAMINFO, WAV (PCM, float,
// extensible), AIFF/AIFC, ALAC in M4A, and files that declare no depth.
//
// Usage: corpus-audio-depth-test

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../include/audio_depth.hpp"

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

static void be16(bytes &b, uint32_t v) { b.push_back(v >> 8); b.push_back(v); }
static void be32(bytes &b, uint32_t v) { be16(b, v >> 16); be16(b, v & 0xFFFF); }
static void le16(bytes &b, uint32_t v) { b.push_back(v); b.push_back(v >> 8); }
static void le32(bytes &b, uint32_t v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }
static void text(bytes &b, const char *s) { while(*s) b.push_back((uint8_t)*s++); }
static void zeros(bytes &b, size_t n) { b.insert(b.end(), n, 0); }

static audio_depth read(const bytes &b)
{
    std::istringstream in(std::string(b.begin(), b.end()), std::ios::binary);
    return audio_read_depth(in);
}

static bytes flac(int bits)
{
    bytes b;
    text(b, "fLaC");
    b.push_back(0x80);  // last block, STREAMINFO
    b.push_back(0); b.push_back(0); b.push_back(34);
    be16(b, 4096); be16(b, 4096);   // block sizes
    zeros(b, 6);                    // frame sizes
    // 44100 Hz (20 bits), 2 channels (3 bits: 1), bits - 1 (5 bits), then 36 bits of sample count.
    uint64_t packed = (uint64_t)44100 << 44 | (uint64_t)1 << 41 | (uint64_t)(bits - 1) << 36;
    for(int i = 7; i >= 0; i--) b.push_back((uint8_t)(packed >> (i * 8)));
    zeros(b, 16);                   // MD5
    return b;
}

static bytes wav(unsigned tag, unsigned bits, bool extensible = false, unsigned valid = 0, unsigned sub = 0)
{
    bytes b;
    text(b, "RIFF"); le32(b, 0); text(b, "WAVE");
    text(b, "LIST"); le32(b, 3); text(b, "abc"); b.push_back(0);   // odd chunk with padding
    text(b, "fmt "); le32(b, extensible ? 40 : 16);
    le16(b, extensible ? 0xFFFE : tag); le16(b, 2); le32(b, 48000); le32(b, 48000 * 2 * bits / 8);
    le16(b, 2 * bits / 8); le16(b, bits);
    if(extensible)
    {
        le16(b, 22); le16(b, valid); le32(b, 3);
        le16(b, sub); zeros(b, 14);
    }
    text(b, "data"); le32(b, 0);
    return b;
}

static bytes aiff(bool aifc, unsigned bits, const char *type = "NONE")
{
    bytes b;
    text(b, "FORM"); be32(b, 0); text(b, aifc ? "AIFC" : "AIFF");
    text(b, "COMM"); be32(b, aifc ? 24 : 18);
    be16(b, 2); be32(b, 100); be16(b, bits); zeros(b, 10);
    if(aifc) { text(b, type); zeros(b, 2); }   // the type, then an empty compression name
    return b;
}

static void box(bytes &out, const char *type, const bytes &body)
{
    be32(out, (uint32_t)(8 + body.size()));
    text(out, type);
    out.insert(out.end(), body.begin(), body.end());
}

static bytes m4a(const char *codec, int bits)
{
    bytes cookie;
    be32(cookie, 0); be32(cookie, 4096); cookie.push_back(0); cookie.push_back(bits);
    zeros(cookie, 16);
    bytes entry;
    zeros(entry, 28);
    box(entry, "alac", cookie);
    bytes stsd;
    be32(stsd, 0); be32(stsd, 1);
    box(stsd, codec, entry);
    bytes stbl, minf, mdia, trak, moov, file;
    box(stbl, "stsd", stsd);
    box(minf, "stbl", stbl);
    box(mdia, "minf", minf);
    box(trak, "mdia", mdia);
    box(moov, "trak", trak);
    bytes ftyp;
    text(ftyp, "M4A "); be32(ftyp, 0); text(ftyp, "M4A ");
    box(file, "ftyp", ftyp);
    bytes mdat; zeros(mdat, 5);
    box(file, "mdat", mdat);     // the sound before the header, as files not optimized for streaming have it
    box(file, "moov", moov);
    return file;
}

int main()
{
    check(read(flac(16)).bits == 16, "FLAC 16-bit");
    check(read(flac(24)).bits == 24, "FLAC 24-bit");
    check(read(flac(32)).bits == 32, "FLAC 32-bit");
    check(read(flac(8)).bits == 8, "FLAC 8-bit");
    bytes bad = flac(16); bad[4] = 0x81;
    check(read(bad).bits == 0, "FLAC whose first block is not STREAMINFO");

    check(read(wav(1, 16)).bits == 16, "WAV 16-bit");
    audio_depth w24 = read(wav(1, 24));
    check(w24.bits == 24 && !w24.is_float, "WAV 24-bit");
    audio_depth w32 = read(wav(1, 32));
    check(w32.bits == 32 && !w32.is_float, "WAV 32-bit integer");
    audio_depth wf = read(wav(3, 32));
    check(wf.bits == 32 && wf.is_float, "WAV 32-bit float");
    audio_depth wx = read(wav(0, 32, true, 24, 1));
    check(wx.bits == 24 && !wx.is_float, "WAV extensible: 24 valid bits in a 32-bit container");
    audio_depth wxf = read(wav(0, 32, true, 32, 3));
    check(wxf.bits == 32 && wxf.is_float, "WAV extensible float");
    check(read(wav(0x55, 0)).bits == 0, "WAV with MP3 inside has no depth");
    check(read(wav(0, 32, true, 32, 0x55)).bits == 0, "WAV extensible with a non-PCM sub-format has no depth");

    check(read(aiff(false, 24)).bits == 24, "AIFF 24-bit");
    audio_depth af = read(aiff(true, 32, "fl32"));
    check(af.bits == 32 && af.is_float, "AIFC float");
    check(read(aiff(true, 16, "ima4")).bits == 0, "AIFC compressed has no depth");
    audio_depth asowt = read(aiff(true, 16, "sowt"));
    check(asowt.bits == 16 && !asowt.is_float, "AIFC sowt keeps its declared depth");
    audio_depth ain24 = read(aiff(true, 24, "in24"));
    check(ain24.bits == 24 && !ain24.is_float, "AIFC in24 keeps its declared depth");

    check(read(m4a("alac", 16)).bits == 16, "ALAC 16-bit");
    check(read(m4a("alac", 24)).bits == 24, "ALAC 24-bit");
    check(read(m4a("mp4a", 16)).bits == 0, "AAC in M4A has no depth");

    // A FLAC preceded by an ID3v2 tag (with and without footer) still shows its depth.
    for(bool footer : { false, true })
    {
        bytes id3; text(id3, "ID3"); id3.push_back(4); id3.push_back(0); id3.push_back(footer ? 0x10 : 0);
        be32(id3, 0x00000102);                  // syncsafe: (1 << 7) | 2 = 130 bytes
        zeros(id3, 130 + (footer ? 10 : 0));
        bytes f24 = flac(24);
        id3.insert(id3.end(), f24.begin(), f24.end());
        check(read(id3).bits == 24, footer ? "FLAC after ID3v2 with footer" : "FLAC after ID3v2");
    }

    bytes mp3; text(mp3, "ID3"); zeros(mp3, 64);
    check(read(mp3).bits == 0, "MP3 has no depth");
    check(read(bytes()).bits == 0, "empty file");
    // Every prefix of a file is read without crashing.
    for(const bytes &whole : { flac(24), wav(1, 24), aiff(false, 24), m4a("alac", 24) })
        for(size_t n = 0; n < whole.size(); n++)
            read(bytes(whole.begin(), whole.begin() + n));
    // Truncated headers must give "unknown" rather than a guess.
    bytes f = flac(24), w = wav(1, 24);
    check(read(bytes(f.begin(), f.begin() + 20)).bits == 0, "truncated FLAC");
    check(read(bytes(w.begin(), w.begin() + 40)).bits == 0, "truncated WAV");
    check(audio_read_depth_file("/nonexistent/file.flac").bits == 0, "missing file");

    if(failures == 0) std::cout << "All audio depth tests passed\n";
    return failures ? 1 : 0;
}
