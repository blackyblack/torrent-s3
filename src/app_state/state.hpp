#pragma once

#include <memory>
#include <string>
#include <optional>
#include <unordered_map>

#include <sqlite3.h>

#define LINKED_FILES_TABLE_NAME "linked_files"

enum file_status_t {
    FILE_STATUS_WAITING = 0,
    FILE_STATUS_DOWNLOADING = 1,
    FILE_STATUS_UPLOADING = 2,
    FILE_STATUS_READY = 3
};

// NOTE: AppState is not thread-safe

class AppState {
public:
    AppState(std::shared_ptr<sqlite3> db_, bool reset = false);

    void add_files(std::string name, std::vector<std::string> children);
    void remove_file(std::string name);
    std::optional<std::string> get_parent(std::string name) const;
    std::optional<file_status_t> get_file_status(std::string name) const;
    // mark file as 'downloading'
    void file_download(std::string name);
    // mark file as 'uploading'
    void file_upload(std::string name);
    // mark file as 'ready'
    void file_complete(std::string name);

    std::unordered_map<std::string, std::vector<std::string>> get_linked_files() const;
    std::unordered_map<std::string, std::vector<std::string>> get_uploading_files() const;
    
private:
    std::shared_ptr<sqlite3> db;
};

// look up for a parent, but only for files with 'uploading' status
std::optional<std::string> get_uploading_parent(const AppState &state, std::string file_name);
