#ifndef STRINGS_HPP
#define STRINGS_HPP

// The text the GUI shows, in one place so it can be translated later. Layouts leave room for longer words.
namespace strings
{
    constexpr const wchar_t *kAppName = L"Corpus";

    constexpr const wchar_t *kSearchPlaceholder = L"Search by name or tag";
    constexpr const wchar_t *kTagPlaceholder    = L"Type tags, separated by spaces";
    constexpr const wchar_t *kTagStripAdd       = L"+ Add tag";
    constexpr const wchar_t *kTagNoSelection    = L"Select files to tag them";
    constexpr const wchar_t *kOneFile           = L"1 file";
    constexpr const wchar_t *kFiles             = L"files";  // after a number: "3 files"

    constexpr const wchar_t *kEmptyLibrary      = L"Drop files or folders here";
    constexpr const wchar_t *kEmptyLibraryHint  = L"Corpus only stores where they are. Nothing is copied.";
    constexpr const wchar_t *kNoMatches         = L"No files match";
    constexpr const wchar_t *kAddFolder         = L"Add folder\u2026";
    constexpr const wchar_t *kChecking          = L"Checking\u2026";
    constexpr const wchar_t *kClearFilters      = L"Clear filters";

    // The sort button at the right end of the search row, and its menu. The two orders are named for each key.
    constexpr const wchar_t *kSortAdded         = L"Date added";
    constexpr const wchar_t *kSortName          = L"Name";
    constexpr const wchar_t *kSortSize          = L"Size";
    constexpr const wchar_t *kSortType          = L"Type";
    constexpr const wchar_t *kSortNewest        = L"Newest first";
    constexpr const wchar_t *kSortOldest        = L"Oldest first";
    constexpr const wchar_t *kSortAToZ          = L"A to Z";
    constexpr const wchar_t *kSortZToA          = L"Z to A";
    constexpr const wchar_t *kSortLargest       = L"Largest first";
    constexpr const wchar_t *kSortSmallest      = L"Smallest first";

    // The Missing view's banner, and finding the files that moved.
    constexpr const wchar_t *kMissingBanner     = L"These files are no longer where Corpus last saw them.";
    constexpr const wchar_t *kFindMoved         = L"Find moved files\u2026";
    constexpr const wchar_t *kLookingIn         = L"Looking for the files in";  // "Looking for the files in Pictures"
    constexpr const wchar_t *kNoneMovedIn       = L"No missing files found in";  // "No missing files found in Pictures"
    constexpr const wchar_t *kOtherDrive        = L"The missing files were on another drive. Look on the drive they were on.";
    constexpr const wchar_t *kRelinked          = L"Found";                       // "Found 12 files"
    constexpr const wchar_t *kRelinkBefore      = L"Found ";                      // "Found 3 of 5 missing files in Pictures:"
    constexpr const wchar_t *kRelinkOf          = L" of ";
    constexpr const wchar_t *kRelinkIn          = L" missing files in ";
    constexpr const wchar_t *kRelinkBody        = L"Point the library to where they are now? Their tags go with them.";
    constexpr const wchar_t *kRelinkConfirm     = L"Update paths";
    constexpr const wchar_t *kRelinkMore        = L"and";                         // "and 12 more"
    constexpr const wchar_t *kRelinkMoreAfter   = L"more";

    // The sidebar.
    constexpr const wchar_t *kLibraryTitle        = L"Library";
    constexpr const wchar_t *kAllFiles            = L"All files";
    constexpr const wchar_t *kPictures            = L"Pictures";
    constexpr const wchar_t *kVideos              = L"Videos";
    constexpr const wchar_t *kBooks               = L"Books";
    constexpr const wchar_t *kUntagged            = L"Untagged";
    constexpr const wchar_t *kMissing             = L"Missing";
    constexpr const wchar_t *kTagsTitle           = L"Tags";
    constexpr const wchar_t *kTagFilterPlaceholder = L"Filter tags";
    constexpr const wchar_t *kNoTags              = L"No tags yet";
    constexpr const wchar_t *kNoTagMatches        = L"No matching tags";

