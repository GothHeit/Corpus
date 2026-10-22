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
    constexpr const wchar_t *kAddFolder         = L"Add folder\u2026";
    constexpr const wchar_t *kClearFilters      = L"Clear filters";

    // The sidebar.
    constexpr const wchar_t *kLibraryTitle        = L"Library";
    constexpr const wchar_t *kAllFiles            = L"All files";
    constexpr const wchar_t *kUntagged            = L"Untagged";
    constexpr const wchar_t *kMissing             = L"Missing";
    constexpr const wchar_t *kTagsTitle           = L"Tags";
    constexpr const wchar_t *kTagFilterPlaceholder = L"Filter tags";
    constexpr const wchar_t *kNoTags              = L"No tags yet";
    constexpr const wchar_t *kNoTagMatches        = L"No matching tags";

    // Under the search bar when the query can't be read.
    constexpr const wchar_t *kUnclosedBrace     = L"Unclosed brace";
    constexpr const wchar_t *kInvalidTags       = L"Incomplete tag expression";

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

    // The viewer. The position reads "3 of 20".
    constexpr const wchar_t *kOf                = L"of";
    constexpr const wchar_t *kReduced           = L"reduced";  // too big to show at full size: "12000 × 9000 · reduced"
    constexpr const wchar_t *kLoading           = L"Loading\u2026";
    constexpr const wchar_t *kCantShow          = L"Corpus can't show this file";
    constexpr const wchar_t *kNotAtPath         = L"This file is no longer at its path";
    constexpr const wchar_t *kOpenHint          = L"Press Enter to open it in its default program";
    constexpr const wchar_t *kSubtitlesOff      = L"Off";       // the first item of the subtitle menu
    constexpr const wchar_t *kTrack             = L"Track";     // a track the file names nothing about: "Track 2"

    // Dialogs. Paths and counts go between the parts: kRemoveBefore + "3 files" + kRemoveAfter.
    constexpr const wchar_t *kNoDirect2D        = L"Could not start Direct2D, which Corpus draws its window with.";
    constexpr const wchar_t *kOpenBefore        = L"Open ";                 // "Open 20 files?"
    constexpr const wchar_t *kOpenAfter         = L" files?";
    constexpr const wchar_t *kRemoveBefore      = L"Remove ";               // "Remove 3 files from the library?"
    constexpr const wchar_t *kRemoveAfter       = L" from the library?\n\nThe files themselves stay where they are.";
    constexpr const wchar_t *kSaveFailed        = L"Could not save to the library:\n";
    constexpr const wchar_t *kReloadFailed      = L"\n\nThe library could not be reloaded either:\n";
    constexpr const wchar_t *kWillClose         = L"\n\nCorpus will close. Open it again to see what was saved.";
    constexpr const wchar_t *kReloaded          = L"\n\nThe last change was not saved. The library was reloaded to show what is saved.";

    // Starting up.
    constexpr const wchar_t *kNoCommandLine     = L"Could not read the command line.";
    constexpr const wchar_t *kLibraryNeedsPath  = L"--library needs a path.";
    constexpr const wchar_t *kUnknownArgument   = L"Unknown argument: ";
    constexpr const wchar_t *kUsage             = L"\n\nUsage: corpus-gui [--library <path to a .db file>]";
    constexpr const wchar_t *kNoAppData         = L"Could not find the local app data folder.";
    constexpr const wchar_t *kCouldNotOpenFile  = L"Could not open ";       // "Could not open C:\\...\\corpus.db:"
}

#endif
