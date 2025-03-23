#include <stdexcept>
#include <variant>
#include <algorithm>
#include <iterator>

#include "./state.hpp"

struct file_with_status_t {
    std::string name;
    file_status_t status;
};

static std::unordered_map<std::string, std::vector<std::string>> get_linked_files_inner(std::shared_ptr<sqlite3> db, file_status_t status = file_status_t::FILE_STATUS_UPLOADING) {
    const auto select_query = std::string("SELECT file, parent FROM ") + LINKED_FILES_TABLE_NAME + " WHERE status=?;";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), select_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_bind_int(stmt, 1, status);
    std::unordered_map<std::string, std::vector<std::string>> ret;
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
        if (parent_ptr == nullptr) {
            ret[child] = {};
            continue;
        }
        const auto parent = std::string(reinterpret_cast<const char *>(parent_ptr));
        auto &children = ret[parent];
        children.push_back(child);
    }
    sqlite3_finalize(stmt);
    return ret;
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

std::unordered_map<std::string, std::vector<std::string>> AppState::get_uploading_files() const {
    return get_linked_files_inner(db, file_status_t::FILE_STATUS_UPLOADING);
}

std::unordered_map<std::string, std::vector<std::string>> AppState::get_completed_files() const {
    return get_linked_files_inner(db, file_status_t::FILE_STATUS_READY);
}

void AppState::add_uploading_files(std::string name, std::vector<std::string> children) {
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

std::optional<std::string> AppState::get_uploading_parent(std::string name) const {
    const auto select_query = std::string("SELECT parent FROM ") + LINKED_FILES_TABLE_NAME + " WHERE file=? AND status=0;";
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

void AppState::file_complete(std::string name) {
    set_file_status(db, name, file_status_t::FILE_STATUS_READY);
}
