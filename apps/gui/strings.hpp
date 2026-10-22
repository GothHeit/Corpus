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

    // The import bar. Counts come before the words: "Added 120 files", "3 skipped".
    constexpr const wchar_t *kCancel            = L"Cancel";
    constexpr const wchar_t *kImporting         = L"Adding files";
    constexpr const wchar_t *kFound             = L"found";            // "1,240 found"
    constexpr const wchar_t *kCancelling        = L"Cancelling";
    constexpr const wchar_t *kImportCancelled   = L"Cancelled. Nothing was added.";
    constexpr const wchar_t *kAdded             = L"Added";            // "Added 120 files"
    constexpr const wchar_t *kNothingNew        = L"No new files";
    constexpr const wchar_t *kAlreadyThere      = L"already in the library";  // "4 already in the library"
    constexpr const wchar_t *kSkipped           = L"skipped";          // "3 skipped": system files
    constexpr const wchar_t *kUnreadableFolders = L"folders could not be read";
    constexpr const wchar_t *kOneUnreadable     = L"1 folder could not be read";
    constexpr const wchar_t *kCouldNotOpen      = L"Could not open";   // "Could not open Pictures"
    constexpr const wchar_t *kNotSaved          = L"The files could not be saved.";
}

#endif
