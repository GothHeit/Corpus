#ifndef UPDATER_HPP
#define UPDATER_HPP

#include <windows.h>
#include <memory>
#include <mutex>
#include <string>

// Finding, downloading and checking a newer Corpus on GitHub (the repository's latest release). Nothing here runs, and
// nothing is sent anywhere, until the person presses "Check for updates" or has turned on the weekly check: see
// Window::StartUpdateCheck. All the network work is on a worker thread that posts 'message' to the window when
// something changed; Snapshot() then says what.
//
// The download is Corpus-Setup.exe for an installed copy, Corpus.exe for a portable one, with SHA256SUMS and its
// signature SHA256SUMS.sig. The signature (ECDSA P-256, public key in update_key.hpp) is checked first, then the file's
// SHA-256 against SHA256SUMS; any failure deletes the download. Nothing is ever started without the person asking.
class Updater
{
public:
    enum class State { kIdle, kChecking, kUpToDate, kAvailable, kDownloading, kReady, kError };

    struct Snapshot
    {
        State state = State::kIdle;
        std::wstring version;    // the newer one: "0.3.0" (kAvailable and after)
        std::wstring pageUrl;    // its release page
        int percent = 0;         // kDownloading
        std::wstring error;      // kError: in plain words
    };

    // What to do once Corpus has closed (main.cpp): run the installer, or swap the exe.
    struct Pending
    {
        bool installed = false;
        std::wstring file;      // the verified download
    };

    Updater();
    ~Updater();

    // The window to tell, and the message to post.
    void Attach(HWND hWnd, UINT message);
    void Detach();

    // The weekly check: whether it is on (off unless the person turned it on), and whether a week has gone by since the
    // last one that worked.
    bool AutoCheck() const;
    void SetAutoCheck(bool on);
    bool DueForAutoCheck() const;

    // 'automatic': the weekly check, which says nothing when it fails.
    void Check(bool automatic);
    // After kAvailable: downloads and checks the update.
    void Download();

    Snapshot Get() const;
    // After kReady.
    Pending GetPending() const;

    // Whether this copy lives where the installer puts it (%LOCALAPPDATA%\Programs\Corpus).
    static bool IsInstalledCopy();
    // Whether the update can replace the running exe: the installer is there, or the folder is writable.
    static bool CanApply(const Pending &pending, std::wstring &why);
    static std::wstring UpdatesFolder();
    // After Corpus has closed: runs the installer silently (it starts Corpus again), or replaces the exe and starts it.
    // False, with the reason, when it could not.
    // A swapped-in exe is started with only its path, and --library <path> when this run was given one.
    static bool Apply(const Pending &pending, const std::wstring &libraryPath, std::wstring &error);
    // At start: removes what an earlier update left (Corpus.exe.old, the downloaded files).
    static void CleanUp();

    // What the worker thread and the window share (updater.cpp).
    struct Shared;

private:
    static void RunCheck(std::shared_ptr<Shared> shared, bool automatic);
    static void RunDownload(std::shared_ptr<Shared> shared);

    std::shared_ptr<Shared> m_shared;
};

#endif
