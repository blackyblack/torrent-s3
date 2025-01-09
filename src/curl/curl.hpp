#pragma once

#include <iostream>

#include <libtorrent/torrent_info.hpp>

class DownloadError : public std::runtime_error {
  public:
    DownloadError(std::string message);
};

class ParseError : public std::runtime_error {
  public:
    ParseError(std::string message);
};

lt::torrent_info download_torrent_info(const std::string &url);
