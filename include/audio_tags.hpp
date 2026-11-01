#ifndef AUDIO_TAGS_HPP
#define AUDIO_TAGS_HPP

#include <istream>
#include <string>

/// The title, artist and album a song's own tags carry, read without decoding anything: ID3v2 and ID3v1 (MP3, and
/// the ID3 chunk of WAV and AIFF), Vorbis comments (FLAC, Ogg Vorbis, Opus, Ogg FLAC), WAV LIST/INFO, and the MP4
/// ilst atoms (M4A, M4B, ALAC, AAC in MP4). Read only: Corpus never writes a file's tags. Strings are UTF-8, trimmed,
/// and at most 512 bytes. An empty string means the file has no such tag (or no tags at all).
struct audio_tags
{
    std::string title;
    std::string artist;  // the track artist, else the album artist
    std::string album;
};

/// Reads the tags from a seekable binary stream. Never throws; unknown or damaged files give empty strings.
audio_tags audio_read_tags(std::istream &in);

/// Opens path (UTF-8) and runs the function above. Returns false, leaving 'out' empty, if the file can't be opened
/// (drive gone, share down): that is not "no tags", and the caller should try again later.
bool audio_read_tags_file(const std::string &path, audio_tags &out);

/// The same, for callers that don't care why the tags are empty.
audio_tags audio_read_tags_file(const std::string &path);

#endif
