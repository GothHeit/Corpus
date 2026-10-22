#include "window.hpp"
#include "ui_util.hpp"
#include "../../include/database.hpp"
#include "../../include/saving.hpp"
#include "../../include/library.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>

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
// database instead of the real catalog. Uses the wide command line: argv in main() is in the ANSI code page and
// would mangle paths with characters outside it. Returns false, with a message, on anything it doesn't understand.
static bool ParseCommandLine(std::filesystem::path& libraryPath, std::wstring& error)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        error = L"Could not read the command line.";
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
        else
        {
            error = (arg == option || arg == option + L"=" ? L"--library needs a path." : L"Unknown argument: " + arg)
                + L"\n\nUsage: corpus-gui [--library <path to a .db file>]";
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

    // Opening a file (ShellExecuteEx) can hand the work to shell extensions, which are COM objects.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::filesystem::path databaseFile;
    std::wstring commandLineError;
    if (!ParseCommandLine(databaseFile, commandLineError))
    {
        MessageBoxW(nullptr, commandLineError.c_str(), L"Corpus", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    if (databaseFile.empty())
        databaseFile = DatabasePath();

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
            ? L"Could not find the local app data folder."
            : L"Could not open " + databaseFile.wstring() + L":\n" + Utf8ToWide(db.last_error());
        MessageBoxW(nullptr, message.c_str(), L"Corpus", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    {
        Window window(lib);
        window.Run();
    }

    CoUninitialize();
    return 0;
}