    // Settings, and the licenses page hidden in it.
    constexpr const wchar_t *kSettings            = L"Settings";
    constexpr const wchar_t *kSettingsDone        = L"Done";
    constexpr const wchar_t *kSettingsSidebar     = L"Sidebar";
    constexpr const wchar_t *kShowSidebar         = L"Show sidebar";
    constexpr const wchar_t *kShowSidebarKey      = L"Ctrl+B";
    constexpr const wchar_t *kSettingsFiles       = L"Files";
    constexpr const wchar_t *kThumbnailSize       = L"Thumbnail size";
    constexpr const wchar_t *kSettingsWatched     = L"Watched folders";
    constexpr const wchar_t *kNoWatchedFolders    = L"None yet. Folders you add are watched for new files.";
    constexpr const wchar_t *kStopWatching        = L"Stop watching";
    constexpr const wchar_t *kWatchedNote         = L"New files saved in these folders are added to the library on their own, untagged. Stop watching keeps the files that are already in it.";
    constexpr const wchar_t *kSettingsWindows     = L"Windows";
    constexpr const wchar_t *kOpenWithRow         = L"Show Corpus in \u201cOpen with\u201d";
    constexpr const wchar_t *kOpenWithFailed      = L"Could not add Corpus to the \u201cOpen with\u201d list.";
    constexpr const wchar_t *kSettingsBackup      = L"Backup";
    constexpr const wchar_t *kExportCatalog       = L"Export catalog\u2026";
    constexpr const wchar_t *kImportCatalog       = L"Import catalog\u2026";
    constexpr const wchar_t *kSettingsRestore     = L"Restore a backup";
    constexpr const wchar_t *kNoBackups           = L"None yet. Corpus keeps a backup each time it starts, at most one every 12 hours.";
    constexpr const wchar_t *kRestore             = L"Restore";
    constexpr const wchar_t *kCatalogFiles        = L"Catalog (*.json)";
    constexpr const wchar_t *kExportFailed        = L"Could not export the catalog: ";
    constexpr const wchar_t *kImportFailed        = L"Could not import the catalog: ";
    constexpr const wchar_t *kExported            = L"Catalog exported";
    constexpr const wchar_t *kImportedNothing     = L"Nothing new in that catalog";
    constexpr const wchar_t *kRestoreTitle        = L"Restore the backup from ";
    constexpr const wchar_t *kRestoreBody         = L"Corpus will replace your catalog with this backup and restart. What you changed since then is lost; the catalog as it is now is kept next to it as corpus.db.before-restore.";
    constexpr const wchar_t *kRestoreFailed       = L"Could not restore the backup: ";
    constexpr const wchar_t *kAbout               = L"About";
    constexpr const wchar_t *kAboutText           = L"A catalog for your personal files. Corpus only stores where they are; nothing is copied.";
    constexpr const wchar_t *kLicenses            = L"Licenses";
    constexpr const wchar_t *kLicensesIntro       = L"Corpus is built with the free software below.";

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
    // When a watched folder gets new files: "12 new files in Pictures", "3 moved files found".
    constexpr const wchar_t *kNewFileIn         = L"new file in";
    constexpr const wchar_t *kNewFilesIn        = L"new files in";
    constexpr const wchar_t *kWatchedFolders    = L"watched folders";
    constexpr const wchar_t *kMovedFileFound    = L"moved file found";
    constexpr const wchar_t *kMovedFilesFound   = L"moved files found";
    constexpr const wchar_t *kAlreadyThere      = L"already in the library";  // "4 already in the library"
    constexpr const wchar_t *kSkipped           = L"skipped";          // "3 skipped": system files
    constexpr const wchar_t *kUnreadableFolders = L"folders could not be read";
    constexpr const wchar_t *kOneUnreadable     = L"1 folder could not be read";
    constexpr const wchar_t *kCouldNotOpen      = L"Could not open";   // "Could not open Pictures"
    constexpr const wchar_t *kNotSaved          = L"The files could not be saved.";

