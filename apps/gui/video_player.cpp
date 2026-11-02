#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "video_player.hpp"
#include "file_kind.hpp"
#include "resource.h"
#include "strings.hpp"
#include "ui_util.hpp"
#include "../../include/audio_depth.hpp"
#include "../../include/file_identity.hpp"
#include <mpv/client.h>
#include <compressapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>

std::wstring LoadCustomLibmpvPath()
{
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Corpus", L"LibmpvPath", RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return L"";
    return buffer;
}

void SaveCustomLibmpvPath(const std::wstring &path)
{
    if (path.empty())
        RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Corpus", L"LibmpvPath");
    else
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Corpus", L"LibmpvPath", REG_SZ, path.c_str(),
            static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
}

namespace
{
    const wchar_t *kWindowClass = L"Corpus Video";
    // The viewer's backdrop (theme::kViewerBackdrop), behind the video and around it.
    const COLORREF kBackdrop = RGB(0x0E, 0x0D, 0x10);
    const char *kBackdropHex = "#0E0D10";
    // How long closing the window waits for libmpv to end.
    const DWORD kShutdownWaitMs = 3000;

    // The libmpv functions the player uses, found in the DLL once it is loaded.
    struct Mpv
    {
        decltype(&mpv_client_api_version) client_api_version;
        decltype(&mpv_create) create;
        decltype(&mpv_initialize) initialize;
        decltype(&mpv_terminate_destroy) terminate_destroy;
        decltype(&mpv_set_option_string) set_option_string;
        decltype(&mpv_command) command;
        decltype(&mpv_command_node) command_node;
        decltype(&mpv_command_async) command_async;
        decltype(&mpv_set_property_async) set_property_async;
        decltype(&mpv_set_property_string) set_property_string;
        decltype(&mpv_get_property) get_property;
        decltype(&mpv_observe_property) observe_property;
        decltype(&mpv_free_node_contents) free_node_contents;
        decltype(&mpv_free) free;
        decltype(&mpv_wait_event) wait_event;
        decltype(&mpv_set_wakeup_callback) set_wakeup_callback;
    };

#ifndef CORPUS_LIBMPV_TAG
    // A build without the DLL built in loads the one next to the .exe.
    const wchar_t *kDllName = L"libmpv-2.dll";

    std::wstring ExeFolder()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD length = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
            if (length == 0)
                return L"";
            if (length < path.size())
            {
                path.resize(length);
                break;
            }
            path.resize(path.size() * 2);
        }
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
    }
#endif

