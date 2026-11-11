#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "window.hpp"
#include "window_shared.hpp"
#include "file_kind.hpp"
#include "video_player.hpp"
#include "../../include/database.hpp"
#include "../../include/library.hpp"
#include "../../include/file.hpp"
#include <string>
#include <vector>

// Hands the audio files whose tags were never read to the background scan: at start (the catalog may predate the
// feature, or files came in while an older build ran, or an older build stored them without a genre), after an import or a watched folder brought songs, and after
// the library was reloaded. Each song is read once; one with no tags is stored as empty so it isn't read again.
void Window::ScanAudioTags()
{
    std::vector<std::string> paths;
    for (const file *f : m_lib.get_files())
    {
        if ((!f->get_audio() || f->get_audio()->outdated) && KindOf(f->get_path()) == FileKind::kAudio)
            paths.push_back(f->get_path());
    }
    m_audioScan.Start(m_hWnd, std::move(paths));
}

// A batch of songs was read: their tags go into the catalog in one transaction, and the grid shows the titles.
void Window::OnAudioTags()
{
    std::vector<AudioScan::Result> results;
    if (!m_audioScan.Take(results))
        return;

    database *db = m_lib.attached();
    if (db && !db->begin())
    {
        ReportSaveError();
        return;
    }
    for (const AudioScan::Result &r : results)
    {
        // The file may have left the library since it was queued.
        // A file that could not be opened (drive unplugged) is not stored as "no tags"; the next scan tries again.
        if (!r.opened)
            continue;
        if (file *f = m_lib.find_file(r.path))
            m_lib.set_audio_tags(f, r.tags);
    }
    if (db && (!db->last_error().empty() || !db->commit()))
        db->rollback();
    ReportSaveError();
    if (!IsWindow(m_hWnd))
        return;
    m_fileGrid.AudioTagsChanged();
    if (m_infoPane.IsOpen())
        m_infoPane.SetSelection(m_fileGrid.SelectedFiles());
    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void Window::PlayExternal(const std::vector<std::wstring> &files, single_instance::Play how)
{
    std::vector<std::string> paths;
    for (const std::wstring &file : files)
    {
        std::string path = WideToUtf8(file);
        if (IsPlayableAudio(path) && GetFileAttributesW(FileWide(file).c_str()) != INVALID_FILE_ATTRIBUTES)
            paths.push_back(std::move(path));
    }
    if (paths.empty() || m_secondary)
        return;
    bool queue = how == single_instance::Play::kQueue || how == single_instance::Play::kNext;
    if (queue && m_viewer.HasSong())
    {
        m_viewer.QueueSongs(paths, how == single_instance::Play::kNext);
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return;
    }
    HandleViewerAction(m_viewer.PlayInBackground(std::move(paths), how == single_instance::Play::kShuffle));
}
