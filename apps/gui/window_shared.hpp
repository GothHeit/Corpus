#ifndef WINDOW_SHARED_HPP
#define WINDOW_SHARED_HPP

// What the files of Window (window.cpp, window_*.cpp) share besides the class itself.

#include <windows.h>
#include <cstddef>
#include <string>
#include <vector>

class library;
class file;

// Where the pointer is said to be once it has left the window: outside every component.
const POINT kNoPointer = { -32768, -32768 };

// Posted by the import thread when it is done.
const UINT kImportDone = WM_APP + 1;

// Posted to itself once a drag of files out of the window ended, behind any drop it made on the window.
const UINT kDragOutDone = WM_APP + 10;

// The thumbnail size and the grid's order, kept for the next run (best effort).
void SaveTileSize(int size);

void SaveSort(int key, bool descending);

// "1 file", "12 files".
std::wstring FileCount(size_t count);

// The last part of a path: "Pictures" for C:\Users\me\Pictures.
std::wstring FolderName(const std::string &path);

// Asks for folders with the system's folder picker: several, or just one. Empty if none was picked.
std::vector<std::string> PickFolders(HWND hWnd, bool several);

// How many of 'files' lack at least one of the tags named: the files that adding them changes.
size_t FilesLacking(const library &lib, const std::vector<file*> &files, const std::vector<std::string> &names);

// The paths of 'files', in their order.
std::vector<std::string> PathsOf(const std::vector<file*> &files);

// Opens the file with the program Windows associates with it (an .html in the browser, a .pdf in the PDF reader,
// ...). Windows itself reports missing files and asks which program to use when there is none.
void OpenWithDefaultProgram(HWND hWnd, const std::string &path);

#endif
