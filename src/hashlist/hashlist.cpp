#include <fstream>
#include <cstdio>
#include <cstdlib>

#include "./hashlist.hpp"

StreamError::StreamError(std::string message) : std::runtime_error(message.c_str()) {}

static std::tuple<lt::piece_index_t, lt::piece_index_t> file_piece_range(lt::file_storage const& fs, lt::file_index_t const file) {
    auto const range = fs.map_file(file, 0, 1);
    std::int64_t const file_size = fs.file_size(file);
    std::int64_t const piece_size = fs.piece_length();
    auto const end_piece = lt::piece_index_t(int((static_cast<int>(range.piece)
                           * piece_size + range.start + file_size - 1) / piece_size + 1));
    return std::make_tuple(range.piece, end_piece);
}

file_hashlist_t create_hashlist(const lt::torrent_info &torrent, const std::unordered_map<std::string, std::vector<std::string>> &linked_files) {
    file_hashlist_t files;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);
        std::vector<std::string> loaded_file_hashes;
        const auto range = file_piece_range(torrent.files(), file_index);
        for (lt::piece_index_t pi = std::get<0>(range); pi != std::get<1>(range); pi++) {
            const auto piece_hash = torrent.hash_for_piece(pi);
            const auto hash_vector = std::string(piece_hash.data(), piece_hash.size());
            loaded_file_hashes.push_back(hash_vector);
        }
        std::vector<std::string> parent_file_linked_files;
        if (linked_files.count(file_name) > 0) {
            parent_file_linked_files = linked_files.at(file_name);
        }
        files.insert({file_name, hashlist_t{loaded_file_hashes, parent_file_linked_files}});
    }
    return files;
}

std::ostream& serialize(const file_hashlist_t& files, std::ostream& os) {
    auto files_size = files.size();
    os.write(reinterpret_cast<char *>(&files_size), sizeof(uint32_t));
    for (const auto &h : files) {
        const auto &hashes = h.second.hashes;
        auto hashes_size = hashes.size();
        os.write(reinterpret_cast<char *>(&hashes_size), sizeof(uint32_t));
        for (const auto &h : hashes) {
            auto hash_size = h.size();
            os.write(reinterpret_cast<char *>(&hash_size), sizeof(uint32_t));
            os.write(h.data(), hash_size);
        }
        const auto &linked_files = h.second.linked_files;
        auto linked_files_size = linked_files.size();
        os.write(reinterpret_cast<char *>(&linked_files_size), sizeof(uint32_t));
        for (const auto &f : linked_files) {
            auto linked_file_name_size = f.size();
            os.write(reinterpret_cast<char *>(&linked_file_name_size), sizeof(uint32_t));
            os.write(f.data(), linked_file_name_size);
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
        std::vector<std::string> hashes;
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
            hashes.push_back(std::string(hash_bytes, hash_size));
        }
        uint32_t linked_file_size;
        is.read(reinterpret_cast<char *>(&linked_file_size), sizeof(uint32_t));
        if (!is) {
            throw StreamError("eof");
        }
        std::vector<std::string> linked_file_names;
        for (auto i = 0; i < hashes_size; i++) {
            char* linked_file_bytes = new char[linked_file_size]();
            for (auto i = 0; i < linked_file_size; i++) {
                is.read(linked_file_bytes + i, 1);
                if (!is) {
                    throw StreamError("eof");
                }
            }
            linked_file_names.push_back(std::string(linked_file_bytes, linked_file_size));
        }
        uint32_t name_size;
        is.read(reinterpret_cast<char *>(&name_size), sizeof(uint32_t));
        if (!is) {
            throw StreamError("eof");
        }
        char* name_bytes = new char[name_size]();
        for (auto i = 0; i < name_size; i++) {
            is.read(name_bytes + i, 1);
            if (!is) {
                throw StreamError("eof");
            }
        }
        std::string name(name_bytes, name_size);
        files[name] = hashlist_t{hashes, linked_file_names};
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

std::unordered_set<std::string> get_updated_files(const file_hashlist_t& files, const lt::torrent_info &torrent) {
    std::unordered_set<std::string> new_files;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);
        std::vector<std::string> loaded_file_hashes;
        if (files.count(file_name) > 0) {
            loaded_file_hashes = files.at(file_name).hashes;
        }
        const auto loaded_file_hashes_size = loaded_file_hashes.size();
        const auto range = file_piece_range(torrent.files(), file_index);
        size_t i = 0;
        bool equal_hashes = true;
        for (lt::piece_index_t pi = std::get<0>(range); pi != std::get<1>(range); pi++) {
            if (loaded_file_hashes_size <= i) {
                equal_hashes = false;
                break;
            }
            const auto piece_hash = torrent.hash_for_piece(pi);
            const auto hash_vector = std::string(piece_hash.data(), piece_hash.size());
            if (std::memcmp(loaded_file_hashes[i].data(), hash_vector.data(), piece_hash.size()) != 0) {
                equal_hashes = false;
                break;
            }
            i++;
        }
        if (!equal_hashes) {
            new_files.insert(file_name);
        }
    }
    return new_files;
}

std::unordered_set<std::string> get_removed_files(const file_hashlist_t& files, const lt::torrent_info &torrent) {
    std::unordered_set<std::string> removed_files;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_name = torrent.files().file_path(file_index);
        if (files.count(file_name) > 0) {
            continue;
        }
        removed_files.insert(file_name);
    }
    return removed_files;
}