    // The context menu and the info pane.
    constexpr const wchar_t *kMenuOpen          = L"Open";
    constexpr const wchar_t *kMenuShowInExplorer = L"Show in Explorer";
    constexpr const wchar_t *kMenuCopy          = L"Copy";
    constexpr const wchar_t *kMenuCopyKeys      = L"Ctrl+C";
    constexpr const wchar_t *kMenuAddFolder     = L"Add folder\u2026";
    constexpr const wchar_t *kMenuAddFolderKeys = L"Ctrl+O";
    constexpr const wchar_t *kMenuAddTag        = L"Add tag";
    constexpr const wchar_t *kMenuAddTagKeys    = L"Ctrl+T";
    constexpr const wchar_t *kMenuInfo          = L"Info";
    constexpr const wchar_t *kMenuInfoKeys      = L"Ctrl+I";
    constexpr const wchar_t *kMenuRename        = L"Rename";
    constexpr const wchar_t *kMenuRenameKeys    = L"F2";
    constexpr const wchar_t *kMenuRemove        = L"Remove from library";
    constexpr const wchar_t *kMenuRemoveKeys    = L"Del";
    constexpr const wchar_t *kMenuDeleteTag     = L"Delete tag";
    constexpr const wchar_t *kMenuDelete        = L"Delete from disk";
    constexpr const wchar_t *kMenuDeleteKeys    = L"Shift+Del";
    // Tooltips of the icon-only buttons.
    constexpr const wchar_t *kTipSidebar        = L"Sidebar  Ctrl+B";
    constexpr const wchar_t *kTipSettings       = L"Settings  Ctrl+,";
    constexpr const wchar_t *kTipAddFolder      = L"Add folder  Ctrl+O";
    constexpr const wchar_t *kTipSort           = L"Sort";
    constexpr const wchar_t *kTipInfoClose      = L"Close  Ctrl+I";
    constexpr const wchar_t *kTipViewerClose    = L"Close  Esc";
    constexpr const wchar_t *kTipViewerPrevious = L"Previous  \u2190";
    constexpr const wchar_t *kTipViewerNext     = L"Next  \u2192";
    constexpr const wchar_t *kTipViewerMute     = L"Mute  M";
    constexpr const wchar_t *kTipViewerFullscreen = L"Fullscreen  F";
    constexpr const wchar_t *kTipViewerAddTag   = L"Add tag  Ctrl+T";
    constexpr const wchar_t *kTipViewerRemoveTag = L"Remove tag";
    constexpr const wchar_t *kInfoTitle         = L"Info";
    constexpr const wchar_t *kInfoNothing       = L"Select a file to see its details";
    constexpr const wchar_t *kInfoFiles         = L"files";
    constexpr const wchar_t *kInfoSize          = L"Size";
    constexpr const wchar_t *kInfoModified      = L"Modified";
    constexpr const wchar_t *kInfoCreated       = L"Created";
    constexpr const wchar_t *kInfoPath          = L"Path";
    constexpr const wchar_t *kInfoTags          = L"Tags";
    constexpr const wchar_t *kInfoNoTags        = L"No tags";
    constexpr const wchar_t *kInfoAddTag        = L"+ Add tag";
    constexpr const wchar_t *kInfoUnknown       = L"\u2014";  // the file is not at its path

