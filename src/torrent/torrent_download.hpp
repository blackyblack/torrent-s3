#pragma once

#include <variant>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include "../deque/deque.hpp"

class TorrentError : public std::runtime_error {
  public:
    TorrentError(std::string message);
};

lt::torrent_info load_magnet_link_info(const std::string magnet_link);

struct TorrentTaskEventTerminate {};

struct TorrentTaskEventNewFile {
    std::string file_name;
};

typedef std::variant<TorrentTaskEventTerminate, TorrentTaskEventNewFile> TorrentTaskEvent;

struct TorrentProgressDownloadOk {
    std::string file_name;
    unsigned int file_index;
};

struct TorrentProgressDownloadError {
    std::string error;
};

typedef std::variant<TorrentProgressDownloadOk, TorrentProgressDownloadError> TorrentProgressEvent;

class TorrentDownloader {
  public:
    TorrentDownloader(const lt::add_torrent_params& params);

    void start();
    void stop();

    // progress_queue allows to receive notifications on download progress
    ThreadSafeDeque<TorrentProgressEvent> &get_progress_queue();
    void download_files(const std::vector<std::string> &files);
  private:
    std::thread task;
    lt::add_torrent_params torrent_params;

    ThreadSafeDeque<TorrentTaskEvent> message_queue;
    ThreadSafeDeque<TorrentProgressEvent> progress_queue;
};
