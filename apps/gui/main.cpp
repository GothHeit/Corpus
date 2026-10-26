#include "window.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include "single_instance.hpp"
#include "open_with.hpp"
#include "../../include/database.hpp"
#include "../../include/saving.hpp"
#include "../../include/library.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
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

// Reads "--library <path>" (or "--library=<path>") from the command line, so a test build can use a scratch
// database instead of the real catalog, and the files to open: any other argument, as Explorer's "Open with" gives it. Uses the wide command line: argv in main() is in the ANSI code page and
// would mangle paths with characters outside it. Returns false, with a message, on anything it doesn't understand.
static bool ParseCommandLine(std::filesystem::path& libraryPath, std::vector<std::wstring>& files, std::wstring& error)
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
        if (arg == option && i + 1 < argc && argv[i + 1][0] != L'\0')
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

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Opening a file (ShellExecuteEx) can hand the work to shell extensions, which are COM objects. OLE on top of COM
    // (a single-threaded apartment) for dragging files out of the window and copying them to the clipboard.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    OleInitialize(nullptr);

    std::filesystem::path databaseFile;
    std::vector<std::wstring> filesToOpen;
    std::wstring commandLineError;
    if (!ParseCommandLine(databaseFile, filesToOpen, commandLineError))
    {
        MessageBoxW(nullptr, commandLineError.c_str(), strings::kAppName, MB_OK | MB_ICONERROR);
        OleUninitialize();
        CoUninitialize();
        return 1;
    }
    // Corpus moved or updated since it was put in "Open with": the entries follow it. Not for a test build on a
    // scratch library, which would point them at its throwaway .exe.
    if (databaseFile.empty())
    {
        databaseFile = DatabasePath();
        open_with::Refresh();
    }

    // One window per library: a second start hands its files to the first and ends.
    std::uint64_t instanceKey = single_instance::Key(databaseFile.wstring());
    HANDLE instanceOwner = nullptr;
    if (!single_instance::Claim(instanceKey, instanceOwner) && single_instance::Forward(instanceKey, filesToOpen))
    {
        CloseHandle(instanceOwner);
        OleUninitialize();
        CoUninitialize();
        return 0;
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

    {
        // The thumbnails go next to the library, so a test library gets its own.
        Window window(lib, (databaseFile.parent_path() / L"thumbnails").wstring(), instanceKey);
        window.OpenExternal(filesToOpen);
        window.Run();
    }

    if (instanceOwner)
        CloseHandle(instanceOwner);
    OleUninitialize();
    CoUninitialize();
    return 0;
}
