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
// SHA-256 against SHA256SUMS; any failure deletes the download. The three files stay in the updates folder, so a download
// is still ready after a restart (Resume checks it all again), and the hash is checked once more just before it runs.
// Nothing is ever started without the person asking.
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
        bool found = false;      // kAvailable or kReady reached without the person asking: the weekly check, or a
                                 // download kept from an earlier run
    };

    // What to do once Corpus has closed (main.cpp): run the installer, or swap the exe.
    struct Pending
    {
        bool installed = false;
        std::wstring file;      // the verified download
        std::string sha256;     // its hash, checked again just before it runs (lower-case hex)
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
    // During kDownloading: stops the download and goes back to kAvailable.
    void CancelDownload();
    // After kReady, when the download is no longer there: back to kAvailable, to download it again.
    void Forget();
    // At start: a download that passed its checks in an earlier run, and is still newer than this Corpus, is kReady
    // again once it passes them a second time (signature, version, hash; on a worker thread). Whatever else is in the
    // updates folder goes, when 'tidy' (the real catalog's window; a test build only looks). While it checks, a check
    // for updates waits (the hourly timer asks again).
    void Resume(bool tidy);
    // A few seconds after start: what the first clean-up couldn't delete while the old Corpus or the setup was still
    // closing (the old exe, a finished download), unless a download is going or kept.
    void Tidy();
    // For the screenshot walk (Corpus --library <scratch> --update-state <state>): shows 'state' with a made-up newer
    // version, and never touches the network.
    void Demo(State state);

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
    // At start: removes the exe an earlier update left (Corpus.exe.old beside a portable copy). The downloads are left
    // to Resume.
    static void CleanUp();

    // What the worker thread and the window share (updater.cpp).
    struct Shared;

private:
    static void RunCheck(std::shared_ptr<Shared> shared, bool automatic, Snapshot previous);
    static void RunDownload(std::shared_ptr<Shared> shared);
    static void RunResume(std::shared_ptr<Shared> shared, bool tidy);

    std::shared_ptr<Shared> m_shared;
};

#endif
