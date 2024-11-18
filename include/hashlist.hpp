#pragma once

#include <unordered_map>

#include <libtorrent/torrent_info.hpp>

typedef std::vector<char> hashstr_t;
typedef std::vector<hashstr_t> hashlist_t;
typedef std::unordered_map<std::string, hashlist_t> file_hashlist_t;

class StreamError : public std::runtime_error
{
public:
  StreamError(std::string message);
};

std::ostream& serialize(file_hashlist_t& files, std::ostream& os);

file_hashlist_t deserialize(std::istream& is);

file_hashlist_t load_hashlist(std::string path);

void save_hashlist(std::string path, const file_hashlist_t& files);

file_hashlist_t compare_hashlists(const file_hashlist_t& files, const lt::torrent_info &torrent);
