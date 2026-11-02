#ifndef VIDEO_PLAYER_HPP
#define VIDEO_PLAYER_HPP

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct mpv_handle;

// Posted to the window when libmpv has news for the player; the window hands it to the viewer.
const UINT kVideoEvent = WM_APP + 4;

// Whether the viewer plays the file: a video by its extension, with libmpv-2.dll built into the .exe, or next to
// it in a build without one. Only the DLL's presence is checked; it is loaded when the first video opens. Only the
// window's thread calls it.
bool IsPlayableVideo(const std::string &path);
// The libmpv-2.dll the user chose in Settings, kept between runs; empty if none. Corpus uses it instead of the one
// built into the .exe, and falls back to that one if it doesn't load. Read once when the first video is looked at,
// so a new choice applies the next time Corpus starts.
std::wstring LoadCustomLibmpvPath();
void SaveCustomLibmpvPath(const std::wstring &path);
// The same for music: FLAC, MP3, WAV, M4A, OGG, OPUS, AAC and the rest FileKind calls audio.
bool IsPlayableAudio(const std::string &path);

// Plays videos with libmpv in a child window the viewer puts over its area. The viewer's buttons go on top as
// libmpv overlays (ShowOverlay), since nothing the window draws can cover a child window. Music plays the same way;
// its window shows only the embedded cover, if there is one, and the viewer puts it where the cover goes.
//
// libmpv-2.dll is loaded on the first video. A build with the DLL built in writes it to %LOCALAPPDATA%\Corpus once
// per build. The player's window is disabled so the mouse goes through to the main window.
//
// Only the window's thread calls these.
class VideoPlayer
{
public:
    enum class Status
    {
        kNothing,
        kLoading,  // opening the file
        kPlaying,  // the video is up, playing or paused
        kFailed,   // libmpv can't play the file, or can't be loaded
        kMissing,  // the file isn't at its path
    };

    // What a music file says of itself, empty for what it doesn't.
    struct Tags
    {
        std::wstring title;
        std::wstring artist;
        std::wstring album;
    };

    // An audio or subtitle track. Ids are libmpv's.
    struct Track
    {
        int64_t id = 0;
        std::wstring label;  // "Signs · eng", "Movie.srt" for a file next to the video, "Track 2" when nothing says
    };

    VideoPlayer() = default;
    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;
    ~VideoPlayer() { Shutdown(); }

    // Starts playing 'path' over 'bounds' of 'parent', dropping the video before; 'paused' shows its first frame
    // and waits; it starts 'start' seconds in. False if libmpv can't be loaded or started: the status is then kFailed.
    bool Open(HWND parent, const RECT &bounds, const std::string &path, bool paused = false, double start = 0.0);
    // Stops playing and hides the video.
    void Stop();
    // Ends libmpv, as the window closes.
    void Shutdown();
    void SetBounds(const RECT &bounds);
    // Hides the video's window while something the main window draws, such as a sheet, has to show over it.
    void SetCovered(bool covered);

    // Takes in what libmpv reported (kVideoEvent). Returns true if anything the viewer shows changed.
    bool OnEvent();
    // Reads where the video is. libmpv isn't asked to report it, as that would wake the window on every frame
    // even while nothing shows the time.
    void ReadPosition();

    Status GetStatus() const { return m_status; }
    double Position() const { return m_position; }  // in seconds
    double Duration() const { return m_duration; }  // 0 while unknown
    bool Paused() const { return m_paused; }
    bool Ended() const { return m_ended; }  // stopped on the last frame
    int Volume() const { return m_volume; }  // 0 to 100; set before the first file, it is the one libmpv starts with
    // True once, after a file opened in exclusive mode got no sound because the device is held by another app.
    // The file open now got no sound for that reason: the song stays, the queue doesn't move on.
    bool OutputBusyFile() const { return m_outputBusyFile; }
    bool TakeOutputBusy() { bool busy = m_outputBusy; m_outputBusy = false; return busy; }
    bool IsAudio() const { return m_audioFile; }  // the file opened last is music
    bool HasCover() const { return m_cover; }     // music with a picture in it, once open
    // What a lossless music file says of its sound. The bit depth is the one its header declares, 0 when the format
    // has none or the header can't be read (never a guess); the sample rate is 0 until libmpv has it.
    int BitDepth() const { return m_bitDepth; }
    bool FloatDepth() const { return m_floatDepth; }  // the samples are floats: "32-bit float"
    int SampleRate() const { return m_sampleRate; }
    double Speed() const { return m_speed; }  // 1.0 is normal; kept from one video to the next
    bool Muted() const { return m_muted; }
    const Tags &MusicTags() const { return m_tags; }  // read once the music is open
    // The cover of the music playing, taken from libmpv as it shows it and reduced to fit 'maxSide' pixels: opaque
    // premultiplied BGRA, 4 bytes each. 'sourceWidth' and 'sourceHeight' are the picture's own size. False while
    // libmpv has no picture to give yet.
    bool ReadCover(UINT maxSide, std::vector<BYTE> &pixels, UINT &width, UINT &height, int &sourceWidth, int &sourceHeight);
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    const std::vector<Track> &AudioTracks() const { return m_audioTracks; }
    const std::vector<Track> &SubtitleTracks() const { return m_subtitleTracks; }
    int64_t Audio() const { return m_audio; }        // the track playing, 0 for none
    int64_t Subtitle() const { return m_subtitle; }  // the track shown, 0 for none

