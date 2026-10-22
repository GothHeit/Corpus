#include "window.hpp"
#include "../../include/saving.hpp"
#include "../../include/library.hpp"
#include <filesystem>

static const char *kLibraryPath = "libs/sample.json";

// Double-clicking the .exe starts it in its own folder (e.g. build-gui/), not the project root,
// so look for the library next to the executable and in each parent folder above it.
static std::filesystem::path FindLibrary()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(kLibraryPath, ec))
        return kLibraryPath;

    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return fs::path();

    for (fs::path dir = fs::path(exePath).parent_path(); !dir.empty(); dir = dir.parent_path())
    {
        fs::path candidate = dir / kLibraryPath;
        if (fs::exists(candidate, ec))
            return candidate;
        if (dir == dir.parent_path())
            break;
    }
    return fs::path();
}

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Opening a file (ShellExecuteEx) can hand the work to shell extensions, which are COM objects.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    library lib;
    std::filesystem::path libraryPath = FindLibrary();
    if (libraryPath.empty())
        MessageBoxW(nullptr, L"Could not find libs/sample.json next to the executable or in any folder above it.",
            L"Corpus", MB_OK | MB_ICONWARNING);
    else
        load_library(lib, libraryPath.string());


    {
        Window window(lib);
        window.Run();
    }

    CoUninitialize();
    return 0;
}