    // The rename sheet, and why a name is refused.
    constexpr const wchar_t *kRenameTitle       = L"Rename";
    constexpr const wchar_t *kRenameConfirm     = L"Rename";
    constexpr const wchar_t *kRenameEmpty       = L"Type a name";
    constexpr const wchar_t *kRenameBadChars    = L"A name can't contain \\ / : * ? \" < > |";
    constexpr const wchar_t *kRenameBadEnd      = L"A name can't end with a space or a dot";
    constexpr const wchar_t *kRenameReserved    = L"Windows reserves that name";
    constexpr const wchar_t *kRenameExists      = L"A file with that name is already there";
    constexpr const wchar_t *kRenameMissing     = L"The file is no longer at its path";
    constexpr const wchar_t *kRenameFailed      = L"Windows could not rename the file";

    // The sheets that edit tags: add tags to the selected files, rename a tag, confirm the folder names after an import.
    constexpr const wchar_t *kTagSheetTitle     = L"Add tag";
    constexpr const wchar_t *kTagSheetConfirm   = L"Add";
    constexpr const wchar_t *kTagSheetEmpty     = L"Type a tag name";
    constexpr const wchar_t *kTagSheetBusiest   = L"Used most";    // the suggestions before anything is typed
    constexpr const wchar_t *kTagSheetMatching  = L"Matching tags";
    constexpr const wchar_t *kTagRenameTitle    = L"Rename tag";
    constexpr const wchar_t *kTagRenameInvalid  = L"A tag name can't be empty, contain { } ( ) or start with -";
    constexpr const wchar_t *kTagRenameFailed   = L"The tag could not be renamed";
    constexpr const wchar_t *kTagRenamed        = L"Renamed to";        // "Renamed to anime"
    constexpr const wchar_t *kTagMerged         = L"Merged into";       // "Merged into anime"
    constexpr const wchar_t *kTagDeleted        = L"Deleted tag";       // "Deleted tag anime"
    constexpr const wchar_t *kTagDeleteFailed   = L"The tag could not be deleted";
    constexpr const wchar_t *kTagDeleteBefore   = L"Delete the tag ";   // "Delete the tag sea?"
    constexpr const wchar_t *kTagDeleteLose     = L" will lose it. The files stay in the library. Ctrl+Z brings the tag back.";  // "3 files will lose it. ..."
    constexpr const wchar_t *kTagDeleteConfirm  = L"Delete tag";
    constexpr const wchar_t *kMergeBefore       = L"Merge ";           // "Merge sea into beach?"
    constexpr const wchar_t *kMergeInto         = L" into ";
    constexpr const wchar_t *kMergeExists       = L" already exists. The files tagged ";  // "beach already exists. The files tagged sea get it, ..."
    constexpr const wchar_t *kMergeGetIt        = L" get it, and the other tag goes away. Ctrl+Z separates them again.";
    constexpr const wchar_t *kMergeConfirm      = L"Merge";
    constexpr const wchar_t *kTagCharRefused    = L"A tag can't contain { } ( ) or start with -";
    constexpr const wchar_t *kFolderTagsTitle   = L"Tag by folder name";
    constexpr const wchar_t *kFolderTagsIntro   = L"Tick the folder names to use as tags on the new files.";
    constexpr const wchar_t *kFolderTagsApply   = L"Add tags";
    constexpr const wchar_t *kFolderTagsSkip    = L"Skip";
    constexpr const wchar_t *kTaggedFiles       = L"Tagged";            // "Tagged 120 files"
    constexpr const wchar_t *kAlreadyTagged     = L"Already tagged";
    constexpr const wchar_t *kUntaggedBefore    = L"Removed";           // "Removed beach from 3 files"
    constexpr const wchar_t *kUntaggedFrom      = L"from";

    // Ctrl+Z and Ctrl+Y. The notice repeats what was undone: "Undone · Tagged 3 files · beach".
    constexpr const wchar_t *kUndone            = L"Undone";
    constexpr const wchar_t *kRedone            = L"Redone";
    constexpr const wchar_t *kNothingToUndo     = L"Nothing to undo";
    constexpr const wchar_t *kNothingToRedo     = L"Nothing to redo";
    constexpr const wchar_t *kUndoFailed        = L"That can no longer be undone";