    // An ended video starts again.
    void TogglePause();
    // 'exact' goes to that very frame; otherwise to the keyframe nearest, which is quick enough to follow a drag.
    void Seek(double seconds, bool exact);
    void SetVolume(int volume);
    // Plays faster or slower; libmpv keeps the pitch of the sound.
    void SetSpeed(double speed);
    void ToggleMute();
    // Best picture (libmpv's high-quality scalers, dithering and HDR tone mapping, heavier on the GPU) or fast.
    // Set before the first file, it is the one libmpv starts with; after, it applies at once.
    void SetBestVideo(bool best);
    // Turns the picture a quarter turn (+1 right, -1 left) or mirrors it as seen on screen. Only the picture: the
    // sound is untouched. Each file starts upright again.
    void Rotate(int quarterTurns);
    void Flip(bool horizontal);
    // Music and sound go straight to the device in exclusive WASAPI at the file's own sample rate, for the next
    // file opened: nothing else can play meanwhile.
    void SetBitPerfect(bool on);
    // The outputs libmpv finds (WASAPI only, the system default left out), read again on every call. 'name' is
    // libmpv's own, the one to keep.
    struct AudioDevice
    {
        std::string name;
        std::wstring description;
    };
    std::vector<AudioDevice> AudioDevices();
    // The output used while bit-perfect is on ('exclusive') and while it is off ('shared'): libmpv's name for it, or
    // empty for the system default. One that is gone falls back to the default. Applies to the next file opened.
    void SetOutputDevices(std::string exclusive, std::string shared);
    // Whether the sound now playing reaches an exclusive device with its samples untouched, or what stops it.
    enum class BitPerfectState { kOff, kNothing, kYes, kVolume, kSpeed, kLossy, kConverted };
    BitPerfectState CheckBitPerfect() const;
    void SetAudio(int64_t id);
    // 0 hides the subtitles.
    void SetSubtitle(int64_t id);

    // Puts premultiplied BGRA pixels over the video with their top left corner at (x, y) of the video's window,
    // replacing the overlay with the same id (0 to 63) if there is one. libmpv copies them before returning.
    void ShowOverlay(int id, int x, int y, const std::vector<BYTE> &pixels, UINT width, UINT height);
    void HideOverlay(int id);

private:
    struct Wakeup
    {
        HWND hWnd = nullptr;
        std::atomic<bool> pending{ false };  // a kVideoEvent is on its way
    };

    static void OnWakeup(void *data);
    bool Start(HWND parent);
    void ReadTracks();
    void ReadAudioFormat();
    void ReadMusicTags();
    void ChooseSubtitle();
    void ApplyVideoQuality();
    void ApplyAudioOutput();
    void ApplyOrientation();
    void BeginPlaying();
    void SetAsync(const char *name, const char *value, uint64_t reply = 0);
    void ShowVideoWindow();
    void Reset();

    mpv_handle *m_mpv = nullptr;
    HWND m_window = nullptr;  // the video's window, a child of the main one
    bool m_covered = false;   // SetCovered: the window stays hidden
    std::shared_ptr<Wakeup> m_wakeup = std::make_shared<Wakeup>();
    std::vector<bool> m_overlays = std::vector<bool>(64);  // by id, those libmpv shows

    Status m_status = Status::kNothing;
    uint64_t m_load = 0;          // counts the loadfiles, as the id of libmpv's answer to each
    bool m_loadAnswered = false;  // libmpv took the last one: what it reports next is about that file
    int64_t m_entry = 0;          // libmpv's id for the file opened last, once it started opening it
    double m_position = 0.0;
    double m_duration = 0.0;
    bool m_paused = false;
    bool m_ended = false;
    int m_volume = 100;
    double m_speed = 1.0;
    bool m_muted = false;
    bool m_bestVideo = true;
    int m_turns = 0;          // quarter turns clockwise
    bool m_flipH = false;     // mirrors as seen on screen, after the turn
    bool m_flipV = false;
    bool m_bitPerfect = false;
    std::vector<AudioDevice> m_devices;  // the outputs libmpv listed last; opening a file must not ask again
    bool m_devicesKnown = false;
    bool m_fileLoaded = false;  // libmpv has the file open; Playing waits for the sound in exclusive mode
    bool m_outputBusy = false;
    bool m_outputBusyFile = false;
    bool m_appliedBitPerfect = false;  // the mode the file playing was opened with
    std::string m_exclusiveDevice;
    std::string m_sharedDevice;
    int m_width = 0;
    int m_height = 0;
    std::vector<Track> m_audioTracks;
    std::vector<Track> m_subtitleTracks;
    int64_t m_audio = 0;
    int64_t m_subtitle = 0;
    bool m_audioFile = false;
    bool m_cover = false;
    bool m_lossless = false;  // the music's codec keeps every sample
    std::string m_codec;      // libmpv's name for it, "flac"
    int m_bitDepth = 0;
    bool m_floatDepth = false;
    int m_sampleRate = 0;
    int m_formatTries = 0;  // times ReadAudioFormat asked for them
    Tags m_tags;
};

#endif
