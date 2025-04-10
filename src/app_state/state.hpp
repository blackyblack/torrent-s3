#pragma once

#include <memory>
#include <string>
#include <optional>
#include <unordered_map>

#include <sqlite3.h>
#include "../hashlist/hashlist.hpp"

#define LINKED_FILES_TABLE_NAME "linked_files"
#define HASHLIST_TABLE_NAME "hashlist"
#define HASHLIST_LINKED_FILES_TABLE_NAME "hashlist_linked_files"

enum file_status_t {
    FILE_STATUS_UPLOADING = 0,
    FILE_STATUS_READY = 1
};

// NOTE: AppState is not thread-safe

class AppState {
public:
    AppState(std::shared_ptr<sqlite3> db_, bool reset = false);

    // removes all previous children of the file and adds new ones
    void add_uploading_files(std::string name, std::vector<std::string> children);
    std::optional<std::string> get_uploading_parent(std::string name) const;
    std::optional<file_status_t> get_file_status(std::string name) const;
    // mark file as 'ready'
    void file_complete(std::string name);
    void save_hashlist(file_hashlist_t hashlist);

    std::unordered_map<std::string, std::vector<std::string>> get_uploading_files() const;
    std::unordered_map<std::string, std::vector<std::string>> get_completed_files() const;
    file_hashlist_t get_hashlist() const;
    
private:
    std::shared_ptr<sqlite3> db;
};
