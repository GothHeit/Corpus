#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "updater.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include "update_key.hpp"
#include "update_verify.hpp"
#include "../../include/update_logic.hpp"
#include <winhttp.h>
#include <shlobj.h>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <functional>
#include <thread>

namespace
{
    const char *const kRepository = "GothHeit/Corpus";
    const wchar_t *const kLatestUrl = L"https://api.github.com/repos/GothHeit/Corpus/releases/latest";
    const wchar_t *const kAutoValue = L"UpdateAutoCheck";
    const wchar_t *const kLastValue = L"UpdateLastCheck";
    const std::time_t kWeek = 7 * 24 * 3600;
    const size_t kMaxRelease = 4 << 20;           // the release JSON
    const size_t kMaxSums = 64 << 10;             // SHA256SUMS
    const size_t kMaxSignature = 1 << 10;         // SHA256SUMS.sig is 64 bytes
    const unsigned long long kMaxProgram = 256ull << 20;

    // Not in every MinGW header.
    const DWORD kAutomaticProxy = 4;              // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
    const DWORD kTls12 = 0x00000800;
    const DWORD kTls13 = 0x00002000;

    std::wstring ErrorOffline() { return strings::kUpdateOffline; }

    namespace fs = std::filesystem;

    bool ReadRegistryTime(std::time_t &value)
    {
        ULONGLONG stored = 0;
        DWORD size = sizeof(stored);
        if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kLastValue, RRF_RT_REG_QWORD, nullptr, &stored, &size) != ERROR_SUCCESS)
            return false;
        value = static_cast<std::time_t>(stored);
        return true;
    }

    void WriteRegistryTime(std::time_t value)
    {
        ULONGLONG stored = static_cast<ULONGLONG>(value);
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kLastValue, REG_QWORD, &stored, sizeof(stored));
    }

    std::wstring ExePath()
    {
        wchar_t exe[MAX_PATH];
        DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        return length == 0 || length >= MAX_PATH ? std::wstring() : std::wstring(exe, length);
    }

    std::wstring LocalAppData()
    {
        PWSTR path = nullptr;
        std::wstring result;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
            result = path;
        CoTaskMemFree(path);
        return result;
    }

    // What an HTTP GET came to.
    enum class Fetch { kOk, kOffline, kStatus, kTooBig, kCancelled, kSink };

    // GET 'url' over https and hands the body to 'sink' piece by piece (false from it stops the download). 'total' is told
    // the size in advance when the server says it. Only a User-Agent is sent: no cookies, no identifiers, no extra headers.
    Fetch HttpGet(const std::wstring &url, unsigned long long limit, const std::atomic<bool> &cancel, DWORD &status,
        const std::function<void(unsigned long long)> &total, const std::function<bool(const char*, size_t)> &sink)
    {
        status = 0;
        URL_COMPONENTS parts = { sizeof(parts) };
        wchar_t host[256], path[2048];
        parts.lpszHostName = host;
        parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
            return Fetch::kOffline;

        std::wstring agent = std::wstring(L"Corpus/") + CORPUS_VERSION_W;
        HINTERNET session = WinHttpOpen(agent.c_str(), kAutomaticProxy, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
        {
            session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session)
                return Fetch::kOffline;
        }
        DWORD protocols = kTls12 | kTls13;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        WinHttpSetTimeouts(session, 10000, 10000, 20000, 20000);

        Fetch result = Fetch::kOffline;
        HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(request, nullptr))
        {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                &code, &size, WINHTTP_NO_HEADER_INDEX);
            status = code;
            if (code != 200)
                result = Fetch::kStatus;
            else
            {
                DWORD length = 0;
                size = sizeof(length);
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
                {
                    if (length > limit)
                        result = Fetch::kTooBig;
                    else if (total)
                        total(length);
                }
                if (result == Fetch::kOffline)
                {
                    result = Fetch::kOk;
                    unsigned long long got = 0;
                    char buffer[64 * 1024];
                    for (;;)
                    {
                        if (cancel)
                        {
                            result = Fetch::kCancelled;
                            break;
                        }
                        DWORD read = 0;
                        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read))
                        {
                            result = Fetch::kOffline;
                            break;
                        }
                        if (read == 0)
                            break;
                        got += read;
                        if (got > limit)
                        {
                            result = Fetch::kTooBig;
                            break;
                        }
                        if (!sink(buffer, read))
                        {
                            result = Fetch::kSink;
                            break;
                        }
                    }
                }
            }
        }
        if (request)
            WinHttpCloseHandle(request);
        if (connection)
            WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    std::wstring Wide(const std::string &utf8)
    {
        return Utf8ToWide(utf8);
    }
}

