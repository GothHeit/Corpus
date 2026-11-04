#ifndef TEXT_EXTRACT_HPP
#define TEXT_EXTRACT_HPP

#include <string>
#include <vector>

/// @brief What an extractor made of a file.
enum class extract_status
{
    ok,         // text holds the file's text (possibly none: a book of pictures)
    failed      // the file couldn't be read or isn't what its extension says
};

/// @brief Pulls the searchable text out of one kind of document. The content index asks the extractor that handles a
/// file's extension; a new document type (a PDF extractor, later) is one more subclass given to
/// register_text_extractor, with nothing else to change. Extractors run on the indexing thread and must not touch
/// shared state; one object serves every file.
class text_extractor
{
public:
    virtual ~text_extractor() = default;

    /// Whether this extractor reads the file at the UTF-8 path (by its extension only, no disk access).
    virtual bool handles(const std::string &utf8_path) const = 0;

    /// Raise it when the extractor starts finding more text, so the files it handles are indexed again.
    virtual int version() const = 0;

    /// Writes the file's text, UTF-8, into text (replacing it), at most max_bytes of it.
    virtual extract_status extract(const std::string &utf8_path, std::string &text, std::size_t max_bytes) const = 0;
};

/// The extractor for a file, or nullptr if no document type handles it. The built-in one reads EPUB.
const text_extractor* find_text_extractor(const std::string &utf8_path);

/// Adds an extractor (not owned: it must outlive every use). Call it before indexing starts, not while a
/// content_indexer runs. Extractors added later win over the built-in ones for the files they handle.
void register_text_extractor(const text_extractor *extractor);

/// The text of an EPUB: title and author from its metadata, then the chapters in reading order, tags dropped,
/// entities decoded, a newline between blocks. Exposed for tests; find_text_extractor is the normal way in.
extract_status extract_epub_text(const std::string &utf8_path, std::string &text, std::size_t max_bytes);

/// A line of the text around the first place a word starts (case and accents ignored, a word matches the start of a
/// text word like the index does), with "…" where it was cut. Empty if no word is found. For showing why a file was
/// found; words are as the search folded them.
std::string text_snippet(const std::string &text, const std::vector<std::string> &words, std::size_t radius = 60);

#endif
