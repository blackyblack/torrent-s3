#include "./state.hpp"
#include <stdexcept>
#include <variant>
#include <algorithm>
#include <iterator>

struct file_with_status_t {
    std::string name;
    file_status_t status;
};

static std::unordered_map<std::string, std::variant<std::vector<file_with_status_t>, file_status_t>> get_linked_files_inner(std::shared_ptr<sqlite3> db) {
    const auto select_query = std::string("SELECT file, parent, status FROM ") + LINKED_FILES_TABLE_NAME + ";";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), select_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    std::unordered_map<std::string, std::variant<std::vector<file_with_status_t>, file_status_t>> linked_files;
    while (true) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        const auto child = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        const auto parent_ptr = sqlite3_column_text(stmt, 1);
        const auto status = sqlite3_column_int(stmt, 2);
        if (parent_ptr == nullptr) {
            linked_files[child] = static_cast<file_status_t>(status);
            continue;
        }
        const auto parent = std::string(reinterpret_cast<const char *>(parent_ptr));
        auto &children = std::get<std::vector<file_with_status_t>>(linked_files[parent]);
        children.push_back({child, static_cast<file_status_t>(status)});
    }
    sqlite3_finalize(stmt);
    return linked_files;
}

AppState::AppState(std::shared_ptr<sqlite3> db_, bool reset) : db {db_} {
    if (reset) {
        char *err_msg = nullptr;
        const auto drop_table_query = std::string("DROP TABLE IF EXISTS ") + LINKED_FILES_TABLE_NAME + ";";
        const auto rc = sqlite3_exec(db.get(), drop_table_query.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            const auto err_msg_str = std::string(err_msg);
            sqlite3_free(err_msg);
            throw std::runtime_error("Failed to drop table: " + err_msg_str);
        }
    }
    char *err_msg = nullptr;
    const auto create_table_query = std::string("CREATE TABLE IF NOT EXISTS ") + LINKED_FILES_TABLE_NAME + " (file TEXT PRIMARY KEY, parent TEXT, status INT NOT NULL);";
    const auto rc = sqlite3_exec(db.get(), create_table_query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to create table: " + err_msg_str);
    }
}

std::unordered_map<std::string, std::vector<std::string>> AppState::get_linked_files() const {
    std::unordered_map<std::string, std::vector<std::string>> ret;
    const auto files = get_linked_files_inner(db);
    for (const auto &p : files) {
        if (std::holds_alternative<file_status_t>(p.second)) {
            ret[p.first] = {};
            continue;
        }
        const auto linked_files = std::get<std::vector<file_with_status_t>>(p.second);
        std::vector<std::string> linked_file_names;
        std::transform(linked_files.begin(), linked_files.end(), std::back_inserter(linked_file_names), [](const auto &f) {
            return f.name;
        });
        ret[p.first] = linked_file_names;
    }
    return ret;
}

std::unordered_map<std::string, std::vector<std::string>> AppState::get_uploading_files() const {
    std::unordered_map<std::string, std::vector<std::string>> ret;
    const auto files = get_linked_files_inner(db);
    for (const auto &p : files) {
        if (std::holds_alternative<file_status_t>(p.second)) {
            if (std::get<file_status_t>(p.second) != file_status_t::FILE_STATUS_UPLOADING) {
                continue;
            }
            ret[p.first] = {};
            continue;
        }
        const auto linked_files = std::get<std::vector<file_with_status_t>>(p.second);
        std::vector<file_with_status_t> filtered_files;
        std::copy_if(linked_files.begin(), linked_files.end(), std::back_inserter(filtered_files), [](const auto &f) {
            return f.status == file_status_t::FILE_STATUS_UPLOADING;
        });
        std::vector<std::string> linked_file_names;
        std::transform(filtered_files.begin(), filtered_files.end(), std::back_inserter(linked_file_names), [](const auto &f) {
            return f.name;
        });
        if (linked_file_names.size() > 0) {
            ret[p.first] = linked_file_names;
        }
    }
    return ret;
}

void AppState::add_files(std::string name, std::vector<std::string> children) {
    char *err_msg = nullptr;
    auto rc = sqlite3_exec(db.get(), "BEGIN TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to begin transaction: " + err_msg_str);
    }

    const auto insert_query = std::string("INSERT OR IGNORE INTO ") + LINKED_FILES_TABLE_NAME + " (file, parent, status) VALUES (?, ?, 0);";
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db.get(), insert_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert statement: " + std::string(sqlite3_errmsg(db.get())));
    }

    for(const auto &c : children) {
        sqlite3_bind_text(stmt, 1, c.c_str(), c.size(), 0);
        sqlite3_bind_text(stmt, 2, name.c_str(), name.size(), 0);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_reset(stmt);
    }
    if (children.size() == 0) {
        sqlite3_bind_text(stmt, 1, name.c_str(), name.size(), 0);
        sqlite3_bind_null(stmt, 2);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_reset(stmt);
    }
    rc = sqlite3_exec(db.get(), "COMMIT TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to commit transaction: " + err_msg_str);
    }
    sqlite3_finalize(stmt);
}

void AppState::remove_file(std::string name) {
    const auto delete_query = std::string("DELETE FROM ") + LINKED_FILES_TABLE_NAME + " WHERE file=?;";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), delete_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare delete statement: " + std::string(sqlite3_errmsg(db.get())));
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), name.size(), 0);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_finalize(stmt);
}

std::optional<file_status_t> AppState::get_file_status(std::string name) const {
    const auto select_query = std::string("SELECT status FROM ") + LINKED_FILES_TABLE_NAME + " WHERE file=?;";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), select_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), name.size(), 0);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    if (rc != SQLITE_ROW) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    const auto status = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return static_cast<file_status_t>(status);
}

static void set_file_status(std::shared_ptr<sqlite3> db, std::string name, file_status_t status) {
    char *err_msg = nullptr;
    const auto update_query = std::string("UPDATE OR IGNORE ") + LINKED_FILES_TABLE_NAME + " SET status=? where file=?;";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), update_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare update statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_text(stmt, 2, name.c_str(), name.size(), 0);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_finalize(stmt);
}

std::optional<std::string> AppState::get_parent(std::string name) const {
    const auto select_query = std::string("SELECT parent FROM ") + LINKED_FILES_TABLE_NAME + " WHERE file=?;";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), select_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), name.size(), 0);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    if (rc != SQLITE_ROW) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    const auto parent_ptr = sqlite3_column_text(stmt, 0);
    if (parent_ptr == nullptr) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    const auto parent = std::string(reinterpret_cast<const char *>(parent_ptr));
    sqlite3_finalize(stmt);
    return parent;
}

void AppState::file_download(std::string name) {
    set_file_status(db, name, file_status_t::FILE_STATUS_DOWNLOADING);
}

void AppState::file_upload(std::string name) {
    set_file_status(db, name, file_status_t::FILE_STATUS_UPLOADING);
}

void AppState::file_complete(std::string name) {
    set_file_status(db, name, file_status_t::FILE_STATUS_READY);
}

std::optional<std::string> get_uploading_parent(const AppState &state, std::string file_name) {
    const auto maybe_parent = state.get_parent(file_name);
    if (!maybe_parent.has_value()) {
        return std::nullopt;
    }
    const auto uploading_files = state.get_uploading_files();
    if (uploading_files.find(maybe_parent.value()) == uploading_files.end()) {
        return std::nullopt;
    }
    return maybe_parent.value();
}
