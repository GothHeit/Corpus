#ifndef MEDIA_CONTROLS_HPP
#define MEDIA_CONTROLS_HPP

#include <windows.h>
#include <string>
#include <vector>

struct IInspectable;
struct ISystemMediaTransportControls;
namespace smtc_abi { struct IDisplayUpdater; }

// Corpus in Windows' media controls (System Media Transport Controls): the title in the volume flyout and on the lock
// screen, and the keyboard's and headset's media keys while another program has the focus. A key pressed arrives as
// 'message' posted to the window, with the button in wParam (the Button enum below). While Corpus has the focus the
// media keys come as WM_APPCOMMAND instead, which the window handles itself.
//
// It starts the first time something plays, and does nothing where Windows lacks it.
class MediaControls
{
public:
    // The buttons, as the values of Windows.Media.SystemMediaTransportControlsButton.
    enum Button { kPlay = 0, kPause = 1, kStop = 2, kNext = 6, kPrevious = 7 };

    struct State
    {
        bool active = false;   // something plays or is paused; false: the controls go away
        bool playing = false;
        bool music = false;    // music, else a video
        std::wstring title;
        std::wstring artist;
        // The cover, as the viewer holds it (opaque premultiplied BGRA); 'coverSerial' tells covers apart, 0 for none.
        // Only read while Update runs.
        unsigned coverSerial = 0;
        const std::vector<BYTE> *coverPixels = nullptr;
        UINT coverWidth = 0;
        UINT coverHeight = 0;
        bool operator==(const State &o) const
        {
            return active == o.active && playing == o.playing && music == o.music && title == o.title && artist == o.artist &&
                coverSerial == o.coverSerial;
        }
    };

    ~MediaControls() { Stop(); }
    void Start(HWND hWnd, UINT message) { m_hWnd = hWnd; m_message = message; }
    void Update(const State &state);
    void Stop();

private:
    bool Connect();
    void Publish(const State &state);
    void SetThumbnail(smtc_abi::IDisplayUpdater *updater, const State &state);

    HWND m_hWnd = nullptr;
    UINT m_message = 0;
    ISystemMediaTransportControls *m_controls = nullptr;
    void *m_handler = nullptr;
    long long m_token = 0;
    bool m_tried = false;
    State m_last;
};

#endif
