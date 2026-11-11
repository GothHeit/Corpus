#ifndef MEDIA_FACTS_HPP
#define MEDIA_FACTS_HPP

#include <string>

// The line the Info pane shows under a file's name, read from the file itself without decoding it:
// "PNG · 900 × 1400", "MP4 · 1920 × 1080 · 0:24", "FLAC · 24-bit / 96 kHz · 4:12". Empty for other kinds, or when
// Windows can't read what it needs. Reads through the Windows property system (the same facts Explorer shows), and
// WIC for a picture it has no size for. It may block on a slow drive: call it from a thread of its own, which has
// COM started.
std::wstring MediaFacts(const std::string &path);

#endif
