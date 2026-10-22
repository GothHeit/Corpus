#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "video_player.hpp"
#include "strings.hpp"
#include "ui_util.hpp"
#include "../../include/file_identity.hpp"
#include <mpv/client.h>
#include <cstring>
#include <unordered_set>

namespace
{
    const wchar_t *kDllName = L"libmpv-2.dll";
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
        decltype(&mpv_wait_event) wait_event;
        decltype(&mpv_set_wakeup_callback) set_wakeup_callback;
    };

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

    bool DllPresent()
    {
        static const bool present = GetFileAttributesW((ExeFolder() + kDllName).c_str()) != INVALID_FILE_ATTRIBUTES;
        return present;
    }

    // Loads the DLL from the .exe's folder only, never from wherever the search path leads. Null if it isn't
    // there or isn't a libmpv this code knows (client API 2.x). Tried once.
    const Mpv *LoadMpv()
    {
        static Mpv api;
        static bool tried = false;
        static bool loaded = false;
        if (tried)
            return loaded ? &api : nullptr;
        tried = true;

        HMODULE dll = LoadLibraryExW((ExeFolder() + kDllName).c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll)
            return nullptr;
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
        find(api.wait_event, "mpv_wait_event");
        find(api.set_wakeup_callback, "mpv_set_wakeup_callback");
        if (!all || (api.client_api_version() >> 16) != 2)
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
        { "hwdec", "auto-safe" },
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
    return true;
}

bool VideoPlayer::Open(HWND parent, const RECT &bounds, const std::string &path)
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
    m_entry = 0;
    m_loadAnswered = false;
    // The window shows once the file is open: until then the viewer says "Loading" if it takes a while.
    ShowWindow(m_window, SW_HIDE);
    // libmpv keeps the pause from the file before, and pauses at the end of one: each video starts playing.
    LoadMpv()->set_property_string(m_mpv, "pause", "no");
    const char *command[] = { "loadfile", path.c_str(), "replace", nullptr };
    LoadMpv()->command_async(m_mpv, ++m_load, command);
    return true;
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

        case MPV_EVENT_FILE_LOADED:
            if (m_entry != 0 && m_status == Status::kLoading)
            {
                m_status = Status::kPlaying;
                ShowWindow(m_window, SW_SHOWNA);
                ChooseSubtitle();
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
                ReadTracks();
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

void VideoPlayer::ReadPosition()
{
    double position = 0.0;
    if (m_mpv && m_status == Status::kPlaying && LoadMpv()->get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &position) >= 0)
        m_position = position;
}

// The audio and subtitle tracks of the file, labelled with what the file says of them: "Signs · eng", or the name
// of a subtitle file next to the video.
void VideoPlayer::ReadTracks()
{
    m_audioTracks.clear();
    m_subtitleTracks.clear();
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
            for (const std::string &part : { title, StringAt(map, "lang") })
            {
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
    if (!m_mpv)
        return;
    m_volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    double value = m_volume;
    LoadMpv()->set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &value);
    if (m_muted)
        ToggleMute();
}

void VideoPlayer::ToggleMute()
{
    if (!m_mpv)
        return;
    m_muted = !m_muted;
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
