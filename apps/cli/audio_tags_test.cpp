// Unit tests for the audio tag reader, on tiny files built here: ID3v2.2/2.3/2.4 (all four text encodings, unsynchro-
// nisation, a big cover skipped), ID3v1, FLAC and Ogg (Vorbis, Opus) comments, WAV INFO and ID3 chunks, AIFF ID3, MP4
// ilst, and files with no tags. Every prefix of every file is read without crashing.
//
// Usage: corpus-audio-tags-test

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../include/audio_tags.hpp"

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
static void be24(bytes &b, uint32_t v) { b.push_back(v >> 16); be16(b, v & 0xFFFF); }
static void be32(bytes &b, uint32_t v) { be16(b, v >> 16); be16(b, v & 0xFFFF); }
static void le16(bytes &b, uint32_t v) { b.push_back(v); b.push_back(v >> 8); }
static void le32(bytes &b, uint32_t v) { le16(b, v & 0xFFFF); le16(b, v >> 16); }
static void text(bytes &b, const std::string &s) { for(char c : s) b.push_back((uint8_t)c); }
static void zeros(bytes &b, size_t n) { b.insert(b.end(), n, 0); }
static void append(bytes &b, const bytes &o) { b.insert(b.end(), o.begin(), o.end()); }
static void syncsafe(bytes &b, uint32_t v) { b.push_back(v >> 21 & 0x7F); b.push_back(v >> 14 & 0x7F); b.push_back(v >> 7 & 0x7F); b.push_back(v & 0x7F); }

static audio_tags read(const bytes &b)
{
    std::istringstream in(std::string(b.begin(), b.end()), std::ios::binary);
    return audio_read_tags(in);
}

static bool is(const audio_tags &t, const std::string &title, const std::string &artist, const std::string &album)
{
    return t.title == title && t.artist == artist && t.album == album;
}

// ---- ID3 ----

static bytes utf16(const std::string &ascii, bool big, bool bom)
{
    bytes b;
    if(bom) { if(big) { b.push_back(0xFE); b.push_back(0xFF); } else { b.push_back(0xFF); b.push_back(0xFE); } }
    for(char c : ascii) { if(big) { b.push_back(0); b.push_back(c); } else { b.push_back(c); b.push_back(0); } }
    return b;
}

// A frame body: encoding byte then the text as given.
static bytes body(int enc, const bytes &s) { bytes b; b.push_back(enc); append(b, s); return b; }
static bytes body(int enc, const std::string &s) { bytes t; text(t, s); return body(enc, t); }

static void frame(bytes &out, int version, const std::string &id, const bytes &data)
{
    text(out, id);
    if(version == 2) be24(out, (uint32_t)data.size());
    else if(version == 3) be32(out, (uint32_t)data.size());
    else syncsafe(out, (uint32_t)data.size());
    if(version != 2) be16(out, 0);
    append(out, data);
}

static bytes id3(int version, const bytes &frames, uint8_t flags = 0)
{
    bytes b;
    text(b, "ID3"); b.push_back((uint8_t)version); b.push_back(0); b.push_back(flags);
    syncsafe(b, (uint32_t)frames.size());
    append(b, frames);
    return b;
}

// ---- Vorbis comments ----

static bytes comments(const std::vector<std::string> &entries, const std::string &vendor = "corpus-test")
{
    bytes b;
    le32(b, (uint32_t)vendor.size()); text(b, vendor);
    le32(b, (uint32_t)entries.size());
    for(const std::string &e : entries) { le32(b, (uint32_t)e.size()); text(b, e); }
    return b;
}

static bytes flac(const bytes &vorbis)
{
    bytes b;
    text(b, "fLaC");
    b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(34); zeros(b, 34);   // STREAMINFO
    b.push_back(0x84); be24(b, (uint32_t)vorbis.size()); append(b, vorbis);            // last block: VORBIS_COMMENT
    zeros(b, 20);
    return b;
}

