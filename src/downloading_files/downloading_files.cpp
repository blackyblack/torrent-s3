#include "./downloading_files.hpp"

DownloadingFiles::DownloadingFiles(const lt::torrent_info& torrent_, std::vector<std::string> updated_files, unsigned long long size_limit_bytes) :
    torrent {torrent_},
    torrent_files {updated_files.begin(), updated_files.end()},
    size_limit {size_limit_bytes}
{}

std::vector<std::string> DownloadingFiles::download_next_chunk() {
    unsigned long long total_size = 0;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);

        if (torrent_files.find(file_name) == torrent_files.end()) {
            continue;
        }
        if (completed_files.find(file_name) != completed_files.end()) {
            continue;
        }
        if (downloading_files.find(file_name) != downloading_files.end()) {
            total_size += file_size;
        }
    }

    std::vector<std::string> to_download_files;
    int first_uncompleted_index = -1;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);

        if (torrent_files.find(file_name) == torrent_files.end()) {
            continue;
        }
        if (completed_files.find(file_name) != completed_files.end()) {
            continue;
        }
        if (downloading_files.find(file_name) != downloading_files.end()) {
            continue;
        }
        if (first_uncompleted_index < 0) first_uncompleted_index = (int) file_index;
        if (total_size + file_size > size_limit) {
            continue;
        }
        to_download_files.push_back(file_name);
        downloading_files.insert(file_name);
        total_size += file_size;
    }

    // if no file fits a size limit, add first available file and download one by one
    if (total_size == 0 && first_uncompleted_index >= 0) {
        const auto file_name = torrent.files().file_path(lt::file_index_t{first_uncompleted_index});
        to_download_files.push_back(file_name);
        downloading_files.insert(file_name);
    }
    return to_download_files;
}

void DownloadingFiles::complete_file(std::string file_name) {
    const auto file_to_erase = downloading_files.find(file_name);
    if (file_to_erase != downloading_files.end()) {
        downloading_files.erase(file_to_erase);
    }
    completed_files.insert(file_name);
}

bool DownloadingFiles::is_completed() const {
    std::unordered_set<std::string> intersect;
    for (const auto f : completed_files) {
        if (torrent_files.count(f)) { intersect.insert(f); }
    }
    return intersect == torrent_files;
}
