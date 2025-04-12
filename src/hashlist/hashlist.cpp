#include "./hashlist.hpp"

file_hashlist_t create_hashlist(const lt::torrent_info &torrent, const std::unordered_map<std::string, std::vector<std::string>> &linked_files) {
    file_hashlist_t files;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);
        const auto torrent_file_hashes = get_file_hashes(torrent, file_name);
        std::vector<std::string> parent_file_linked_files;
        if (linked_files.count(file_name) > 0) {
            parent_file_linked_files = linked_files.at(file_name);
        }
        files.insert({file_name, hashlist_t{torrent_file_hashes, parent_file_linked_files}});
    }
    return files;
}

std::unordered_set<std::string> get_updated_files(const lt::torrent_info &torrent, const file_hashlist_t& hashlist) {
    std::unordered_set<std::string> new_files;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);
        std::vector<std::string> loaded_file_hashes;
        if (hashlist.count(file_name) > 0) {
            loaded_file_hashes = hashlist.at(file_name).hashes;
        }
        const auto torrent_file_hashes = get_file_hashes(torrent, file_name);
        const auto torrent_hashes_size = torrent_file_hashes.size();
        const auto loaded_hashes_size = loaded_file_hashes.size();
        if (torrent_hashes_size != loaded_hashes_size) {
            new_files.insert(file_name);
            continue;
        }
        bool is_equal = true;
        for (auto i = 0; i < torrent_hashes_size; i++) {
            const auto hash1 = torrent_file_hashes[i];
            const auto hash2 = loaded_file_hashes[i];
            if (hash1.size() != hash2.size()) {
                is_equal = false;
                break;
            }
            if (std::memcmp(hash1.data(), hash2.data(), hash1.size()) != 0) {
                is_equal = false;
                break;
            }
        }
        if (!is_equal) {
            new_files.insert(file_name);
        }
    }
    return new_files;
}

std::unordered_set<std::string> get_removed_files(const lt::torrent_info &torrent, const file_hashlist_t& hashlist) {
    std::unordered_set<std::string> removed_files;
    for (const auto &f : hashlist) {
        removed_files.insert(f.first);
    }
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_name = torrent.files().file_path(file_index);
        removed_files.erase(file_name);
    }
    return removed_files;
}