// One Ogg page holding whole packets (each under 255 bytes so one lacing value apiece is not enough: use the real
// lacing, 255-runs ending in a short value).
static bytes ogg_page(const std::vector<bytes> &packets, uint32_t sequence, uint8_t flags = 0)
{
    bytes lacing, data;
    for(const bytes &p : packets)
    {
        size_t n = p.size();
        while(n >= 255) { lacing.push_back(255); n -= 255; }
        lacing.push_back((uint8_t)n);
        append(data, p);
    }
    bytes b;
    text(b, "OggS"); b.push_back(0); b.push_back(flags); zeros(b, 8); le32(b, 1); le32(b, sequence); le32(b, 0);
    b.push_back((uint8_t)lacing.size());
    append(b, lacing); append(b, data);
    return b;
}

static bytes ogg_vorbis(const bytes &vorbis)
{
    bytes ident; ident.push_back(1); text(ident, "vorbis"); zeros(ident, 23);
    bytes comment; comment.push_back(3); text(comment, "vorbis"); append(comment, vorbis); comment.push_back(1);
    bytes setup; setup.push_back(5); text(setup, "vorbis"); zeros(setup, 10);
    bytes b = ogg_page({ ident }, 0, 2);
    append(b, ogg_page({ comment, setup }, 1));
    return b;
}

static bytes ogg_opus(const bytes &vorbis)
{
    bytes head; text(head, "OpusHead"); zeros(head, 11);
    bytes tags; text(tags, "OpusTags"); append(tags, vorbis);
    bytes b = ogg_page({ head }, 0, 2);
    append(b, ogg_page({ tags }, 1));
    return b;
}

// ---- RIFF, AIFF, MP4 ----

static void chunk_le(bytes &out, const std::string &id, const bytes &data)
{
    text(out, id); le32(out, (uint32_t)data.size()); append(out, data); if(data.size() & 1) out.push_back(0);
}

static bytes wav(const bytes &chunks)
{
    bytes b; text(b, "RIFF"); le32(b, (uint32_t)chunks.size() + 4); text(b, "WAVE");
    bytes fmt; le16(fmt, 1); le16(fmt, 2); le32(fmt, 44100); le32(fmt, 176400); le16(fmt, 4); le16(fmt, 16);
    chunk_le(b, "fmt ", fmt);
    bytes pcm; zeros(pcm, 33);
    chunk_le(b, "data", pcm);
    append(b, chunks);
    return b;
}

static bytes info_entry(const std::string &id, const std::string &value)
{
    bytes v; text(v, value); v.push_back(0);
    bytes b; chunk_le(b, id, v);
    return b;
}

static void box(bytes &out, const std::string &type, const bytes &data)
{
    be32(out, (uint32_t)data.size() + 8); text(out, type); append(out, data);
}

static bytes mp4_item(const std::string &type, const std::string &value)
{
    bytes d; be32(d, 1); be32(d, 0); text(d, value);
    bytes data; box(data, "data", d);
    bytes item; box(item, type, data);
    return item;
}

static bytes mp4(const bytes &items)
{
    bytes ilst; box(ilst, "ilst", items);
    bytes meta; be32(meta, 0); append(meta, ilst);
    bytes udta; box(udta, "meta", meta);
    bytes moov; box(moov, "udta", udta);
    bytes file, ftyp; text(ftyp, "M4A "); be32(ftyp, 0); text(ftyp, "M4A ");
    box(file, "ftyp", ftyp);
    bytes mdat; zeros(mdat, 5); box(file, "mdat", mdat);
    bytes m; box(m, "moov", moov); append(file, m);
    return file;
}

static std::string latin1_acute() { return std::string("Caf") + "\xE9"; }   // "Café" in Latin-1

