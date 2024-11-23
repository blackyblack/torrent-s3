#include <fstream>
#include <cstdio>
#include <cstdlib>

#include "./hashlist.hpp"

StreamError::StreamError(std::string message) : std::runtime_error(message.c_str()) {}

std::ostream& serialize(const file_hashlist_t& files, std::ostream& os) {
  auto files_size = files.size();
  os.write(reinterpret_cast<char *>(&files_size), sizeof(uint32_t));
  for (const auto &h : files) {
    auto hashes_size = h.second.size();
    os.write(reinterpret_cast<char *>(&hashes_size), sizeof(uint32_t));
    for (const auto &h : h.second) {
      auto hash_size = h.size();
      os.write(reinterpret_cast<char *>(&hash_size), sizeof(uint32_t));
      os.write(h.data(), hash_size);
    }
    auto file_name_size = h.first.size();
    os.write(reinterpret_cast<char *>(&file_name_size), sizeof(uint32_t));
    os.write(h.first.c_str(), file_name_size);
  }
  return os;
}

file_hashlist_t deserialize(std::istream& is) {
  uint32_t files_size;
  is.read(reinterpret_cast<char *>(&files_size), sizeof(uint32_t));
  if (!is) {
    throw StreamError("eof");
  }
  file_hashlist_t files;
  for (auto i = 0; i < files_size; i++) {
    uint32_t hashes_size;
    is.read(reinterpret_cast<char *>(&hashes_size), sizeof(uint32_t));
    if (!is) {
      throw StreamError("eof");
    }
    std::vector<std::vector<char>> hashes;
    for (auto i = 0; i < hashes_size; i++) {
      uint32_t hash_size;
      is.read(reinterpret_cast<char *>(&hash_size), sizeof(uint32_t));
      if (!is) {
        throw StreamError("eof");
      }
      char* hash_bytes = new char[hash_size]();
      for (auto i = 0; i < hash_size; i++) {
        is.read(hash_bytes + i, 1);
        if (!is) {
          throw StreamError("eof");
        }
      }
      std::vector<char> hash(hash_bytes, hash_bytes + hash_size);
      hashes.push_back(hash);
    }
    uint32_t name_size;
    is.read(reinterpret_cast<char *>(&name_size), sizeof(uint32_t));
    if (!is) {
      throw StreamError("eof");
    }
    char* name_bytes = new char[name_size + 1]();
    for (auto i = 0; i < name_size; i++) {
      is.read(name_bytes + i, 1);
      if (!is) {
        throw StreamError("eof");
      }
    }
    std::string name(name_bytes);
    files[name] = hashes;
  }
  return files;
}

file_hashlist_t load_hashlist(std::string path) {
  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  return deserialize(ifs);
}

void save_hashlist(std::string path, const file_hashlist_t& files) {
  std::ofstream ofs(path, std::ios::out | std::ios::binary);
  serialize(files, ofs);
}

static std::tuple<lt::piece_index_t, lt::piece_index_t> file_piece_range(lt::file_storage const& fs, lt::file_index_t const file)
{
  auto const range = fs.map_file(file, 0, 1);
  std::int64_t const file_size = fs.file_size(file);
  std::int64_t const piece_size = fs.piece_length();
  auto const end_piece = lt::piece_index_t(int((static_cast<int>(range.piece)
    * piece_size + range.start + file_size - 1) / piece_size + 1));
  return std::make_tuple(range.piece, end_piece);
}

file_hashlist_t compare_hashlists(const file_hashlist_t& files, const lt::torrent_info &torrent) {
  file_hashlist_t new_hashlist;

  for (const auto &file_index: torrent.files().file_range()) {
    const auto file_size = torrent.files().file_size(file_index);
    const auto file_name = torrent.files().file_path(file_index);
    hashlist_t loaded_file_hashes;
    if (files.count(file_name) > 0) {
      loaded_file_hashes = files.at(file_name);
    }
    hashlist_t torrent_file_hashes;
    const auto range = file_piece_range(torrent.files(), file_index);
    for (lt::piece_index_t pi = std::get<0>(range); pi != std::get<1>(range); pi++) {
      const auto piece_hash = torrent.hash_for_piece(pi);
      const auto hash_vector = std::vector<char>(piece_hash.data(), piece_hash.data() + piece_hash.size());
      torrent_file_hashes.push_back(hash_vector);
    }

    new_hashlist[file_name] = torrent_file_hashes;
  }
  return new_hashlist;
}
