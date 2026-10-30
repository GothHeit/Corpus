#ifndef COLOR_PROFILE_HPP
#define COLOR_PROFILE_HPP

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <vector>

// Pictures are shown in the monitor's own colors: each is converted from the ICC profile it carries (sRGB when it
// carries none) to the profile Windows has for the monitor the window is on. Thumbnails are cached in sRGB and
// converted when they are read.

// The profile file of the monitor 'hWnd' is mostly on; empty when Windows gives it sRGB's own, or none.
std::wstring MonitorProfile(HWND hWnd);

// The monitor profile pictures are converted to: set by the window's thread, read by any thread. Empty for sRGB.
void SetDisplayProfile(const std::wstring &path);
std::wstring DisplayProfile();

// The profile a decoded picture carries (an ICC profile, or an EXIF color space), or null when it carries none or
// says sRGB.
Microsoft::WRL::ComPtr<IWICColorContext> EmbeddedProfile(IWICImagingFactory *wic, IWICBitmapFrameDecode *frame);
// An ICC profile from its bytes; null when there are none, they can't be read, or they describe sRGB.
Microsoft::WRL::ComPtr<IWICColorContext> ProfileFromBytes(IWICImagingFactory *wic, const std::vector<BYTE> &bytes);

// Whether the pixels are CMYK inks.
bool IsCmyk(IWICBitmapSource *source);

// Puts a color conversion over 'source', to 32bppBGRA: from 'from' (null: sRGB, or for CMYK the stock CMYK
// profile Windows ships) to the profile file 'to' (empty: sRGB). False, with 'source' left as it was, when there is
// nothing to convert or WIC can't.
bool ConvertColors(IWICImagingFactory *wic, Microsoft::WRL::ComPtr<IWICBitmapSource> &source, IWICColorContext *from,
    const std::wstring &to);

// Converts sRGB pixels (BGRA, 4 bytes each, rows top to bottom) to the profile file 'to' in place. Premultiplied
// pixels work too: only the edges of see-through ones come out a little off.
bool ConvertPixels(IWICImagingFactory *wic, UINT width, UINT height, std::vector<BYTE> &pixels, const std::wstring &to);

#endif
