#ifndef VIEWER_HPP
#define VIEWER_HPP

#include <windows.h>
#include "renderer.hpp"
#include "image_decoder.hpp"
#include "ui_util.hpp"
#include <string>
#include <vector>

// The in-window viewer, like Quick Look: a picture on a near-black backdrop, first fitted to the window (never
// enlarged). The wheel zooms at the pointer, dragging pans, a double-click switches between fitted and actual size,
// and the arrow keys step through the files it was opened with. The name and the buttons fade out once the pointer
// stood still for two seconds, and so does the pointer.
//
// The view (backdrop, fit, zoom, pan, overlays, stepping) only needs a bitmap and its size, so it doesn't care what
// makes them: pictures come from ImageDecoder, and a video player can feed it the same way.
class Viewer
{
public:
    // What the window has to do after an event.
    enum class Action
    {
        kNone,
        kClose,           // close the viewer
        kOpenExternally,  // open the file shown in its default program
        kFallBack,        // close the viewer and open the file in its default program: Corpus can't show it
    };

    void Start(HWND hWnd) { m_decoder.Start(hWnd); }
    void Stop() { m_decoder.Stop(); }
    void SetBounds(HWND hWnd, Renderer &renderer, const RECT &bounds);

    // Shows paths[index]; the arrows step through 'paths'. Each must be a picture (IsDecodableImage).
    void Open(std::vector<std::string> paths, size_t index);
    void Close();
    bool IsOpen() const { return m_open; }
    bool Contains(POINT pt) const { return m_open && PtInRect(&m_bounds, pt); }
    const std::string &CurrentPath() const { return m_paths[m_index]; }
    // The pointer hides with the overlays.
    bool CursorHidden() const;

    void Paint(Renderer &renderer);

    // 'repeat': the key is held down.
    Action OnKeyDown(WPARAM key, bool repeat);
    Action OnLButtonDown(POINT pt, bool doubleClick);
    // 'pt' is outside the window when the pointer left it.
    void OnMouseMove(POINT pt);
    void OnLButtonUp();
    void OnCaptureChanged();
    void OnMouseWheel(POINT pt, int delta);
    void OnTimer(UINT_PTR timer);
    // The decoder has something: kImageDecoded.
    Action OnDecoded();

private:
    enum Button { kCloseButton = 0, kPreviousButton = 1, kNextButton = 2, kButtonCount = 3 };

    int Scale(int value) const;
    void Invalidate() const;

    void Show(size_t index);
    void Step(int delta);
    void StartFrame();
    void NextFrame();

    // Zoom is in pixels of the screen per pixel of the picture.
    double FitZoom() const;
    double Zoom() const { return m_fit ? FitZoom() : m_zoom; }
    void ZoomAt(POINT pt, double zoom);
    void ZoomToFit();
    void KeepInView();
    D2D1_RECT_F PictureRect() const;
    POINT Center() const;

    float OverlayLevel() const;
    void ShowOverlays();
    void FadeOverlays(float to);

    RECT ButtonRect(int button) const;
    bool ButtonShown(int button) const;
    bool ButtonEnabled(int button) const;
    int ButtonAt(POINT pt) const;
    void PaintButton(Renderer &renderer, int button, float level) const;
    void PaintInfo(Renderer &renderer, float level) const;
    void PaintMessage(Renderer &renderer) const;

    HWND m_hWnd = nullptr;
    Renderer *m_renderer = nullptr;
    RECT m_bounds = {};
    ImageDecoder m_decoder;

    bool m_open = false;
    std::vector<std::string> m_paths;
    size_t m_index = 0;
    // Still on the file it was opened with: if Corpus can't show that one, it opens in its default program instead.
    bool m_first = false;

    // The picture: what the decoder said, the frame it made and its bitmap.
    ImageDecoder::Status m_status = ImageDecoder::Status::kNothing;
    bool m_loadingShown = false;
    bool m_reloading = false;  // decoding the picture again because the graphics device was lost
    DecodedFrame m_frame;      // its pixels are kept for an animation only, past the upload
    bool m_upload = false;     // m_frame waits to become the bitmap
    ComPtr<ID2D1Bitmap> m_bitmap;
    unsigned m_bitmapGeneration = 0;

    // An animation shows m_frame while the decoder makes the next one. When it's time for it but it isn't there
    // yet, it is shown as soon as it arrives.
    bool m_animated = false;
    DecodedFrame m_next;
    bool m_hasNext = false;
    bool m_due = false;

    // Fitted, or zoomed with the picture's point (m_x, m_y) in the middle of the view.
    bool m_fit = true;
    double m_zoom = 1.0;
    double m_x = 0.0;
    double m_y = 0.0;

    bool m_dragging = false;
    POINT m_dragFrom = {};
    double m_dragX = 0.0;
    double m_dragY = 0.0;

    POINT m_pointer = { -32768, -32768 };
    HoverFade m_hover;  // by button
    float m_overlayFrom = 1.0f;
    float m_overlayTo = 1.0f;
    ULONGLONG m_overlayStart = 0;
};

#endif