struct Updater::Shared
{
    std::mutex mutex;
    HWND hWnd = nullptr;
    UINT message = 0;
    std::atomic<bool> cancel{ false };
    bool busy = false;                 // a worker is running
    Snapshot snapshot;
    update_check::release release;
    Pending pending;
};

Updater::Updater() : m_shared(std::make_shared<Shared>())
{
}

Updater::~Updater()
{
    Detach();
}

void Updater::Attach(HWND hWnd, UINT message)
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->hWnd = hWnd;
    m_shared->message = message;
}

// The worker is detached, like the other background jobs: a download still going when the window closes ends at its next
// read, and only touches what it keeps alive itself.
void Updater::Detach()
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    m_shared->hWnd = nullptr;
    m_shared->cancel = true;
}

bool Updater::AutoCheck() const
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kAutoValue, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
        && value != 0;
}

void Updater::SetAutoCheck(bool on)
{
    DWORD value = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kAutoValue, REG_DWORD, &value, sizeof(value));
}

bool Updater::DueForAutoCheck() const
{
    if (!AutoCheck())
        return false;
    std::time_t last = 0;
    std::time_t now = std::time(nullptr);
    return !ReadRegistryTime(last) || last > now || now - last >= kWeek;
}

Updater::Snapshot Updater::Get() const
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    return m_shared->snapshot;
}

Updater::Pending Updater::GetPending() const
{
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    return m_shared->pending;
}

namespace
{
    void Tell(Updater::Shared &shared)
    {
        // The caller holds the lock.
        if (shared.hWnd)
            PostMessageW(shared.hWnd, shared.message, 0, 0);
    }
}

namespace
{
    template <class Fn>
    void Change(Updater::Shared &shared, Fn change)
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        change(shared);
        Tell(shared);
    }
}

void Updater::Check(bool automatic)
{
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        if (m_shared->busy || m_shared->snapshot.state == State::kDownloading)
            return;
        m_shared->busy = true;
        m_shared->cancel = false;
        m_shared->snapshot = Snapshot();
        m_shared->snapshot.state = State::kChecking;
        Tell(*m_shared);
    }
    std::thread(RunCheck, m_shared, automatic).detach();
}

void Updater::RunCheck(std::shared_ptr<Shared> shared, bool automatic)
{
    Snapshot result;
    update_check::release release;
    std::string body;
    DWORD status = 0;
    Fetch fetched = HttpGet(kLatestUrl, kMaxRelease, shared->cancel, status, nullptr, [&](const char *data, size_t size)
    {
        body.append(data, size);
        return true;
    });

    int current[3] = {}, latest[3] = {};
    bool known = update_check::parse_version(CORPUS_VERSION, current);
    if (fetched == Fetch::kCancelled)
    {
        result.state = State::kIdle;
    }
    else if (fetched == Fetch::kStatus && (status == 403 || status == 429))
    {
        result.state = State::kError;
        result.error = strings::kUpdateLimited;
    }
    else if (fetched == Fetch::kStatus && status == 404)
    {
        result.state = State::kError;
        result.error = strings::kUpdateNoRelease;
    }
    else if (fetched != Fetch::kOk)
    {
        result.state = State::kError;
        result.error = ErrorOffline();
    }
    else if (!known || !update_check::parse_release(body, release) || !update_check::parse_version(release.tag, latest)
        || !update_check::is_repository_url(release.page_url, kRepository))
    {
        result.state = State::kError;
        result.error = strings::kUpdateUnreadable;
    }
    else
    {
        WriteRegistryTime(std::time(nullptr));
        if (update_check::compare_versions(latest, current) > 0)
        {
            result.state = State::kAvailable;
            result.version = std::to_wstring(latest[0]) + L"." + std::to_wstring(latest[1]) + L"." + std::to_wstring(latest[2]);
            result.pageUrl = Wide(release.page_url);
        }
        else
            result.state = State::kUpToDate;
    }
    // The weekly check says nothing when it can't reach GitHub: the person didn't ask for it right now.
    if (automatic && result.state == State::kError)
        result = Snapshot();

    Change(*shared, [&](Shared &s)
    {
        s.busy = false;
        s.snapshot = result;
        s.release = release;
    });
}