    // The viewer. The position reads "3 of 20".
    constexpr const wchar_t *kOf                = L"of";
    constexpr const wchar_t *kReduced           = L"reduced";  // too big to show at full size: "12000 × 9000 · reduced"
    constexpr const wchar_t *kLoading           = L"Loading\u2026";
    constexpr const wchar_t *kPsdPreviewOnly    = L"Preview only";  // before why: "Preview only: The Photoshop file has no merged image"
    constexpr const wchar_t *kCantShow          = L"Corpus can't show this file";
    constexpr const wchar_t *kNotAtPath         = L"This file is no longer at its path";
    constexpr const wchar_t *kSubtitlesOff      = L"Off";       // the first item of the subtitle menu
    constexpr const wchar_t *kTrack             = L"Track";     // a track the file names nothing about: "Track 2"

    // Questions and dialogs. Names and counts go between the parts: kRemoveBefore + "3 files" + kRemoveAfter.
    constexpr const wchar_t *kNoDirect2D        = L"Could not start Direct2D, which Corpus draws its window with.";
    constexpr const wchar_t *kOpenBefore        = L"Open ";                 // "Open 20 files?"
    constexpr const wchar_t *kOpenAfter         = L" files?";
    constexpr const wchar_t *kOpenBody          = L"Each one opens in its default program.";
    constexpr const wchar_t *kOpenConfirm       = L"Open";
    constexpr const wchar_t *kShowFoldersAfter  = L" folders in Explorer?";    // "Open 20 folders in Explorer?"
    constexpr const wchar_t *kCopied            = L"Copied";                    // "Copied 3 files"
    constexpr const wchar_t *kCopyFailed        = L"The files could not be copied. Are they still on disk?";
    constexpr const wchar_t *kRemoveBefore      = L"Remove ";               // "Remove 3 files from the library?"
    constexpr const wchar_t *kRemoveAfter       = L" from the library?";
    constexpr const wchar_t *kRemoveBody        = L"The files themselves stay where they are. Ctrl+Z puts them back in the library.";
    constexpr const wchar_t *kRemoveConfirm     = L"Remove";
    constexpr const wchar_t *kRemovedFiles      = L"Removed";               // "Removed 3 files from the library"
    constexpr const wchar_t *kFromLibrary       = L"from the library";
    constexpr const wchar_t *kDeleteBefore      = L"Move ";                 // "Move 3 files to the Recycle Bin?"
    constexpr const wchar_t *kDeleteAfter       = L" to the Recycle Bin?";
    constexpr const wchar_t *kDeleteBody        = L"You can restore them from the Recycle Bin. They also leave the library.";
    constexpr const wchar_t *kDeleteConfirm     = L"Move to Recycle Bin";
    constexpr const wchar_t *kDeleteFailed      = L"Could not move to the Recycle Bin:";
    constexpr const wchar_t *kSaveFailed        = L"Could not save to the library:\n";
    constexpr const wchar_t *kReloadFailed      = L"\n\nThe library could not be reloaded either:\n";
    constexpr const wchar_t *kWillClose         = L"\n\nCorpus will close. Open it again to see what was saved.";
    constexpr const wchar_t *kReloaded          = L"\n\nThe last change was not saved. The library was reloaded to show what is saved.";

    // Starting up.
    constexpr const wchar_t *kNoCommandLine     = L"Could not read the command line.";
    constexpr const wchar_t *kLibraryNeedsPath  = L"--library needs a path.";
    constexpr const wchar_t *kUnknownArgument   = L"Unknown argument: ";
    constexpr const wchar_t *kUsage             = L"\n\nUsage: Corpus [--library <path to a .db file>] [file...]";
    constexpr const wchar_t *kNoAppData         = L"Could not find the local app data folder.";
    constexpr const wchar_t *kCouldNotOpenFile  = L"Could not open ";       // "Could not open C:\\...\\corpus.db:"
}

#endif
