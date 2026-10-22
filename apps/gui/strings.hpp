#ifndef STRINGS_HPP
#define STRINGS_HPP

// The text the GUI shows, in one place so it can be translated later. Layouts leave room for longer words.
namespace strings
{
    constexpr const wchar_t *kAppName = L"Corpus";

    constexpr const wchar_t *kSearchPlaceholder = L"Search by name or tag";
    constexpr const wchar_t *kTagPlaceholder    = L"Add tags";
    constexpr const wchar_t *kTagNoSelection    = L"Select files to tag them";
    constexpr const wchar_t *kOneFile           = L"1 file";
    constexpr const wchar_t *kFiles             = L"files";  // after a number: "3 files"

    constexpr const wchar_t *kEmptyLibrary      = L"Drop files or folders here";
    constexpr const wchar_t *kEmptyLibraryHint  = L"Corpus only stores where they are. Nothing is copied.";
    constexpr const wchar_t *kNoMatches         = L"No files match";
}

#endif