void Updater::Download()
{
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        if (m_shared->busy || m_shared->snapshot.state != State::kAvailable)
            return;
        m_shared->busy = true;
        m_shared->cancel = false;
        m_shared->snapshot.state = State::kDownloading;
        m_shared->snapshot.percent = 0;
        Tell(*m_shared);
    }
    std::thread(RunDownload, m_shared).detach();
}

bool Updater::IsInstalledCopy()
{
    std::wstring exe = ExePath();
    std::wstring base = LocalAppData();
    if (exe.empty() || base.empty())
        return false;
    fs::path folder = fs::path(exe).parent_path();
    fs::path installed = fs::path(base) / L"Programs" / L"Corpus";
    return _wcsicmp(folder.lexically_normal().wstring().c_str(), installed.lexically_normal().wstring().c_str()) == 0;
}

std::wstring Updater::UpdatesFolder()
{
    std::wstring base = LocalAppData();
    return base.empty() ? std::wstring() : (fs::path(base) / L"Corpus" / L"updates").wstring();
}

void Updater::RunDownload(std::shared_ptr<Shared> shared)
{
    update_check::release release;
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        release = shared->release;
        snapshot = shared->snapshot;
    }

    const bool installed = IsInstalledCopy();
    const std::string assetName = installed ? "Corpus-Setup.exe" : "Corpus.exe";
    const update_check::asset *program = update_check::find_asset(release, assetName);
    const update_check::asset *sums = update_check::find_asset(release, "SHA256SUMS");
    const update_check::asset *signature = update_check::find_asset(release, "SHA256SUMS.sig");
    const std::wstring folder = UpdatesFolder();

    Snapshot result = snapshot;
    result.percent = 0;
    Pending pending;
    pending.installed = installed;

    auto fail = [&](const std::wstring &why)
    {
        result.state = State::kError;
        result.error = why;
        std::error_code ec;
        if (!folder.empty())
            fs::remove_all(folder, ec);
    };

    if (!program || !sums || !signature || folder.empty()
        || !update_check::is_repository_url(program->url, kRepository) || !update_check::is_repository_url(sums->url, kRepository)
        || !update_check::is_repository_url(signature->url, kRepository))
    {
        result.state = State::kError;
        result.error = program && sums && signature ? std::wstring(strings::kUpdateSaveFailed) : std::wstring(strings::kUpdateNoDownload);
    }
    else
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
        fs::create_directories(folder, ec);
        DWORD status = 0;
        std::string sumsText, signatureBytes;
        bool ok = !ec;
        if (!ok)
            fail(strings::kUpdateSaveFailed);

        // The small files first: the signature has to hold before the big download is worth its time.
        if (ok)
        {
            Fetch a = HttpGet(Wide(sums->url), kMaxSums, shared->cancel, status, nullptr, [&](const char *d, size_t n)
            {
                sumsText.append(d, n);
                return true;
            });
            Fetch b = a == Fetch::kOk ? HttpGet(Wide(signature->url), kMaxSignature, shared->cancel, status, nullptr,
                [&](const char *d, size_t n)
                {
                    signatureBytes.append(d, n);
                    return true;
                }) : a;
            if (a != Fetch::kOk || b != Fetch::kOk)
            {
                ok = false;
                fail(a == Fetch::kCancelled || b == Fetch::kCancelled ? std::wstring() : ErrorOffline());
            }
        }
        std::string expected;
        int current[3] = {};
        if (ok && (!update_verify::VerifySignature(kUpdatePublicKey, sizeof(kUpdatePublicKey), sumsText.data(), sumsText.size(),
                reinterpret_cast<const unsigned char*>(signatureBytes.data()), signatureBytes.size())
            || !update_check::parse_version(CORPUS_VERSION, current)
            // The signed version must be the release's and newer than this one: an old signed release is not an update.
            || !update_check::update_acceptable(sumsText, release.tag, current)
            || !update_check::find_hash(sumsText, assetName, expected)))
        {
            ok = false;
            fail(strings::kUpdateRejected);
        }

        // The program itself, hashed as it is written.
        const std::wstring file = (fs::path(folder) / Wide(assetName)).wstring();
        if (ok)
        {
            HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE)
            {
                ok = false;
                fail(strings::kUpdateSaveFailed);
            }
            else
            {
                update_verify::Sha256 hash;
                unsigned long long total = 0, got = 0;
                bool writeFailed = false;
                Fetch fetched = HttpGet(Wide(program->url), kMaxProgram, shared->cancel, status,
                    [&](unsigned long long size) { total = size; },
                    [&](const char *d, size_t n)
                    {
                        DWORD written = 0;
                        if (!hash.Add(d, n) || !WriteFile(out, d, static_cast<DWORD>(n), &written, nullptr) || written != n)
                        {
                            writeFailed = true;
                            return false;
                        }
                        got += n;
                        if (total > 0)
                        {
                            int percent = static_cast<int>(got * 100 / total);
                            std::lock_guard<std::mutex> lock(shared->mutex);
                            if (percent != shared->snapshot.percent)
                            {
                                shared->snapshot.percent = percent;
                                Tell(*shared);
                            }
                        }
                        return true;
                    });
                CloseHandle(out);
                if (fetched != Fetch::kOk)
                {
                    ok = false;
                    fail(fetched == Fetch::kCancelled ? std::wstring()
                        : writeFailed ? std::wstring(strings::kUpdateSaveFailed) : ErrorOffline());
                }
                else if (hash.Finish() != expected)
                {
                    ok = false;
                    fail(strings::kUpdateRejected);
                }
            }
        }
        if (ok)
        {
            result.state = State::kReady;
            result.percent = 100;
            pending.file = file;
        }
    }

    if (result.state == State::kError && result.error.empty())
    {
        // Cancelled (the window closed): back to where it was.
        result = snapshot;
        result.state = State::kAvailable;
    }
    Change(*shared, [&](Shared &s)
    {
        s.busy = false;
        s.snapshot = result;
        s.pending = pending;
    });
}

