#ifndef AUDIO_DEPTH_HPP
#define AUDIO_DEPTH_HPP

#include <istream>
#include <string>

/// The bit depth a lossless audio file declares in its header, read without decoding anything. FLAC (STREAMINFO),
/// WAV (fmt chunk), AIFF/AIFC (COMM) and ALAC in MP4/M4A (the 'alac' cookie). Anything else, lossy codecs included,
/// has no depth to report: bits stays 0 and the caller shows none.
struct audio_depth
{
    int bits = 0;           // 0 = unknown
    bool is_float = false;  // samples are IEEE floats (bits is then 32 or 64)
};

/// Reads the depth from a seekable binary stream. Never throws; unknown or damaged files give bits == 0.
audio_depth audio_read_depth(std::istream &in);

/// Opens path (UTF-8) and runs the function above; bits == 0 if it can't be opened.
audio_depth audio_read_depth_file(const std::string &path);

#endif
