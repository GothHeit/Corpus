#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "media_controls.hpp"
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <systemmediatransportcontrolsinterop.h>
#include <atomic>
#include <cstring>
#include <shlwapi.h>
#include <shcore.h>

// Windows.Media is a Windows Runtime API and MinGW has no header for it besides the interop one, so the interfaces
// used are declared by hand, as in the Windows SDK's windows.media.h: same IIDs, same methods in the same order.
// Methods that are never called only hold their place in the table.
namespace smtc_abi
{
    // Windows.Storage.Streams.IRandomAccessStreamReferenceStatics, as far as the one method used.
    struct IReferenceStatics : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE CreateFromFile(IInspectable *file, IInspectable **reference) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateFromUri(IInspectable *uri, IInspectable **reference) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateFromStream(IUnknown *stream, IInspectable **reference) = 0;
    };

    enum PlaybackStatus { kClosed = 0, kChanging = 1, kStopped = 2, kPlaying = 3, kPaused = 4 };
    enum PlaybackType { kMusic = 1, kVideo = 2 };

    struct Token { long long value; };

    struct IButtonPressedArgs : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE get_Button(int *value) = 0;
    };

    // TypedEventHandler<SystemMediaTransportControls, SystemMediaTransportControlsButtonPressedEventArgs>.
    struct IButtonHandler : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable *sender, IButtonPressedArgs *args) = 0;
    };

    struct IMusicProperties : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_AlbumArtist(HSTRING value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Artist(HSTRING value) = 0;
    };

    struct IVideoProperties : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING value) = 0;
    };

    struct IDisplayUpdater : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE get_Type(int *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Type(int value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IInspectable **value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(IInspectable *value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(IMusicProperties **value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(IVideoProperties **value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(IInspectable **value) = 0;
        virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(int type, IInspectable *file, IInspectable **operation) = 0;
        virtual HRESULT STDMETHODCALLTYPE ClearAll() = 0;
        virtual HRESULT STDMETHODCALLTYPE Update() = 0;
    };
}

// Windows.Media.ISystemMediaTransportControls, outside the namespace to match the forward declaration in the header.
struct ISystemMediaTransportControls : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(int value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(smtc_abi::IDisplayUpdater **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(int *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelUpEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelDownEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(smtc_abi::IButtonHandler *handler, smtc_abi::Token *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(smtc_abi::Token token) = 0;
    // add_PropertyChanged and remove_PropertyChanged follow; never called.
};

namespace
{
    // {DDB0472D-C911-4A1F-86D9-DC3D71A95F5A}: the header declares it but no library defines it.
    const GUID kIidInterop = { 0xDDB0472D, 0xC911, 0x4A1F, { 0x86, 0xD9, 0xDC, 0x3D, 0x71, 0xA9, 0x5F, 0x5A } };
    // {99FA3FF4-1742-42A6-902E-087D41F965EC}
    const GUID kIidControls = { 0x99FA3FF4, 0x1742, 0x42A6, { 0x90, 0x2E, 0x08, 0x7D, 0x41, 0xF9, 0x65, 0xEC } };
    // {857309DC-3FBF-4E7D-986F-EF3B1A07A964}: IRandomAccessStreamReferenceStatics.
    const GUID kIidReferenceStatics = { 0x857309DC, 0x3FBF, 0x4E7D, { 0x98, 0x6F, 0xEF, 0x3B, 0x1A, 0x07, 0xA9, 0x64 } };
    // {905A0FE1-BC53-11DF-8C49-001E4FC686DA}: IRandomAccessStream.
    const GUID kIidRandomAccessStream = { 0x905A0FE1, 0xBC53, 0x11DF, { 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA } };
    // IAgileObject {94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90}: tells Windows the handler may be called from any thread.
    // TypedEventHandler<SystemMediaTransportControls, SystemMediaTransportControlsButtonPressedEventArgs>, a generic
    // delegate whose IID Windows derives from the type names (a v5 UUID of their signature). add_ButtonPressed asks
    // the handler for exactly this and fails with E_NOINTERFACE when it is not answered.
    const GUID kIidButtonHandler = { 0x0557E996, 0x7B23, 0x5BAE, { 0xAA, 0x81, 0xEA, 0x0D, 0x67, 0x11, 0x43, 0xA4 } };
    const GUID kIidAgile = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };

    // Called on a thread of Windows' own: it only posts the button to the window, which does the rest.
    class ButtonHandler : public smtc_abi::IButtonHandler
    {
    public:
        ButtonHandler(HWND hWnd, UINT message) : m_hWnd(hWnd), m_message(message) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
        {
            if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, kIidAgile) || IsEqualIID(iid, kIidButtonHandler))
            {
                *object = static_cast<smtc_abi::IButtonHandler *>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG left = --m_refs;
            if (left == 0)
                delete this;
            return left;
        }
        HRESULT STDMETHODCALLTYPE Invoke(IInspectable *, smtc_abi::IButtonPressedArgs *args) override
        {
            int button = -1;
            if (args && SUCCEEDED(args->get_Button(&button)))
                PostMessage(m_hWnd, m_message, static_cast<WPARAM>(button), 0);
            return S_OK;
        }

    private:
        HWND m_hWnd;
        UINT m_message;
        std::atomic<ULONG> m_refs{ 1 };
    };

    // An HSTRING made from text, released with it.
    class Text
    {
    public:
        explicit Text(const std::wstring &text)
        {
            if (FAILED(WindowsCreateString(text.c_str(), static_cast<UINT32>(text.size()), &m_string)))
                m_string = nullptr;
        }
        ~Text() { WindowsDeleteString(m_string); }
        Text(const Text &) = delete;
        Text &operator=(const Text &) = delete;
        operator HSTRING() const { return m_string; }

    private:
        HSTRING m_string = nullptr;
    };
}

// Asks Windows for the controls of this window, once. Anything missing (an old Windows) leaves them off.
bool MediaControls::Connect()
{
    if (m_controls)
        return true;
    if (m_tried || !m_hWnd)
        return false;
    m_tried = true;

    // The main thread has COM already; this only says Windows Runtime may be used on it too.
    RoInitialize(RO_INIT_SINGLETHREADED);
    Text className(L"Windows.Media.SystemMediaTransportControls");
    ISystemMediaTransportControlsInterop *interop = nullptr;
    if (FAILED(RoGetActivationFactory(className, kIidInterop,
            reinterpret_cast<void **>(&interop))) || !interop)
        return false;
    ISystemMediaTransportControls *controls = nullptr;
    HRESULT hr = interop->GetForWindow(m_hWnd, kIidControls, reinterpret_cast<void **>(&controls));
    interop->Release();
    if (FAILED(hr) || !controls)
        return false;

    ButtonHandler *handler = new ButtonHandler(m_hWnd, m_message);
    smtc_abi::Token token = {};
    if (FAILED(controls->add_ButtonPressed(handler, &token)))
    {
        handler->Release();
        controls->Release();
        return false;
    }
    m_controls = controls;
    m_handler = handler;
    m_token = token.value;
    return true;
}

void MediaControls::Update(const State &state)
{
    if (state == m_last)
        return;
    if (!state.active && !m_controls)
    {
        m_last = state;
        return;
    }
    if (!Connect())
        return;
    Publish(state);
    m_last = state;
}

void MediaControls::Publish(const State &state)
{
    ISystemMediaTransportControls *c = m_controls;
    if (!state.active)
    {
        c->put_PlaybackStatus(smtc_abi::kClosed);
        c->put_IsEnabled(FALSE);
        return;
    }
    c->put_IsEnabled(TRUE);
    c->put_IsPlayEnabled(TRUE);
    c->put_IsPauseEnabled(TRUE);
    c->put_IsStopEnabled(TRUE);
    c->put_IsNextEnabled(TRUE);
    c->put_IsPreviousEnabled(TRUE);
    c->put_PlaybackStatus(state.playing ? smtc_abi::kPlaying : smtc_abi::kPaused);

    // The play state alone needs no new title.
    bool sameFile = m_last.active && m_last.music == state.music && m_last.title == state.title &&
        m_last.artist == state.artist &&
        m_last.coverSerial == state.coverSerial;
    if (sameFile)
        return;
    smtc_abi::IDisplayUpdater *updater = nullptr;
    if (FAILED(c->get_DisplayUpdater(&updater)) || !updater)
        return;
    updater->ClearAll();
    updater->put_Type(state.music ? smtc_abi::kMusic : smtc_abi::kVideo);
    if (state.music)
    {
        smtc_abi::IMusicProperties *music = nullptr;
        if (SUCCEEDED(updater->get_MusicProperties(&music)) && music)
        {
            Text title(state.title), artist(state.artist);
            music->put_Title(title);
            // Both the album artist and the artist take it, whichever the shell shows.
            music->put_AlbumArtist(artist);
            music->put_Artist(artist);
            music->Release();
        }
    }
    else
    {
        smtc_abi::IVideoProperties *video = nullptr;
        if (SUCCEEDED(updater->get_VideoProperties(&video)) && video)
        {
            Text title(state.title);
            video->put_Title(title);
            video->Release();
        }
    }
    SetThumbnail(updater, state);
    updater->Update();
    updater->Release();
}

// The cover as the card's picture: a 24-bit bitmap in memory, handed to Windows as a stream reference.
void MediaControls::SetThumbnail(smtc_abi::IDisplayUpdater *updater, const State &state)
{
    if (!state.music || !state.coverPixels || state.coverWidth == 0 || state.coverHeight == 0 ||
        state.coverPixels->size() < static_cast<size_t>(state.coverWidth) * state.coverHeight * 4)
        return;
    const UINT w = state.coverWidth, h = state.coverHeight;
    const UINT rowBytes = (w * 3 + 3) & ~3u;
    const UINT dataBytes = rowBytes * h;
    const UINT headerBytes = 14 + 40;
    std::vector<BYTE> bmp(headerBytes + dataBytes, 0);
    auto put32 = [&](size_t at, UINT v) { for (int i = 0; i < 4; i++) bmp[at + i] = static_cast<BYTE>(v >> (8 * i)); };
    bmp[0] = 'B';
    bmp[1] = 'M';
    put32(2, headerBytes + dataBytes);
    put32(10, headerBytes);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    bmp[26] = 1;
    bmp[28] = 24;
    put32(34, dataBytes);
    const BYTE *source = state.coverPixels->data();
    for (UINT y = 0; y < h; y++)
    {
        BYTE *row = &bmp[headerBytes + static_cast<size_t>(h - 1 - y) * rowBytes];   // bottom-up
        for (UINT x = 0; x < w; x++)
            memcpy(row + x * 3, source + (static_cast<size_t>(y) * w + x) * 4, 3);
    }

    IStream *stream = SHCreateMemStream(bmp.data(), static_cast<UINT>(bmp.size()));
    if (!stream)
        return;
    IUnknown *random = nullptr;
    HRESULT hr = CreateRandomAccessStreamOverStream(stream, BSOS_DEFAULT, kIidRandomAccessStream,
        reinterpret_cast<void **>(&random));
    stream->Release();
    if (FAILED(hr) || !random)
        return;
    Text className(L"Windows.Storage.Streams.RandomAccessStreamReference");
    smtc_abi::IReferenceStatics *statics = nullptr;
    if (SUCCEEDED(RoGetActivationFactory(className, kIidReferenceStatics, reinterpret_cast<void **>(&statics))) && statics)
    {
        IInspectable *reference = nullptr;
        if (SUCCEEDED(statics->CreateFromStream(random, &reference)) && reference)
        {
            updater->put_Thumbnail(reference);
            reference->Release();
        }
        statics->Release();
    }
    random->Release();
}

void MediaControls::Stop()
{
    if (m_controls)
    {
        smtc_abi::Token token = { m_token };
        m_controls->remove_ButtonPressed(token);
        m_controls->put_IsEnabled(FALSE);
        m_controls->Release();
        m_controls = nullptr;
    }
    if (m_handler)
    {
        static_cast<smtc_abi::IButtonHandler *>(m_handler)->Release();
        m_handler = nullptr;
    }
}
