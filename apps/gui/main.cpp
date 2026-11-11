#include "window.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include "single_instance.hpp"
#include "open_with.hpp"
#include "media_controls.hpp"
#include "file_kind.hpp"
#include "video_player.hpp"
#include "../../include/database.hpp"
#include "../../include/saving.hpp"
#include "../../include/library.hpp"
#include "../../include/backup.hpp"
#include "../../include/content_index.hpp"
#include "../../include/file.hpp"
#include "../../include/tag.hpp"
#include <algorithm>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <memory>
#include <vector>

// The library lives in %LOCALAPPDATA%\Corpus\corpus.db (C:\Users\<you>\AppData\Local\Corpus), the usual place
// for an app's own data, so it doesn't depend on where the .exe is. It must be on a local disk: SQLite's file
// locks don't work over network shares, and that includes \\wsl.localhost when the .exe is built inside WSL.
static std::filesystem::path DatabasePath()
{
    PWSTR localAppData = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
        path = std::filesystem::path(localAppData) / L"Corpus" / L"corpus.db";
    CoTaskMemFree(localAppData);
    return path;
}

// What other programs ask of Corpus on its command line: the music list written to a file (--export-music), or
// music played without the window coming up (--play [--shuffle], --queue, --next). They never show a message: a
// bad command line ends with exit code 2, a failure with 1.
struct MusicCommand
{
    std::filesystem::path exportTo;
    single_instance::Play play = single_instance::Play::kNone;
    bool shuffle = false;
    bool Any() const { return !exportTo.empty() || play != single_instance::Play::kNone || shuffle; }
};

// Reads "--library <path>" (or "--library=<path>") from the command line, so a test build can use a scratch
// database instead of the real catalog, and the files to open: any other argument, as Explorer's "Open with" gives it. Uses the wide command line: argv in main() is in the ANSI code page and
// would mangle paths with characters outside it. Returns false, with a message, on anything it doesn't understand.
static bool ParseCommandLine(std::filesystem::path& libraryPath, std::vector<std::wstring>& files, bool& newWindow,
    MusicCommand& music, std::wstring& error)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        error = strings::kNoCommandLine;
        return false;
    }

    const std::wstring option = L"--library";
    bool ok = true;
    for (int i = 1; i < argc && ok; i++)
    {
        std::wstring arg = argv[i];
        if (arg == L"--new-window")
            newWindow = true;
        else if (arg == L"--export-music" && i + 1 < argc && argv[i + 1][0] != L'\0')
        {
            std::error_code ec;
            std::filesystem::path absolute = std::filesystem::absolute(argv[++i], ec);
            music.exportTo = ec ? std::filesystem::path(argv[i]) : absolute;
        }
        else if ((arg == L"--play" || arg == L"--queue" || arg == L"--next") && music.play == single_instance::Play::kNone)
            music.play = arg == L"--play" ? single_instance::Play::kPlay : arg == L"--queue" ? single_instance::Play::kQueue
                : single_instance::Play::kNext;
        else if (arg == L"--shuffle")
            music.shuffle = true;
        else if (arg == L"--volume" && i + 1 < argc)
            gInheritedVolume = _wtoi(argv[++i]);
        else if (arg == L"--audio-device" && i + 1 < argc)
            gInheritedDevice = WideToUtf8(argv[++i]);
        else if (arg == option && i + 1 < argc && argv[i + 1][0] != L'\0')
            libraryPath = argv[++i];
        else if (arg.compare(0, option.size() + 1, option + L"=") == 0 && arg.size() > option.size() + 1)
            libraryPath = arg.substr(option.size() + 1);
        else if (arg.compare(0, 2, L"--") != 0)
        {
            // Relative to the current folder, like the library.
            std::error_code ec;
            std::filesystem::path absolute = std::filesystem::absolute(arg, ec);
            files.push_back(ec ? arg : absolute.wstring());
        }
        else
        {
            error = (arg == option || arg == option + L"=" ? std::wstring(strings::kLibraryNeedsPath) : strings::kUnknownArgument + arg)
                + strings::kUsage;
            ok = false;
        }
    }
    LocalFree(argv);

    // One music command at a time, with what it needs; --shuffle goes with --play only.
    if (ok && music.Any())
    {
        bool playing = music.play != single_instance::Play::kNone;
        bool fine = !newWindow && !(playing && !music.exportTo.empty()) && (!music.shuffle || music.play == single_instance::Play::kPlay)
            && (playing ? !files.empty() : files.empty());
        if (!fine)
        {
            error = strings::kUsage;
            ok = false;
        }
        else if (music.shuffle)
            music.play = single_instance::Play::kShuffle;
    }

    // Relative to the current folder, so error messages show where the file really is.
    if (ok && !libraryPath.empty())
    {
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(libraryPath, ec);
        if (!ec)
            libraryPath = absolute;
    }
    return ok;
}

