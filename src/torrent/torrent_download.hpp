#pragma once

#include <libtorrent/torrent_info.hpp>
#include "../deque/deque.hpp"

class TorrentError : public std::runtime_error {
  public:
    TorrentError(std::string message);
};

lt::torrent_info load_magnet_link_info(const std::string magnet_link);

enum torrent_message_type_t {
    NEW_FILE,
    TERMINATE,
};

class TorrentTaskEvent {
  public:
    virtual torrent_message_type_t message_type() = 0;
};

class TorrentTaskEventTerminate : public TorrentTaskEvent {
  public:
    torrent_message_type_t message_type();
};

class TorrentTaskEventNewFile : public TorrentTaskEvent {
  private:
    std::string file_name;
  public:
    TorrentTaskEventNewFile(std::string name);
    torrent_message_type_t message_type();
    std::string get_name();
};

enum torrent_progress_type_t {
    DOWNLOAD_OK,
    DOWNLOAD_ERROR,
};

struct TorrentProgressEvent {
    torrent_progress_type_t message_type;
    std::string file_name;
    unsigned int file_index;
    std::string error;
};

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

    ThreadSafeDeque<std::shared_ptr<TorrentTaskEvent>> message_queue;
    ThreadSafeDeque<TorrentProgressEvent> progress_queue;
};
