#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

#include <libtorrent/torrent_info.hpp>

class DownloadingFiles {
  public:
    DownloadingFiles(const lt::torrent_info& torrent_, std::vector<std::string> updated_files, unsigned long long size_limit_bytes);
    std::vector<std::string> download_next_chunk();
    // mark as downloaded
    void complete_file(std::string file_name);
    bool is_completed() const;

  private:
    const lt::torrent_info torrent;
    unsigned long long size_limit;
    // we might want to download not all files from the torrent, so we keep a separate set of downloadable files
    std::unordered_set<std::string> torrent_files;
    std::unordered_set<std::string> completed_files;
    std::unordered_set<std::string> downloading_files;
};
