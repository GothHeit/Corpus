#ifndef SINGLE_INSTANCE_HPP
#define SINGLE_INSTANCE_HPP

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// One Corpus window per library. A second start (Explorer's "Open with", say) hands its files to the window that
// is already open and ends, so the files open there.
namespace single_instance
{
    // Sent with WM_COPYDATA: the files, as wide strings each ended by a null.
    constexpr ULONG_PTR kCopyDataFiles = 0x434F5250;

    // Names the instance for a library: the same file always gives the same key.
    std::uint64_t Key(const std::wstring &libraryPath);

    // Claims the key for this process. False if another process holds it. 'owner' keeps the claim until it is closed.
    bool Claim(std::uint64_t key, HANDLE &owner);

    // The window marks itself with the key so the next start can find it.
    void Mark(HWND hWnd, std::uint64_t key);

    // Hands 'files' to the window that holds the key and brings it to the front. False if there is none (yet).
    bool Forward(std::uint64_t key, const std::vector<std::wstring> &files);

    // The files in a WM_COPYDATA of kCopyDataFiles.
    std::vector<std::wstring> ReadFiles(const COPYDATASTRUCT &data);
}

#endif
