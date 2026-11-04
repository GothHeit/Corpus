#ifndef ZIP_READER_HPP
#define ZIP_READER_HPP

#include <cstdint>
#include <string>
#include <vector>

/// @brief A zip archive read in place, with no library: stored and deflated entries (what EPUBs use), no encryption,
/// no zip64. Only the central directory is read on open; an entry is read from the file when asked for. Every size
/// and offset in the file is checked against the file's length, and an entry is only inflated up to a limit the caller
/// sets, so a damaged or hostile archive fails instead of exhausting memory. Portable (core): the GUI's own
/// ZipFolder (shell) is separate.
class zip_archive
{
public:
    struct entry
    {
        std::string name;               // UTF-8, '/' separators, as stored
        std::uint32_t method = 0;       // 0 stored, 8 deflated
        std::uint64_t compressed = 0;
        std::uint64_t size = 0;         // uncompressed size, as the archive claims
        std::uint64_t offset = 0;       // of the local header
    };

    /// Reads the central directory of the file at the UTF-8 path. False if it isn't a readable zip.
    bool open(const std::string &utf8_path);

    const std::vector<entry>& entries() const { return list; }

    /// The entry named exactly so (names compare as stored), or nullptr.
    const entry* find(const std::string &name) const;

    /// Reads and inflates an entry into out (replacing it). False if the entry is damaged, uses a method other than
    /// stored or deflate, or is larger than max_bytes uncompressed.
    bool read(const entry &e, std::string &out, std::size_t max_bytes) const;

private:
    std::string path;
    std::uint64_t file_size = 0;
    std::vector<entry> list;
};

/// Inflates a raw deflate stream (RFC 1951). Stops with false on bad data, or when the output would pass max_bytes.
bool inflate_raw(const std::uint8_t *data, std::size_t size, std::string &out, std::size_t max_bytes);

#endif