int main()
{
    // ID3v2.3: Latin-1, UTF-16 with BOM, UTF-8 (a 2.4 encoding that is allowed to appear anywhere in practice).
    {
        bytes f;
        frame(f, 3, "TIT2", body(0, latin1_acute()));
        check(read(id3(3, f)).title == "Caf\xC3\xA9", "ID3v2.3 Latin-1 title becomes UTF-8");
    }
    {
        bytes f;
        frame(f, 3, "TIT2", body(1, utf16("Song", false, true)));
        frame(f, 3, "TPE1", body(1, utf16("Artist", true, true)));
        frame(f, 3, "TALB", body(0, "Album"));
        check(is(read(id3(3, f)), "Song", "Artist", "Album"), "ID3v2.3 UTF-16 LE and BE with BOM");
    }
    {
        bytes f;
        frame(f, 4, "TIT2", body(3, "T\xC3\xADtulo"));
        bytes artists = body(3, "One"); artists.push_back(0); text(artists, "Two");
        frame(f, 4, "TPE1", artists);
        frame(f, 4, "TALB", body(2, utf16("Disc", true, false)));
        audio_tags t = read(id3(4, f));
        check(is(t, "T\xC3\xADtulo", "One; Two", "Disc"), "ID3v2.4 UTF-8, two artists joined, UTF-16BE without BOM");
    }
    {
        bytes f;
        frame(f, 2, "TT2", body(0, "Old"));
        frame(f, 2, "TP1", body(0, "Timer"));
        frame(f, 2, "TAL", body(0, "Disc"));
        check(is(read(id3(2, f)), "Old", "Timer", "Disc"), "ID3v2.2 three-letter frames");
    }
    {
        // Album artist stands in for a missing track artist; a big cover and a padding area are skipped.
        bytes f;
        frame(f, 3, "TIT2", body(0, "Named"));
        frame(f, 3, "TPE2", body(0, "Band"));
        bytes cover; cover.push_back(0); zeros(cover, 200000);
        frame(f, 3, "APIC", cover);
        zeros(f, 64);
        audio_tags t = read(id3(3, f));
        check(is(t, "Named", "Band", ""), "ID3v2.3 album artist fallback, cover skipped");
    }
    {
        // Whole-tag unsynchronisation: every 0xFF 0x00 stands for 0xFF.
        // Put an 0xFF in the title ("A", 0xFF, "B" in Latin-1 = "A\xC3\xBFB") and unsynchronise by hand.
        bytes plain; frame(plain, 3, "TIT2", body(0, std::string("A\xFF") + "B"));
        bytes unsync;
        for(uint8_t c : plain) { unsync.push_back(c); if(c == 0xFF) unsync.push_back(0); }
        check(read(id3(3, unsync, 0x80)).title == "A\xC3\xBF" "B", "ID3v2.3 unsynchronised tag");
    }
    {
        // An ID3v1 block alone (no sync-safe garbage before it), and as the fallback for an empty v2 tag.
        bytes v1; text(v1, "TAG");
        auto field = [&](const std::string &s) { text(v1, s); zeros(v1, 30 - s.size()); };
        field("Short Title"); field("The Artist"); field("The Album"); zeros(v1, 4 + 30 + 1);
        bytes mp3; zeros(mp3, 300); append(mp3, v1);
        check(is(read(mp3), "Short Title", "The Artist", "The Album"), "ID3v1 at the end of an MP3");
        bytes both = id3(3, bytes()); zeros(both, 100); append(both, v1);
        check(read(both).title == "Short Title", "ID3v1 fills in when the v2 tag has no title");
    }

    // Vorbis comments.
    {
        bytes v = comments({ "title=Lossless", "ARTIST=Left", "artist=Right", "ALBUM=Wide", "ALBUMARTIST=Group", "DATE=1999" });
        check(is(read(flac(v)), "Lossless", "Left; Right", "Wide"), "FLAC comments, names in any case, two artists");
        check(is(read(ogg_vorbis(v)), "Lossless", "Left; Right", "Wide"), "Ogg Vorbis comments");
        check(is(read(ogg_opus(v)), "Lossless", "Left; Right", "Wide"), "Opus tags");
        check(read(flac(comments({ "ALBUMARTIST=Group", "TITLE=X" }))).artist == "Group", "FLAC album artist fallback");
        // A long comment packet spans many pages' worth of lacing (cover art in a comment).
        bytes big = comments({ "TITLE=Heavy", "METADATA_BLOCK_PICTURE=" + std::string(60000, 'A'), "ALBUM=After" });
        check(read(ogg_vorbis(big)).title == "Heavy", "Ogg packet with a huge comment keeps the title");
    }

    // WAV.
    {
        bytes list; text(list, "INFO"); append(list, info_entry("INAM", "Wave Title")); append(list, info_entry("IART", "Wave Artist"));
        append(list, info_entry("IPRD", "Wave Album"));
        bytes chunks; chunk_le(chunks, "LIST", list);
        check(is(read(wav(chunks)), "Wave Title", "Wave Artist", "Wave Album"), "WAV LIST/INFO");

        bytes f; frame(f, 3, "TIT2", body(0, "Embedded")); frame(f, 3, "TPE1", body(0, "Tagger"));
        bytes c; chunk_le(c, "id3 ", id3(3, f));
        check(is(read(wav(c)), "Embedded", "Tagger", ""), "WAV id3 chunk");
        check(is(read(wav(bytes())), "", "", ""), "WAV with no tags");
    }
    // AIFF with an ID3 chunk.
    {
        bytes f; frame(f, 3, "TIT2", body(0, "Aiff Title"));
        bytes t = id3(3, f);
        bytes b; text(b, "FORM"); be32(b, 0); text(b, "AIFF");
        text(b, "ID3 "); be32(b, (uint32_t)t.size()); append(b, t); if(t.size() & 1) b.push_back(0);
        check(read(b).title == "Aiff Title", "AIFF ID3 chunk");
    }
    // MP4.
    {
        static const char nam[] = { char(0xA9), 'n', 'a', 'm', 0 }, art[] = { char(0xA9), 'A', 'R', 'T', 0 },
            alb[] = { char(0xA9), 'a', 'l', 'b', 0 };
        bytes items;
        append(items, mp4_item(nam, "M4A Title")); append(items, mp4_item(art, "M4A Artist")); append(items, mp4_item(alb, "M4A Album"));
        check(is(read(mp4(items)), "M4A Title", "M4A Artist", "M4A Album"), "MP4 ilst");
        bytes band; append(band, mp4_item(nam, "N")); append(band, mp4_item("aART", "Ensemble"));
        check(read(mp4(band)).artist == "Ensemble", "MP4 album artist fallback");
    }

    // Nothing to read.
    check(is(read(bytes()), "", "", ""), "empty file");
    {
        bytes junk; zeros(junk, 1000);
        check(is(read(junk), "", "", ""), "zeros");
        bytes noise; for(int i = 0; i < 4000; i++) noise.push_back((uint8_t)(i * 131 + 7));
        check(is(read(noise), "", "", ""), "noise");
        check(is(read(id3(3, bytes())), "", "", ""), "empty ID3 tag");
    }
    check(audio_read_tags_file("/nonexistent/file.flac").title.empty(), "missing file");
    {
        audio_tags t;
        t.title = "stale";
        check(!audio_read_tags_file("/nonexistent/file.flac", t) && t.title.empty(), "a file that can't be opened is told apart from one with no tags");
    }

    // A title with control characters and padding is tidied; broken UTF-8 is dropped; long ones are cut.
    {
        bytes f; frame(f, 4, "TIT2", body(3, std::string("  Tab\tbed \xFF text  ")));
        check(read(id3(4, f)).title == "Tab bed text", "control characters and bad bytes tidied");
        bytes g; frame(g, 4, "TIT2", body(3, std::string(2000, 'x')));
        check(read(id3(4, g)).title.size() == 512, "long titles are cut to 512 bytes");
    }

    // Every prefix, and every byte flipped, of each kind of file is read without crashing.
    {
        bytes f; frame(f, 3, "TIT2", body(0, "T")); frame(f, 3, "TPE1", body(1, utf16("A", false, true)));
        bytes v = comments({ "TITLE=T", "ARTIST=A" });
        bytes list; text(list, "INFO"); append(list, info_entry("INAM", "T"));
        bytes lc; chunk_le(lc, "LIST", list);
        static const char nam[] = { char(0xA9), 'n', 'a', 'm', 0 };
        for(const bytes &whole : { id3(3, f), flac(v), ogg_vorbis(v), ogg_opus(v), wav(lc), mp4(mp4_item(nam, "T")) })
        {
            for(size_t n = 0; n < whole.size(); n++)
                read(bytes(whole.begin(), whole.begin() + n));
            for(size_t n = 0; n < whole.size() && n < 400; n++)
            {
                bytes bad = whole; bad[n] ^= 0xFF;
                read(bad);
                bad[n] = 0x7F;
                read(bad);
            }
        }
    }

    if(failures == 0) std::cout << "All audio tag tests passed\n";
    return failures ? 1 : 0;
}