// Starts Corpus again with the same arguments, once the first one has let go of the library.
static void Relaunch()
{
    wchar_t exe[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    std::wstring command = GetCommandLineW();
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(exe, &command[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

// "Open in another window": a process of its own that shows the files in the viewer and ends when its window closes.
// It takes no part in the one-window-per-library rule and never touches the catalog: it works on an empty library
// held in memory, and keeps its settings (volume, no queue, shared audio output) under a key of its own, so what the
// main window plays, saves or holds on the audio device is left alone. 'libraryPath' is only where the thumbnail
// cache is looked for; empty for the usual library.
static int RunNewWindow(const std::filesystem::path& libraryPath, const std::vector<std::wstring>& files)
{
    bool any = false;
    for (const std::wstring& file : files)
        any = any || (Viewer::CanShow(WideToUtf8(file)) && GetFileAttributesW(FileWide(file).c_str()) != INVALID_FILE_ATTRIBUTES);
    if (!any)
        return 0;

    kSettingsKey = kSecondarySettingsKey;
    database db;
    library lib;
    if (!db.open(std::string(":memory:")) || !open_library(lib, db, ""))
        return 1;

    std::filesystem::path thumbnails = (libraryPath.empty() ? DatabasePath() : libraryPath).parent_path() / L"thumbnails";
    Window window(lib, thumbnails.wstring(), 0, true);
    window.SetLibraryArgument(libraryPath.wstring());
    window.OpenExternal(files);
    window.Run();
    return 0;
}

// Corpus --export-music: every song in the library, with its own tags and the library's, written as JSON for
// another program, and nothing else. The catalog is opened read only, so it works beside the window that holds it
// and never writes to it. A song whose tags were never read is read here, from the file.
static int ExportMusic(const std::filesystem::path &databaseFile, const std::filesystem::path &dest)
{
    database db;
    library lib;
    if (!db.open_read_only(databaseFile) || !db.load(lib))
        return 1;
    std::vector<music_entry> songs;
    for (const file *f : lib.get_files())
    {
        if (KindOf(f->get_path()) != FileKind::kAudio)
            continue;
        music_entry song;
        song.path = f->get_path();
        const audio_tags *tags = f->get_audio();
        song.tags = tags && !tags->outdated ? *tags : audio_read_tags_file(f->get_path());
        for (const tag *t : f->get_tags())
            song.tags_of_file.push_back(t->get_id());
        std::sort(song.tags_of_file.begin(), song.tags_of_file.end());
        songs.push_back(std::move(song));
    }
    lib.clear();
    db.close();
    std::string error;
    return export_music(songs, dest, error) ? 0 : 1;
}

int main()
{
    RegisterAppIdentity();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Opening a file (ShellExecuteEx) can hand the work to shell extensions, which are COM objects. OLE on top of COM
    // (a single-threaded apartment) for dragging files out of the window and copying them to the clipboard.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    OleInitialize(nullptr);

    std::filesystem::path databaseFile;
    std::vector<std::wstring> filesToOpen;
    std::wstring commandLineError;
    bool newWindow = false;
    MusicCommand music;
    if (!ParseCommandLine(databaseFile, filesToOpen, newWindow, music, commandLineError))
    {
        if (!music.Any())
            MessageBoxW(nullptr, commandLineError.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
        OleUninitialize();
        CoUninitialize();
        return music.Any() ? 2 : 1;
    }
    if (!music.exportTo.empty())
    {
        std::filesystem::path catalog = databaseFile.empty() ? DatabasePath() : databaseFile;
        int result = catalog.empty() ? 1 : ExportMusic(catalog, music.exportTo);
        OleUninitialize();
        CoUninitialize();
        return result;
    }
    RegisterStartShortcut();

    // Music to play: only songs that are there count, and with none Corpus is not started for nothing.
    if (music.play != single_instance::Play::kNone)
    {
        bool any = false;
        for (const std::wstring &file : filesToOpen)
            any = any || (IsPlayableAudio(WideToUtf8(file)) && GetFileAttributesW(FileWide(file).c_str()) != INVALID_FILE_ATTRIBUTES);
        if (!any)
        {
            OleUninitialize();
            CoUninitialize();
            return 1;
        }
        gQuietStart = true;
    }
    if (newWindow)
    {
        int result = RunNewWindow(databaseFile, filesToOpen);
        OleUninitialize();
        CoUninitialize();
        return result;
    }

    // Corpus moved or updated since it was put in "Open with": the entries follow it. Not for a test build on a
    // scratch library, which would point them at its throwaway .exe.
    const bool customLibrary = !databaseFile.empty();
    if (databaseFile.empty())
    {
        databaseFile = DatabasePath();
        open_with::Refresh();
    }

    // One window per library: a second start hands its files to the first and ends.
    std::uint64_t instanceKey = single_instance::Key(databaseFile.wstring());
    HANDLE instanceOwner = nullptr;
    if (!single_instance::Claim(instanceKey, instanceOwner) && instanceOwner)
    {
        // Another Corpus owns this catalog. It is never opened a second time, even when the first one is busy and
        // doesn't answer: two windows would hold separate copies of one catalog. Ask once more, then give up; the
        // files, if any, open in a window of their own that doesn't touch the catalog.
        int result = 0;
        if (!single_instance::Forward(instanceKey, filesToOpen, music.play) && !single_instance::Forward(instanceKey, filesToOpen, music.play))
        {
            // Music is never played by a second window: it would play over the first one's.
            if (music.play != single_instance::Play::kNone)
                result = 1;
            else if (!filesToOpen.empty())
                result = RunNewWindow(customLibrary ? databaseFile : std::filesystem::path(), filesToOpen);
            else
                MessageBoxW(nullptr, strings::kAlreadyOpenBusy, strings::kAppName, MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(instanceOwner);
        OleUninitialize();
        CoUninitialize();
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(databaseFile.parent_path(), ec);

    // SQLite takes UTF-8 paths. path::string() would use the ANSI code page and break accented folder names.
    std::string databasePath = WideToUtf8(databaseFile.wstring());

    // Declared before the library so it is closed after it.
    database db;
    library lib;
    if (databaseFile.empty() || !db.open(databasePath) || !open_library(lib, db, ""))
    {
        std::wstring message = databaseFile.empty()
            ? std::wstring(strings::kNoAppData)
            : strings::kCouldNotOpenFile + databaseFile.wstring() + L":\n" + Utf8ToWide(db.last_error());
        MessageBoxW(nullptr, message.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
        OleUninitialize();
        CoUninitialize();
        return 1;
    }

    // The automatic backups: %LOCALAPPDATA%\Corpus\backups, or next to a test library. A copy of the database file
    // takes a moment, and the window isn't up yet, so it runs here, before the library is in use.
    std::filesystem::path backupFolder = customLibrary ? databaseFile.parent_path() / L"backups" : default_backup_folder();
    if (!backupFolder.empty())
        backup_database(db, backupFolder);

    // The text of the books, searchable: an index file beside the catalog and a background indexer (see
    // content_index.hpp). Without them (SQLite built with no FTS5, say) the search covers names and tags only.
    content_index contentIndex;
    std::unique_ptr<content_indexer> indexer;
    if (contentIndex.open(content_index_path(databaseFile).u8string(), databasePath))
    {
        lib.attach_content(&contentIndex);
        indexer = std::make_unique<content_indexer>(contentIndex);
    }

    std::filesystem::path restore;
    {
        // The thumbnails go next to the library, so a test library gets its own.
        Window window(lib, (databaseFile.parent_path() / L"thumbnails").wstring(), instanceKey);
        window.SetBackupFolder(backupFolder);
        window.SetLibraryArgument(customLibrary ? databaseFile.wstring() : std::wstring());
        window.SetContent(indexer ? &contentIndex : nullptr, indexer.get());
        if (music.play != single_instance::Play::kNone)
            window.PlayExternal(filesToOpen, music.play);
        else
            window.OpenExternal(filesToOpen);
        window.Run();
        restore = window.PendingRestore();
    }

    // The indexer and the index have the file open too: they go before the file is replaced.
    indexer.reset();
    lib.attach_content(nullptr);
    contentIndex.close();

    // "Restore" in the settings: the database is closed, the backup takes its place, and Corpus starts again.
    if (!restore.empty())
    {
        lib.clear();
        db.close();
        std::string error;
        if (restore_database(restore, databaseFile, error))
        {
            if (instanceOwner)
            {
                CloseHandle(instanceOwner);
                instanceOwner = nullptr;
            }
            Relaunch();
        }
        else
        {
            std::wstring message = strings::kRestoreFailed + Utf8ToWide(error);
            MessageBoxW(nullptr, message.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
        }
    }

    if (instanceOwner)
        CloseHandle(instanceOwner);
    OleUninitialize();
    CoUninitialize();
    return 0;
}
