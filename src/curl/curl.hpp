#pragma once

#include <variant>

#include <libtorrent/torrent_info.hpp>

std::variant<lt::torrent_info, std::string> download_torrent_info(const std::string &url);
