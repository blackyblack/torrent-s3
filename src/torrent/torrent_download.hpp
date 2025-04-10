#pragma once

#include <thread>
#include <variant>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include "../deque/deque.hpp"

std::variant<lt::torrent_info, std::string> load_magnet_link_info(const std::string magnet_link);

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
    lt::torrent_info get_torrent_info() const;
private:
    std::thread task;
    lt::add_torrent_params torrent_params;

    ThreadSafeDeque<TorrentTaskEvent> message_queue;
    ThreadSafeDeque<TorrentProgressEvent> progress_queue;
};

std::vector<std::string> get_file_hashes(const lt::torrent_info &torrent, std::string file_name);
