#pragma once

#include <libtorrent/torrent_info.hpp>

#include "../s3/s3.hpp"
#include "../deque/deque.hpp"

class TorrentError : public std::runtime_error
{
public:
  TorrentError(std::string message);
};

enum file_status_t {
  WAITING,
  DOWNLOADING,
  COMPLETED,
  S3_ERROR,
};

struct file_info_t {
  unsigned long long size;
  file_status_t status;
};

struct file_error_info_t {
  unsigned int file_index;
  std::string error;
};

lt::torrent_info load_magnet_link_info(const std::string magnet_link);

// returns vector of files that couldn't upload to S3
std::vector<file_error_info_t> download_torrent_files(const lt::add_torrent_params& params, std::vector<file_info_t> &files, S3Uploader &uploader, unsigned long long limit_size_bytes);
