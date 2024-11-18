#pragma once

#include <libtorrent/torrent_info.hpp>

class TorrentError : public std::runtime_error
{
public:
  TorrentError(std::string message);
};

lt::torrent_info load_magnet_link_info(const std::string magnet_link);

// arguments are torrent session handle and finished file index
// returns true if all files are downloaded
void subscribe_downloaded_file(std::function<bool(lt::torrent_handle &, unsigned int)> event);

void download_torrent_files(const lt::add_torrent_params& params);