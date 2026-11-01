#ifndef AUDIO_SCAN_HPP
#define AUDIO_SCAN_HPP

#include <windows.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
#include "../../include/audio_tags.hpp"

// Posted to the window when songs have been read; the window then takes what is ready.
const UINT kAudioTags = WM_APP + 13;

// Reads the title, artist and album from the songs' own tags (see audio_read_tags) on a thread of its own, a few
// files at a time, so a big import or a slow drive never holds the window. The thread only gets paths, never the
// library; the window stores what comes back.
//
// Only the window's thread calls these.
class AudioScan
{
public:
    struct Result
    {
        std::string path;
        audio_tags tags;
        bool opened = true;  // false: the file could not be opened, so nothing was learned about it
    };

    AudioScan() = default;
    AudioScan(const AudioScan &) = delete;
    AudioScan &operator=(const AudioScan &) = delete;
    ~AudioScan() { Stop(); }

    // Reads these paths and posts kAudioTags to 'hWnd' as results come. Paths asked for while a scan runs are read
    // after it.
    void Start(HWND hWnd, std::vector<std::string> paths);
    // The window is closing: a scan left running finishes its file and posts nothing more.
    void Stop();
    // After kAudioTags: moves what has been read since the last call into 'out'. Returns false if there was nothing.
    bool Take(std::vector<Result> &out);

private:
    struct Shared
    {
        std::mutex mutex;
        HWND hWnd = nullptr;  // null once stopped
        bool done = false;
        bool posted = false;  // a kAudioTags is on its way
        std::vector<Result> results;
    };

    void Launch(std::vector<std::string> paths);
    static void Run(std::shared_ptr<Shared> shared, std::vector<std::string> paths);

    HWND m_hWnd = nullptr;
    std::shared_ptr<Shared> m_running;
    std::vector<std::string> m_queued;
    std::unordered_set<std::string> m_asked;  // paths given to Start whose result hasn't been taken yet
};

#endif