bool Updater::CanApply(const Pending &pending, std::wstring &why)
{
    if (pending.file.empty() || GetFileAttributesW(pending.file.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        why = strings::kUpdateGone;
        return false;
    }
    if (pending.installed)
        return true;
    // Portable: the running exe is renamed and the new one put in its place, so the folder has to take new files.
    std::wstring exe = ExePath();
    if (exe.empty())
    {
        why = strings::kUpdateNotWritable;
        return false;
    }
    std::wstring probe = (fs::path(exe).parent_path() / L"corpus-write-test.tmp").wstring();
    HANDLE handle = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        why = strings::kUpdateNotWritable;
        return false;
    }
    CloseHandle(handle);
    return true;
}

namespace
{
    bool Start(const std::wstring &program, std::wstring commandLine, const wchar_t *directory)
    {
        STARTUPINFOW startup = { sizeof(startup) };
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(program.c_str(), &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr, directory, &startup, &process))
            return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }
}

bool Updater::Apply(const Pending &pending, const std::wstring &libraryPath, std::wstring &error)
{
    if (pending.installed)
    {
        // The installer waits for the exe to be free, replaces it, and starts Corpus again (silent, with /RELAUNCH).
        std::wstring command = L"\"" + pending.file + L"\" /S /RELAUNCH";
        if (Start(pending.file, command, fs::path(pending.file).parent_path().c_str()))
            return true;
        error = strings::kUpdateInstallerFailed;
        return false;
    }

    std::wstring exe = ExePath();
    std::wstring old = exe + L".old";
    DeleteFileW(old.c_str());
    if (exe.empty() || !MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        error = strings::kUpdateNotWritable;
        return false;
    }
    if (!MoveFileExW(pending.file.c_str(), exe.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
    {
        // Put the old one back, so Corpus is still where it was.
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        error = strings::kUpdateNotWritable;
        return false;
    }
    std::wstring command = L"\"" + exe + L"\"";
    if (!libraryPath.empty())
        command += L" --library \"" + libraryPath + L"\"";
    if (!Start(exe, command, nullptr))
    {
        error = strings::kUpdateStartFailed;
        return false;
    }
    return true;
}

void Updater::CleanUp()
{
    std::wstring exe = ExePath();
    if (!exe.empty())
        DeleteFileW((exe + L".old").c_str());
    std::wstring folder = UpdatesFolder();
    if (!folder.empty())
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
}
