#pragma once

#include <unordered_map>
#include <unordered_set>

#include "../torrent/torrent_download.hpp"

struct hashlist_t {
    // hash of each piece of a file - stored in .torrent file
    std::vector<std::string> hashes;
    // list of files that should be updated if parent file is modified, i.e.
    // files contained in archive parent file
    std::vector<std::string> linked_files;
};

// key - file name
// value - hashes of file pieces and linked file names
typedef std::unordered_map<std::string, hashlist_t> file_hashlist_t;

file_hashlist_t create_hashlist(const lt::torrent_info &torrent, const std::unordered_map<std::string, std::vector<std::string>> &linked_files);

std::unordered_set<std::string> get_updated_files(const lt::torrent_info &torrent, const file_hashlist_t& files);

std::unordered_set<std::string> get_removed_files(const lt::torrent_info &torrent, const file_hashlist_t& files);