#ifdef CORPUS_LIBMPV_TAG
    // The DLL built into the .exe, named after its hash when written out, so a build with another one writes its own.
    const wchar_t *kBuiltInDllName = L"libmpv-2-" CORPUS_LIBMPV_TAG ".dll";

    // The DLL is in the .exe compressed (tools/pack-libmpv.py): its size, then each 32 KiB of it as a block of its own.
    const DWORD kUnpackedBlock = 32768;

    // Only finds it: its bytes are read from the .exe when it is written out.
    HRSRC BuiltInDll()
    {
        return FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_LIBMPV), RT_RCDATA);
    }

    // The Compression API, loaded when the DLL is first written out, so Corpus doesn't load cabinet.dll at startup.
    struct Unpacker
    {
        HMODULE module = nullptr;
        DECOMPRESSOR_HANDLE handle = nullptr;
        decltype(&Decompress) decompress = nullptr;
        decltype(&CloseDecompressor) close = nullptr;

        Unpacker()
        {
            module = LoadLibraryExW(L"cabinet.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module)
                return;
            auto create = reinterpret_cast<decltype(&CreateDecompressor)>(
                reinterpret_cast<void(*)()>(GetProcAddress(module, "CreateDecompressor")));
            decompress = reinterpret_cast<decltype(&Decompress)>(reinterpret_cast<void(*)()>(GetProcAddress(module, "Decompress")));
            close = reinterpret_cast<decltype(&CloseDecompressor)>(
                reinterpret_cast<void(*)()>(GetProcAddress(module, "CloseDecompressor")));
            if (!create || !decompress || !close || !create(COMPRESS_ALGORITHM_MSZIP | COMPRESS_RAW, nullptr, &handle))
                handle = nullptr;
        }

        ~Unpacker()
        {
            if (handle)
                close(handle);
            if (module)
                FreeLibrary(module);
        }

        Unpacker(const Unpacker&) = delete;
        Unpacker &operator=(const Unpacker&) = delete;
    };

    // Where the built-in DLL is written out: %LOCALAPPDATA%\Corpus, beside the library, or else a Corpus folder in
    // %TEMP% when that one can't be written to.
    std::vector<std::wstring> BuiltInDllFolders()
    {
        std::vector<std::wstring> folders;
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
            folders.push_back(std::wstring(localAppData) + L"\\Corpus\\");
        CoTaskMemFree(localAppData);
        wchar_t temp[MAX_PATH + 1];
        DWORD length = GetTempPathW(MAX_PATH + 1, temp);
        if (length > 0 && length <= MAX_PATH)
            folders.push_back(std::wstring(temp, length) + L"Corpus\\");
        return folders;
    }

    bool HasSize(const std::wstring &path, DWORD size)
    {
        WIN32_FILE_ATTRIBUTE_DATA info;
        return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && info.nFileSizeHigh == 0 && info.nFileSizeLow == size;
    }

    // Writes the DLL to a file of its own first and renames that, so the DLL's name never shows a half-written file:
    // not after a crash, nor to another Corpus writing it at the same time.
    bool WriteBuiltInDll(const std::wstring &folder, const std::wstring &path, const BYTE *packed, size_t packedSize,
        DWORD size)
    {
        Unpacker unpacker;
        if (!unpacker.handle)
            return false;
        CreateDirectoryW(folder.c_str(), nullptr);
        std::wstring part = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".part";
        HANDLE file = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        bool ok = true;
        std::vector<BYTE> block(kUnpackedBlock);
        size_t read = sizeof(uint64_t);
        DWORD written = 0;
        while (ok && written < size)
        {
            DWORD expected = std::min<DWORD>(kUnpackedBlock, size - written);
            uint32_t length = 0;
            ok = read + sizeof(length) <= packedSize;
            if (ok)
                memcpy(&length, packed + read, sizeof(length));
            read += sizeof(length);
            SIZE_T got = 0;
            ok = ok && length <= packedSize - read &&
                unpacker.decompress(unpacker.handle, const_cast<BYTE*>(packed + read), length, block.data(), expected, &got) && got == expected;
            read += length;
            DWORD chunk = 0;
            ok = ok && WriteFile(file, block.data(), expected, &chunk, nullptr) && chunk == expected;
            written += expected;
        }
        // On the disk before it takes the DLL's name: a power cut can't leave a file of the right size but empty.
        ok = ok && read == packedSize && FlushFileBuffers(file);
        CloseHandle(file);
        if (ok && MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            return true;
        DeleteFileW(part.c_str());
        // Another Corpus may have written it meanwhile, and be using it.
        return HasSize(path, size);
    }

    // Deletes the DLLs written out by other builds, and the .part files a crash left half written. One that another
    // Corpus still has loaded, or is writing, stays until a later time.
    void DeleteOtherBuiltInDlls(const std::wstring &folder)
    {
        WIN32_FIND_DATAW found;
        HANDLE find = FindFirstFileW((folder + L"libmpv-2-*").c_str(), &found);
        if (find == INVALID_HANDLE_VALUE)
            return;
        do
        {
            if (_wcsicmp(found.cFileName, kBuiltInDllName) != 0)
                DeleteFileW((folder + found.cFileName).c_str());
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }

    // The path of the built-in DLL written out, writing it the first time. Empty if it can't be written anywhere.
    std::wstring WriteOutBuiltInDll()
    {
        HRSRC resource = BuiltInDll();
        HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
        const BYTE *packed = loaded ? static_cast<const BYTE*>(LockResource(loaded)) : nullptr;
        size_t packedSize = resource ? SizeofResource(nullptr, resource) : 0;
        uint64_t unpackedSize = 0;
        if (!packed || packedSize < sizeof(unpackedSize))
            return L"";
        memcpy(&unpackedSize, packed, sizeof(unpackedSize));
        if (unpackedSize == 0 || unpackedSize > MAXDWORD)
            return L"";
        DWORD size = static_cast<DWORD>(unpackedSize);
        for (const std::wstring &folder : BuiltInDllFolders())
        {
            std::wstring path = folder + kBuiltInDllName;
            if (HasSize(path, size))
                return path;
            if (WriteBuiltInDll(folder, path, packed, packedSize, size))
            {
                DeleteOtherBuiltInDlls(folder);
                return path;
            }
        }
        return L"";
    }
#endif

    // The libmpv-2.dll chosen in Settings, read once: a change applies the next time Corpus starts. Empty if none.
    const std::wstring &CustomDll()
    {
        static const std::wstring path = LoadCustomLibmpvPath();
        return path;
    }

    bool CustomDllPresent()
    {
        const std::wstring &path = CustomDll();
        return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool DllPresent()
    {
        if (CustomDllPresent())
            return true;
#ifdef CORPUS_LIBMPV_TAG
        static const bool present = BuiltInDll() != nullptr;
#else
        static const bool present = GetFileAttributesW((ExeFolder() + kDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
#endif
        return present;
    }

    // Finds the functions in 'dll'. False if one is missing or it isn't client API 2.x.
    bool FillMpv(HMODULE dll, Mpv &api)
    {
        bool all = true;
        auto find = [&](auto &function, const char *name)
        {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                reinterpret_cast<void(*)()>(GetProcAddress(dll, name)));
            all = all && function;
        };
        find(api.client_api_version, "mpv_client_api_version");
        find(api.create, "mpv_create");
        find(api.initialize, "mpv_initialize");
        find(api.terminate_destroy, "mpv_terminate_destroy");
        find(api.set_option_string, "mpv_set_option_string");
        find(api.command, "mpv_command");
        find(api.command_node, "mpv_command_node");
        find(api.command_async, "mpv_command_async");
        find(api.set_property_async, "mpv_set_property_async");
        find(api.set_property_string, "mpv_set_property_string");
        find(api.get_property, "mpv_get_property");
        find(api.observe_property, "mpv_observe_property");
        find(api.free_node_contents, "mpv_free_node_contents");
        find(api.free, "mpv_free");
        find(api.wait_event, "mpv_wait_event");
        find(api.set_wakeup_callback, "mpv_set_wakeup_callback");
        return all && (api.client_api_version() >> 16) == 2;
    }

    // Loads the DLL by its full path, never from wherever the search path leads: the one chosen in Settings, if it
    // loads, and otherwise the one built into the .exe, or in a build without one, the one next to the .exe. A DLL
    // next to an .exe that has one built in is ignored, so an old copy left there can't stand in for it. Null if
    // none loads or is a libmpv this code knows (client API 2.x). Tried once.
    const Mpv *LoadMpv()
    {
        static Mpv api;
        static bool tried = false;
        static bool loaded = false;
        if (tried)
            return loaded ? &api : nullptr;
        tried = true;

        const DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
        if (CustomDllPresent())
        {
            // No checks beyond the exports below: the file is whatever the user chose. One that doesn't work is
            // dropped and the built-in one used.
            if (HMODULE custom = LoadLibraryExW(CustomDll().c_str(), nullptr, flags))
            {
                if (FillMpv(custom, api))
                {
                    loaded = true;
                    return &api;
                }
                FreeLibrary(custom);
            }
        }
#ifdef CORPUS_LIBMPV_TAG
        std::wstring path = WriteOutBuiltInDll();
        HMODULE dll = path.empty() ? nullptr : LoadLibraryExW(path.c_str(), nullptr, flags);
        // A copy that doesn't load is damaged: the next session writes it again.
        if (!dll && !path.empty())
            DeleteFileW(path.c_str());
#else
        std::wstring path = ExeFolder() + kDllName;
        HMODULE dll = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ?
            LoadLibraryExW(path.c_str(), nullptr, flags) : nullptr;
#endif
        if (!dll)
            return nullptr;
        if (!FillMpv(dll, api))
        {
            FreeLibrary(dll);
            return nullptr;
        }
        loaded = true;
        return &api;
    }

    // The properties the player follows, by the id libmpv reports them with.
    enum Observed : uint64_t
    {
        kDuration = 1,
        kPause,
        kEofReached,
        kVolume,
        kMute,
        kTrackList,
        kAid,
        kSid,
        kWidth,
        kHeight,
        kAudioDisplay,  // the reply of an audio-display change, not a property followed
    };

    // A node map, for the commands that take named arguments.
    class NodeMap
    {
    public:
        void Add(const char *key, const std::string &value)
        {
            m_strings.push_back(value);
            mpv_node node = {};
            node.format = MPV_FORMAT_STRING;
            m_keys.push_back(key);
            m_values.push_back(node);
        }
        void Add(const char *key, int64_t value)
        {
            mpv_node node = {};
            node.format = MPV_FORMAT_INT64;
            node.u.int64 = value;
            m_keys.push_back(key);
            m_values.push_back(node);
            m_strings.emplace_back();
        }
        mpv_node *Node()
        {
            // The strings are in place now: pointers to them hold until the next Add.
            for (size_t i = 0; i < m_values.size(); i++)
            {
                if (m_values[i].format == MPV_FORMAT_STRING)
                    m_values[i].u.string = const_cast<char*>(m_strings[i].c_str());
            }
            m_list.num = static_cast<int>(m_values.size());
            m_list.values = m_values.data();
            m_list.keys = const_cast<char**>(m_keys.data());
            m_node.format = MPV_FORMAT_NODE_MAP;
            m_node.u.list = &m_list;
            return &m_node;
        }

    private:
        std::vector<std::string> m_strings;  // by entry; empty for numbers
        std::vector<const char*> m_keys;
        std::vector<mpv_node> m_values;
        mpv_node_list m_list = {};
        mpv_node m_node = {};
    };

    const mpv_node *Find(const mpv_node_list *map, const char *key)
    {
        for (int i = 0; i < map->num; i++)
        {
            if (strcmp(map->keys[i], key) == 0)
                return &map->values[i];
        }
        return nullptr;
    }

    std::string StringAt(const mpv_node_list *map, const char *key)
    {
        const mpv_node *node = Find(map, key);
        return node && node->format == MPV_FORMAT_STRING && node->u.string ? node->u.string : "";
    }

    int64_t NumberAt(const mpv_node_list *map, const char *key)
    {
        const mpv_node *node = Find(map, key);
        return node && node->format == MPV_FORMAT_INT64 ? node->u.int64 : 0;
    }

    // A tag, whatever the case of its key: Vorbis comments say ARTIST, ID3 says artist.
    std::wstring TagAt(const mpv_node_list *map, const char *key)
    {
        for (int i = 0; i < map->num; i++)
        {
            const mpv_node &node = map->values[i];
            if (_stricmp(map->keys[i], key) == 0 && node.format == MPV_FORMAT_STRING && node.u.string)
                return Utf8ToWide(node.u.string);
        }
        return L"";
    }

    bool FlagAt(const mpv_node_list *map, const char *key)
    {
        const mpv_node *node = Find(map, key);
        return node && node->format == MPV_FORMAT_FLAG && node->u.flag;
    }

    LRESULT CALLBACK VideoWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    // The end of libmpv, on a thread of its own: it waits for libmpv's threads, and the one with the video's
    // window may need the window's thread to handle a message meanwhile.
    struct Ending
    {
        const Mpv *api;
        mpv_handle *mpv;
    };

    DWORD WINAPI EndMpv(void *data)
    {
        Ending *ending = static_cast<Ending*>(data);
        ending->api->terminate_destroy(ending->mpv);
        delete ending;
        return 0;
    }
}

bool IsPlayableVideo(const std::string &path)
{
    static const std::unordered_set<std::string> extensions = { ".mkv", ".mp4", ".m4v", ".mov", ".avi", ".webm",
        ".wmv", ".asf", ".flv", ".f4v", ".mpg", ".mpeg", ".m2v", ".ts", ".mts", ".m2ts", ".vob", ".ogv", ".3gp",
        ".3g2", ".divx", ".rm", ".rmvb" };

    // Like IsDecodableImage: on the UTF-8 path, lowering ASCII letters only.
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("\\/");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return false;
    std::string extension = path.substr(dot);
    for (char &c : extension)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return extensions.count(extension) != 0 && DllPresent();
}

bool IsPlayableAudio(const std::string &path)
{
    return KindOf(path) == FileKind::kAudio && DllPresent();
}

void VideoPlayer::OnWakeup(void *data)
{
    Wakeup *wakeup = static_cast<Wakeup*>(data);
    if (!wakeup->pending.exchange(true))
        PostMessage(wakeup->hWnd, kVideoEvent, 0, 0);
}

// Loads libmpv and starts it on a window of its own. The options leave out everything of mpv the player
// doesn't use: its keys and mouse bindings, its on-screen messages and controller, its scripts and config files.
bool VideoPlayer::Start(HWND parent)
{
    if (m_mpv)
        return true;
    const Mpv *api = LoadMpv();
    if (!api)
        return false;

    if (!m_window)
    {
        HINSTANCE instance = GetModuleHandle(nullptr);
        WNDCLASSW wndClass = {};
        wndClass.lpfnWndProc = VideoWindowProc;
        wndClass.hInstance = instance;
        wndClass.lpszClassName = kWindowClass;
        wndClass.hbrBackground = CreateSolidBrush(kBackdrop);
        RegisterClassW(&wndClass);
        // Disabled, so the mouse goes through it (and through libmpv's window inside it) to the main window.
        m_window = CreateWindowExW(WS_EX_NOPARENTNOTIFY, kWindowClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_DISABLED,
            0, 0, 0, 0, parent, nullptr, instance, nullptr);
        if (!m_window)
            return false;
    }

    mpv_handle *mpv = api->create();
    if (!mpv)
        return false;
    const char *options[][2] =
    {
        { "wid", nullptr },
        { "vo", "gpu-next,gpu" },
        { "hwdec", "auto-safe" },
        { "audio-channels", "auto-safe" },
        { "idle", "yes" },
        { "keep-open", "yes" },
        { "force-window", "no" },
        { "audio-display", "no" },
        { "volume-max", "100" },
        { "sub-auto", "fuzzy" },
        { "background-color", kBackdropHex },
        { "osd-level", "0" },
        { "cursor-autohide", "no" },
        { "input-default-bindings", "no" },
        { "input-builtin-bindings", "no" },
        { "input-vo-keyboard", "no" },
        { "input-cursor", "no" },
        { "input-media-keys", "no" },
        { "osc", "no" },
        { "ytdl", "no" },
        { "load-scripts", "no" },
        { "load-stats-overlay", "no" },
        { "load-console", "no" },
        { "load-auto-profiles", "no" },
        { "load-select", "no" },
        { "load-positioning", "no" },
        { "load-commands", "no" },
        { "load-context-menu", "no" },
    };
    std::string wid = std::to_string(static_cast<int64_t>(reinterpret_cast<intptr_t>(m_window)));
    options[0][1] = wid.c_str();
    // Options an older or newer libmpv doesn't know are left out; only the window is a must.
    for (const auto &option : options)
    {
        if (api->set_option_string(mpv, option[0], option[1]) < 0 && option == options[0])
        {
            api->terminate_destroy(mpv);
            return false;
        }
    }
    // The volume set before libmpv started (the one kept from the last run).
    std::string volume = std::to_string(m_volume);
    api->set_option_string(mpv, "volume", volume.c_str());
    api->set_option_string(mpv, "mute", m_muted ? "yes" : "no");
    if (api->initialize(mpv) < 0)
    {
        api->terminate_destroy(mpv);
        return false;
    }

    api->observe_property(mpv, kDuration, "duration", MPV_FORMAT_DOUBLE);
    api->observe_property(mpv, kPause, "pause", MPV_FORMAT_FLAG);
    api->observe_property(mpv, kEofReached, "eof-reached", MPV_FORMAT_FLAG);
    api->observe_property(mpv, kVolume, "volume", MPV_FORMAT_DOUBLE);
    api->observe_property(mpv, kMute, "mute", MPV_FORMAT_FLAG);
    api->observe_property(mpv, kTrackList, "track-list", MPV_FORMAT_NONE);
    // "no" doesn't convert to a number: that comes as MPV_FORMAT_NONE.
    api->observe_property(mpv, kAid, "aid", MPV_FORMAT_INT64);
    api->observe_property(mpv, kSid, "sid", MPV_FORMAT_INT64);
    api->observe_property(mpv, kWidth, "width", MPV_FORMAT_INT64);
    api->observe_property(mpv, kHeight, "height", MPV_FORMAT_INT64);

    m_wakeup->hWnd = parent;
    api->set_wakeup_callback(mpv, &VideoPlayer::OnWakeup, m_wakeup.get());
    m_mpv = mpv;
    ApplyVideoQuality();
    ApplyAudioOutput();
    return true;
}

bool VideoPlayer::Open(HWND parent, const RECT &bounds, const std::string &path, bool paused, double start)
{
    Reset();
    if (file_missing(path))
    {
        Stop();
        m_status = Status::kMissing;
        return true;
    }
    if (!Start(parent))
    {
        m_status = Status::kFailed;
        return false;
    }
    SetBounds(bounds);
    m_status = Status::kLoading;
    // A music file shows its embedded cover, if it has one; a video never shows one instead of its picture.
    m_audioFile = IsPlayableAudio(path);
    if (m_audioFile)
    {
        // The depth is what the file's header declares, or nothing: libmpv's decoded format says how the decoder
        // hands the samples over, not how many bits the file has.
        audio_depth depth = audio_read_depth_file(path);
        m_bitDepth = depth.bits;
        m_floatDepth = depth.is_float;
    }
    // Every change goes in asynchronously: while libmpv waits on a busy audio device it answers nothing, and a
    // call that waited for it would freeze the window. An older libmpv without "embedded-first" answers with an error.
    SetAsync("audio-display", m_audioFile ? "embedded-first" : "no", m_audioFile ? kAudioDisplay : 0);
    m_fileLoaded = false;
    m_entry = 0;
    m_loadAnswered = false;
    // The window shows once the file is open: until then the viewer says "Loading" if it takes a while.
    ShowWindow(m_window, SW_HIDE);
    // libmpv keeps the pause from the file before, and pauses at the end of one: each video starts playing,
    // unless it was asked to wait.
    SetAsync("pause", paused ? "yes" : "no");
    m_paused = paused;
    // Until the first time-pos arrives, the position is the start asked for.
    m_position = start > 0.0 ? start : 0.0;
    char from[32] = "none";
    if (start > 0.0)
        snprintf(from, sizeof(from), "%.3f", start);
    SetAsync("start", from);
    double speed = m_speed;
    LoadMpv()->set_property_async(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE, &speed);
    ApplyAudioOutput();
    const char *command[] = { "loadfile", path.c_str(), "replace", nullptr };
    LoadMpv()->command_async(m_mpv, ++m_load, command);
    return true;
}

void VideoPlayer::SetAsync(const char *name, const char *value, uint64_t reply)
{
    char *text = const_cast<char*>(value);
    LoadMpv()->set_property_async(m_mpv, reply, name, MPV_FORMAT_STRING, &text);
}

// The file is open and the output is settled: shows it and reads what libmpv knows of it.
void VideoPlayer::BeginPlaying()
{
    m_status = Status::kPlaying;
    if (m_appliedBitPerfect)
    {
        // The tracks changed while libmpv couldn't answer (OnEvent skipped them).
        ReadTracks();
        // Exclusive output that failed leaves libmpv with no output at all.
        const Mpv *api = LoadMpv();
        char *ao = nullptr;
        if (api->get_property(m_mpv, "current-ao", MPV_FORMAT_STRING, &ao) < 0 || !ao)
            m_outputBusy = m_audioFile;
        if (ao)
            api->free(ao);
    }
    if (m_audioFile)
        ReadMusicTags();
    ChooseSubtitle();
    ShowVideoWindow();
}

void VideoPlayer::ApplyVideoQuality()
{
    // A profile sets a bundle at once; the rest undo what the other profile may have left on.
    SetAsync("profile", m_bestVideo ? "high-quality" : "fast");
    SetAsync("hdr-compute-peak", m_bestVideo ? "yes" : "no");
    // The profile turns on frame blending and display-resample, which change the picture and resample the sound.
    SetAsync("deband", "no");
    SetAsync("interpolation", "no");
    SetAsync("video-sync", "audio");
    SetAsync("dither-depth", m_bestVideo ? "auto" : "no");
}

void VideoPlayer::ApplyAudioOutput()
{
    m_appliedBitPerfect = m_bitPerfect;
    SetAsync("audio-exclusive", m_bitPerfect ? "yes" : "no");
    SetAsync("audio-samplerate", "0");
    SetAsync("audio-format", "no");
    // Exclusive output keeps libmpv's own processing out of the way: no ReplayGain, no filters, no downmix. With the
    // volume at 100, unmuted and at 1x, libmpv adds no gain and no resampler either (CheckBitPerfect confirms it).
    SetAsync("replaygain", "no");
    SetAsync("audio-normalize-downmix", "no");
    SetAsync("af", "");
    // The device chosen for this mode, or the system default when none was chosen or it is no longer there.
    const std::string &wanted = m_bitPerfect ? m_exclusiveDevice : m_sharedDevice;
    std::string device = "auto";
    if (!wanted.empty())
    {
        // From the list kept: asking libmpv now could wait on a busy output.
        if (!m_devicesKnown)
            AudioDevices();
        for (const AudioDevice &known : m_devices)
        {
            if (known.name == wanted)
                device = wanted;
        }
    }
    SetAsync("audio-device", device.c_str());
    // The trailing comma falls back to the other outputs, so a device held by another app doesn't mute the video.
    SetAsync("ao", "wasapi,");
}

std::vector<VideoPlayer::AudioDevice> VideoPlayer::AudioDevices()
{
    std::vector<AudioDevice> devices;
    const Mpv *api = LoadMpv();
    if (!api)
        return devices;
    // While libmpv opens a file in exclusive mode it may not answer for seconds: the list kept stands in.
    if (m_devicesKnown && m_mpv && m_appliedBitPerfect && m_status == Status::kLoading)
        return m_devices;
    // Before the first file there is no player yet: a bare libmpv with nothing loaded answers just as well.
    mpv_handle *mpv = m_mpv;
    if (!mpv)
    {
        mpv = api->create();
        if (!mpv)
            return devices;
        for (const char *option : { "config", "load-scripts", "idle", "audio-display", "ytdl" })
            api->set_option_string(mpv, option, option[0] == 'i' ? "yes" : "no");
        api->set_option_string(mpv, "vo", "null");
        if (api->initialize(mpv) < 0)
        {
            api->terminate_destroy(mpv);
            return devices;
        }
    }
    mpv_node list;
    if (api->get_property(mpv, "audio-device-list", MPV_FORMAT_NODE, &list) >= 0)
    {
        if (list.format == MPV_FORMAT_NODE_ARRAY)
        {
            for (int i = 0; i < list.u.list->num; i++)
            {
                const mpv_node &item = list.u.list->values[i];
                if (item.format != MPV_FORMAT_NODE_MAP)
                    continue;
                std::string name = StringAt(item.u.list, "name");
                if (name.compare(0, 7, "wasapi/") != 0)
                    continue;
                std::string description = StringAt(item.u.list, "description");
                devices.push_back({ name, Utf8ToWide(description.empty() ? name : description) });
            }
        }
        api->free_node_contents(&list);
        m_devices = devices;
        m_devicesKnown = true;
    }
    if (mpv != m_mpv)
        api->terminate_destroy(mpv);
    return devices;
}

void VideoPlayer::SetOutputDevices(std::string exclusive, std::string shared)
{
    m_exclusiveDevice = std::move(exclusive);
    m_sharedDevice = std::move(shared);
}

VideoPlayer::BitPerfectState VideoPlayer::CheckBitPerfect() const
{
    if (!m_bitPerfect)
        return BitPerfectState::kOff;
    // The switch applies from the next file: until then the one playing was opened in the other mode.
    if (!m_mpv || !m_audioFile || m_status != Status::kPlaying || !m_appliedBitPerfect)
        return BitPerfectState::kNothing;
    if (m_volume != 100 || m_muted)
        return BitPerfectState::kVolume;
    if (m_speed != 1.0)
        return BitPerfectState::kSpeed;
    // What the decoder hands over against what reaches the output: the same format, rate and channels means libmpv
    // added no conversion or resampling in between.
    mpv_node decoded, sent;
    const Mpv *api = LoadMpv();
    // The "wasapi," fallback may have moved the sound to another output if another app holds the device.
    char *ao = nullptr;
    bool wasapi = api->get_property(m_mpv, "current-ao", MPV_FORMAT_STRING, &ao) >= 0 && ao && strcmp(ao, "wasapi") == 0;
    if (ao)
        api->free(ao);
    if (!wasapi)
        return BitPerfectState::kConverted;
    if (api->get_property(m_mpv, "audio-params", MPV_FORMAT_NODE, &decoded) < 0)
        return BitPerfectState::kNothing;
    BitPerfectState state = BitPerfectState::kConverted;
    if (api->get_property(m_mpv, "audio-out-params", MPV_FORMAT_NODE, &sent) >= 0)
    {
        if (decoded.format == MPV_FORMAT_NODE_MAP && sent.format == MPV_FORMAT_NODE_MAP &&
            NumberAt(decoded.u.list, "samplerate") > 0 &&
            NumberAt(decoded.u.list, "samplerate") == NumberAt(sent.u.list, "samplerate") &&
            StringAt(decoded.u.list, "format") == StringAt(sent.u.list, "format") &&
            StringAt(decoded.u.list, "hr-channels") == StringAt(sent.u.list, "hr-channels"))
            state = BitPerfectState::kYes;
        api->free_node_contents(&sent);
    }
    api->free_node_contents(&decoded);
    return state;
}

void VideoPlayer::SetBestVideo(bool best)
{
    m_bestVideo = best;
    if (m_mpv)
        ApplyVideoQuality();
}

void VideoPlayer::SetBitPerfect(bool on)
{
    m_bitPerfect = on;
}

void VideoPlayer::Reset()
{
    m_status = Status::kNothing;
    m_position = m_duration = 0.0;
    m_paused = m_ended = false;
    m_width = m_height = 0;
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_audio = m_subtitle = 0;
    m_audioFile = m_cover = m_lossless = false;
    m_codec.clear();
    m_tags = Tags();
    m_bitDepth = m_sampleRate = m_formatTries = 0;
    m_floatDepth = false;
}

void VideoPlayer::Stop()
{
    if (m_window)
        ShowWindow(m_window, SW_HIDE);
    if (m_mpv)
    {
        for (int id = 0; id < static_cast<int>(m_overlays.size()); id++)
            HideOverlay(id);
        const char *command[] = { "stop", nullptr };
        LoadMpv()->command_async(m_mpv, 0, command);
    }
    Reset();
}

void VideoPlayer::Shutdown()
{
    if (!m_mpv)
        return;
    const Mpv *api = LoadMpv();
    api->set_wakeup_callback(m_mpv, nullptr, nullptr);
    if (m_window)
        ShowWindow(m_window, SW_HIDE);

    // Waits for it, handling the messages sent to the window meanwhile. One that takes too long is left behind:
    // the process ends with it.
    HANDLE thread = CreateThread(nullptr, 0, EndMpv, new Ending{ api, m_mpv }, 0, nullptr);
    m_mpv = nullptr;
    if (!thread)
        return;
    ULONGLONG deadline = GetTickCount64() + kShutdownWaitMs;
    for (;;)
    {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        DWORD result = MsgWaitForMultipleObjects(1, &thread, FALSE, static_cast<DWORD>(deadline - now), QS_SENDMESSAGE);
        if (result != WAIT_OBJECT_0 + 1)
            break;
        MSG msg;
        PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    CloseHandle(thread);
}

void VideoPlayer::SetBounds(const RECT &bounds)
{
    if (m_window)
        SetWindowPos(m_window, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOACTIVATE);
}

void VideoPlayer::SetCovered(bool covered)
{
    m_covered = covered;
    if (m_status == Status::kPlaying)
        ShowVideoWindow();
}

// Up while a file plays and nothing covers it; for music, only with a cover to show.
void VideoPlayer::ShowVideoWindow()
{
    if (m_window)
        ShowWindow(m_window, m_status == Status::kPlaying && !m_covered && (!m_audioFile || m_cover) ? SW_SHOWNA : SW_HIDE);
}

bool VideoPlayer::OnEvent()
{
    m_wakeup->pending = false;
    if (!m_mpv)
        return false;
    const Mpv *api = LoadMpv();
    bool changed = false;
    for (;;)
    {
        mpv_event *event = api->wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;

        // What libmpv reports of a file before it took the last loadfile (one still opening, when the viewer
        // steps on quickly) is about that file: the next file to start after the answer is the one opened last.
        switch (event->event_id)
        {
        case MPV_EVENT_COMMAND_REPLY:
            if (event->reply_userdata == m_load && m_load != 0 && !m_loadAnswered)
            {
                m_loadAnswered = true;
                if (event->error < 0 && m_status == Status::kLoading)
                {
                    m_status = Status::kFailed;
                    changed = true;
                }
            }
            break;

        case MPV_EVENT_START_FILE:
            if (m_loadAnswered && m_entry == 0)
                m_entry = static_cast<mpv_event_start_file*>(event->data)->playlist_entry_id;
            break;

        case MPV_EVENT_SET_PROPERTY_REPLY:
            if (event->reply_userdata == kAudioDisplay && event->error < 0)
                SetAsync("audio-display", "attachment");
            break;

        case MPV_EVENT_FILE_LOADED:
            if (m_entry != 0 && m_status == Status::kLoading)
            {
                m_fileLoaded = true;
                // In exclusive mode libmpv opens the device after this event, and while another app holds it libmpv
                // retries for a couple of seconds without answering: asking it for anything now would freeze the
                // window. The file counts as open once the sound is up or has given up (the next event below).
                if (!m_appliedBitPerfect)
                {
                    BeginPlaying();
                    changed = true;
                }
            }
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            if (m_fileLoaded && m_status == Status::kLoading)
            {
                BeginPlaying();
                changed = true;
            }
            break;

        case MPV_EVENT_END_FILE:
        {
            const mpv_event_end_file *end = static_cast<mpv_event_end_file*>(event->data);
            if (m_entry != 0 && end->reason == MPV_END_FILE_REASON_ERROR && end->playlist_entry_id == m_entry &&
                m_status == Status::kLoading)
            {
                m_status = Status::kFailed;
                ShowWindow(m_window, SW_HIDE);
                changed = true;
            }
            break;
        }

        case MPV_EVENT_PROPERTY_CHANGE:
        {
            const mpv_event_property *property = static_cast<mpv_event_property*>(event->data);
            const void *data = property->data;
            switch (event->reply_userdata)
            {
            case kDuration:
                m_duration = property->format == MPV_FORMAT_DOUBLE ? *static_cast<const double*>(data) : 0.0;
                break;
            case kPause:
                m_paused = property->format == MPV_FORMAT_FLAG && *static_cast<const int*>(data);
                break;
            case kEofReached:
                m_ended = property->format == MPV_FORMAT_FLAG && *static_cast<const int*>(data);
                break;
            case kVolume:
                if (property->format == MPV_FORMAT_DOUBLE)
                    m_volume = static_cast<int>(*static_cast<const double*>(data) + 0.5);
                break;
            case kMute:
                m_muted = property->format == MPV_FORMAT_FLAG && *static_cast<const int*>(data);
                break;
            case kTrackList:
                // In exclusive mode libmpv may not answer yet: BeginPlaying reads them once it does.
                if (m_status != Status::kLoading || !m_appliedBitPerfect)
                    ReadTracks();
                if (m_status == Status::kPlaying)
                    ShowVideoWindow();
                break;
            case kAid:
                m_audio = property->format == MPV_FORMAT_INT64 ? *static_cast<const int64_t*>(data) : 0;
                break;
            case kSid:
                m_subtitle = property->format == MPV_FORMAT_INT64 ? *static_cast<const int64_t*>(data) : 0;
                break;
            case kWidth:
                m_width = property->format == MPV_FORMAT_INT64 ? static_cast<int>(*static_cast<const int64_t*>(data)) : 0;
                break;
            case kHeight:
                m_height = property->format == MPV_FORMAT_INT64 ? static_cast<int>(*static_cast<const int64_t*>(data)) : 0;
                break;
            }
            changed = true;
            break;
        }

        case MPV_EVENT_SHUTDOWN:
            // libmpv ended on its own: nothing more comes from it.
            m_status = Status::kFailed;
            ShowWindow(m_window, SW_HIDE);
            return true;

        default:
            break;
        }
    }
    return changed;
}

// Subtitles are on: when libmpv picked none (it only picks those the file marks as default or forced, or in the
// language of the system), the first one shows. Read now, as the file is open, rather than from the property
// events, which may still be on their way.
void VideoPlayer::ChooseSubtitle()
{
    ReadTracks();
    int64_t sid = 0;
    if (LoadMpv()->get_property(m_mpv, "sid", MPV_FORMAT_INT64, &sid) < 0)
        sid = 0;  // "no"
    m_subtitle = sid;
    if (m_subtitle == 0 && !m_subtitleTracks.empty())
        SetSubtitle(m_subtitleTracks[0].id);
}

// How many times the format of the sound is asked for before giving up.
static const int kFormatTries = 8;

void VideoPlayer::ReadPosition()
{
    double position = 0.0;
    if (m_mpv && m_status == Status::kPlaying && LoadMpv()->get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &position) >= 0)
        m_position = position;
    if (m_lossless && m_sampleRate == 0 && m_formatTries < kFormatTries)
        ReadAudioFormat();
}

// Whether libmpv's name for a codec is one that keeps every sample.
static bool IsLosslessCodec(const std::string &codec)
{
    if (codec.compare(0, 4, "pcm_") == 0)
        return true;
    for (const char *name : { "flac", "alac", "wavpack", "ape", "tta", "mlp", "truehd" })
    {
        if (codec == name)
            return true;
    }
    return false;
}

// The sample rate of the sound, once libmpv has it. libmpv has nothing before the sound starts, so this is tried on
// a few ticks only.
void VideoPlayer::ReadAudioFormat()
{
    m_formatTries++;
    mpv_node params;
    if (!m_mpv || LoadMpv()->get_property(m_mpv, "audio-params", MPV_FORMAT_NODE, &params) < 0)
        return;
    if (params.format == MPV_FORMAT_NODE_MAP)
    {
        int64_t rate = NumberAt(params.u.list, "samplerate");
        if (rate > 0)
            m_sampleRate = static_cast<int>(rate);
    }
    LoadMpv()->free_node_contents(&params);
}

// The title, artist and album the music file carries, if it does.
void VideoPlayer::ReadMusicTags()
{
    m_tags = Tags();
    mpv_node node;
    if (!m_mpv || LoadMpv()->get_property(m_mpv, "metadata", MPV_FORMAT_NODE, &node) < 0)
        return;
    if (node.format == MPV_FORMAT_NODE_MAP)
    {
        m_tags.title = TagAt(node.u.list, "title");
        m_tags.artist = TagAt(node.u.list, "artist");
        if (m_tags.artist.empty())
            m_tags.artist = TagAt(node.u.list, "album_artist");
        m_tags.album = TagAt(node.u.list, "album");
    }
    LoadMpv()->free_node_contents(&node);
}

// libmpv's screenshot of the video as it is decoded (no scaling, no subtitles): for a cover, the picture itself. It
// comes as 4 bytes a pixel, blue first. A box filter shrinks it to the size asked for, which is all the backdrop needs.
bool VideoPlayer::ReadCover(UINT maxSide, std::vector<BYTE> &pixels, UINT &width, UINT &height, int &sourceWidth, int &sourceHeight)
{
    if (!m_mpv || m_status != Status::kPlaying || !m_cover || maxSide == 0)
        return false;
    NodeMap args;
    args.Add("name", std::string("screenshot-raw"));
    args.Add("flags", std::string("video"));
    mpv_node result = {};
    if (LoadMpv()->command_node(m_mpv, args.Node(), &result) < 0)
        return false;

    bool ok = false;
    if (result.format == MPV_FORMAT_NODE_MAP)
    {
        const mpv_node_list *map = result.u.list;
        int64_t w = NumberAt(map, "w");
        int64_t h = NumberAt(map, "h");
        int64_t stride = NumberAt(map, "stride");
        std::string format = StringAt(map, "format");
        const mpv_node *data = Find(map, "data");
        const mpv_byte_array *bytes = data && data->format == MPV_FORMAT_BYTE_ARRAY ? data->u.ba : nullptr;
        if (w > 0 && h > 0 && w < 65536 && h < 65536 && stride >= w * 4 && bytes && bytes->data &&
            bytes->size >= static_cast<size_t>(stride) * static_cast<size_t>(h) && (format == "bgr0" || format == "bgra"))
        {
            double scale = std::min(1.0, static_cast<double>(maxSide) / static_cast<double>(std::max(w, h)));
            UINT dw = std::max<UINT>(1, static_cast<UINT>(w * scale + 0.5));
            UINT dh = std::max<UINT>(1, static_cast<UINT>(h * scale + 0.5));
            pixels.assign(static_cast<size_t>(dw) * dh * 4, 255);
            const BYTE *source = static_cast<const BYTE*>(bytes->data);
            for (UINT y = 0; y < dh; y++)
            {
                int64_t y0 = static_cast<int64_t>(y) * h / dh;
                int64_t y1 = std::max(y0 + 1, static_cast<int64_t>(y + 1) * h / dh);
                for (UINT x = 0; x < dw; x++)
                {
                    int64_t x0 = static_cast<int64_t>(x) * w / dw;
                    int64_t x1 = std::max(x0 + 1, static_cast<int64_t>(x + 1) * w / dw);
                    unsigned long long sum[3] = {};
                    for (int64_t sy = y0; sy < y1; sy++)
                    {
                        const BYTE *row = source + sy * stride + x0 * 4;
                        for (int64_t sx = x0; sx < x1; sx++, row += 4)
                        {
                            sum[0] += row[0];
                            sum[1] += row[1];
                            sum[2] += row[2];
                        }
                    }
                    unsigned long long count = static_cast<unsigned long long>(y1 - y0) * static_cast<unsigned long long>(x1 - x0);
                    BYTE *out = &pixels[(static_cast<size_t>(y) * dw + x) * 4];
                    out[0] = static_cast<BYTE>(sum[0] / count);
                    out[1] = static_cast<BYTE>(sum[1] / count);
                    out[2] = static_cast<BYTE>(sum[2] / count);
                }
            }
            width = dw;
            height = dh;
            sourceWidth = static_cast<int>(w);
            sourceHeight = static_cast<int>(h);
            ok = true;
        }
    }
    LoadMpv()->free_node_contents(&result);
    return ok;
}

// The audio and subtitle tracks of the file, labelled with what the file says of them: "Signs · eng", or the name
// of a subtitle file next to the video.
void VideoPlayer::ReadTracks()
{
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_cover = m_lossless = false;
    m_codec.clear();
    mpv_node list;
    if (LoadMpv()->get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &list) < 0)
        return;
    if (list.format == MPV_FORMAT_NODE_ARRAY)
    {
        for (int i = 0; i < list.u.list->num; i++)
        {
            const mpv_node &item = list.u.list->values[i];
            if (item.format != MPV_FORMAT_NODE_MAP)
                continue;
            const mpv_node_list *map = item.u.list;
            std::string type = StringAt(map, "type");
            // A music file's only picture is its cover.
            if (type == "video" && m_audioFile)
                m_cover = true;
            if (type == "audio" && m_audioFile && m_codec.empty())
            {
                m_codec = StringAt(map, "codec");
                m_lossless = IsLosslessCodec(m_codec);
                m_formatTries = 0;
            }
            std::vector<Track> *tracks = type == "audio" ? &m_audioTracks : type == "sub" ? &m_subtitleTracks : nullptr;
            if (!tracks)
                continue;

            // A subtitle file next to the video goes by its name: libmpv's title for it is only a part of that.
            std::string title = StringAt(map, "title");
            if (FlagAt(map, "external"))
            {
                title = StringAt(map, "external-filename");
                size_t slash = title.find_last_of("\\/");
                if (slash != std::string::npos)
                    title.erase(0, slash + 1);
            }
            std::wstring label;
            // Audio also says its codec and channels, unless the title already does.
            std::string codec, channels;
            if (type == "audio")
            {
                codec = StringAt(map, "codec");
                for (char &c : codec)
                    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                int64_t count = NumberAt(map, "demux-channel-count");
                channels = count == 1 ? "mono" : count == 2 ? "stereo" : count == 6 ? "5.1" : count == 8 ? "7.1" : count > 0 ? std::to_string(count) + " ch" : "";
            }
            auto upper = [](std::string text) {
                for (char &c : text)
                    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                return text;
            };
            const std::string titleUpper = upper(title);
            const std::string lang = StringAt(map, "lang");
            int partIndex = 0;
            for (const std::string &part : { title, lang, codec, channels })
            {
                if (partIndex++ > 0 && !part.empty() && titleUpper.find(upper(part)) != std::string::npos)
                    continue;
                if (part.empty())
                    continue;
                if (!label.empty())
                    label += L" \u00B7 ";
                label += Utf8ToWide(part);
            }
            if (label.empty())
                label = std::wstring(strings::kTrack) + L" " + std::to_wstring(tracks->size() + 1);
            tracks->push_back({ NumberAt(map, "id"), label });
        }
    }
    LoadMpv()->free_node_contents(&list);
}

void VideoPlayer::TogglePause()
{
    if (!m_mpv || m_status != Status::kPlaying)
        return;
    const Mpv *api = LoadMpv();
    if (m_ended)
    {
        const char *command[] = { "seek", "0", "absolute", nullptr };
        api->command_async(m_mpv, 0, command);
    }
    m_paused = !m_ended && !m_paused;
    int pause = m_paused ? 1 : 0;
    api->set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
}

void VideoPlayer::Seek(double seconds, bool exact)
{
    if (!m_mpv || m_status != Status::kPlaying)
        return;
    if (seconds < 0.0)
        seconds = 0.0;
    std::string at = std::to_string(seconds);
    const char *command[] = { "seek", at.c_str(), exact ? "absolute+exact" : "absolute+keyframes", nullptr };
    LoadMpv()->command_async(m_mpv, 0, command);
    // Shown at once, rather than when libmpv gets there.
    m_position = seconds;
}

void VideoPlayer::SetVolume(int volume)
{
    m_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    if (!m_mpv)
        return;
    double value = m_volume;
    LoadMpv()->set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &value);
    if (m_muted)
        ToggleMute();
}

void VideoPlayer::SetSpeed(double speed)
{
    m_speed = speed < 0.25 ? 0.25 : speed > 8.0 ? 8.0 : speed;
    if (!m_mpv)
        return;
    double value = m_speed;
    LoadMpv()->set_property_async(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE, &value);
}

void VideoPlayer::ToggleMute()
{
    m_muted = !m_muted;
    if (!m_mpv)
        return;
    int mute = m_muted ? 1 : 0;
    LoadMpv()->set_property_async(m_mpv, 0, "mute", MPV_FORMAT_FLAG, &mute);
}

void VideoPlayer::SetAudio(int64_t id)
{
    if (!m_mpv)
        return;
    m_audio = id;
    LoadMpv()->set_property_async(m_mpv, 0, "aid", MPV_FORMAT_INT64, &id);
}

void VideoPlayer::SetSubtitle(int64_t id)
{
    if (!m_mpv)
        return;
    m_subtitle = id;
    if (id == 0)
    {
        const char *off = "no";
        LoadMpv()->set_property_async(m_mpv, 0, "sid", MPV_FORMAT_STRING, &off);
    }
    else
        LoadMpv()->set_property_async(m_mpv, 0, "sid", MPV_FORMAT_INT64, &id);
}

void VideoPlayer::ShowOverlay(int id, int x, int y, const std::vector<BYTE> &pixels, UINT width, UINT height)
{
    if (!m_mpv || id < 0 || id >= static_cast<int>(m_overlays.size()) || pixels.size() < static_cast<size_t>(width) * height * 4)
        return;
    NodeMap args;
    args.Add("name", std::string("overlay-add"));
    args.Add("id", static_cast<int64_t>(id));
    args.Add("x", static_cast<int64_t>(x));
    args.Add("y", static_cast<int64_t>(y));
    args.Add("file", "&" + std::to_string(reinterpret_cast<uintptr_t>(pixels.data())));
    args.Add("offset", static_cast<int64_t>(0));
    args.Add("fmt", std::string("bgra"));
    args.Add("w", static_cast<int64_t>(width));
    args.Add("h", static_cast<int64_t>(height));
    args.Add("stride", static_cast<int64_t>(width) * 4);
    // Waits for libmpv to copy the pixels.
    if (LoadMpv()->command_node(m_mpv, args.Node(), nullptr) >= 0)
        m_overlays[id] = true;
}

void VideoPlayer::HideOverlay(int id)
{
    if (!m_mpv || id < 0 || id >= static_cast<int>(m_overlays.size()) || !m_overlays[id])
        return;
    m_overlays[id] = false;
    std::string at = std::to_string(id);
    const char *command[] = { "overlay-remove", at.c_str(), nullptr };
    LoadMpv()->command_async(m_mpv, 0, command);
}
