#pragma once

#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#include <libtorrent/torrent_info.hpp>

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

class StreamError : public std::runtime_error {
  public:
    StreamError(std::string message);
};

file_hashlist_t create_hashlist(const lt::torrent_info &torrent, const std::unordered_map<std::string, std::vector<std::string>> &linked_files);

std::ostream& serialize(file_hashlist_t& files, std::ostream& os);

file_hashlist_t deserialize(std::istream& is);

// unicode paths on Windows are not supported
file_hashlist_t load_hashlist(std::filesystem::path path);

// unicode paths on Windows are not supported
void save_hashlist(std::filesystem::path path, const file_hashlist_t& files);

std::unordered_set<std::string> get_updated_files(const file_hashlist_t& files, const lt::torrent_info &torrent);

std::unordered_set<std::string> get_removed_files(const file_hashlist_t& files, const lt::torrent_info &torrent);
