#ifndef BOARD_FRAMES_HPP
#define BOARD_FRAMES_HPP

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

class board;

// The frames a video was paused on, on a board: PNG files in a folder of their own ("board-frames") under the
// thumbnail folder. The catalog keeps only the time (board_item::frame); the picture is what the tile shows and what
// the PNG export draws in the video's place. A file is named after the video's database id and the time, so it follows
// the file through a move or rename, and a frame paused on again on another board is the same file.

// The PNG of file 'fileId' paused at 'seconds', under 'thumbnailFolder'.
std::wstring BoardFramePath(const std::wstring &thumbnailFolder, std::int64_t fileId, double seconds);
// Deletes the PNGs no item of 'boards' points to (taken off, board deleted, file removed). Only with the whole
// catalog loaded: never on a library that failed to load, nor in a second window, whose library is empty.
void SweepBoardFrames(const std::wstring &thumbnailFolder, const std::vector<board*> &boards);
// Writes opaque premultiplied BGRA pixels as a PNG, making the folder if needed. False if it couldn't.
bool SaveBoardFrame(const std::wstring &target, const std::vector<BYTE> &pixels, UINT width, UINT height);
// Reads one back as premultiplied BGRA. False if it is missing or unreadable.
bool LoadBoardFrame(const std::wstring &source, std::vector<BYTE> &pixels, UINT &width, UINT &height);

#endif
