#include <stdexcept>
#include <variant>
#include <algorithm>
#include <iterator>

#include "./state.hpp"

struct file_with_status_t {
    std::string name;
    file_status_t status;
};

static std::unordered_map<std::string, std::vector<std::string>> get_all_linked_files_inner(std::shared_ptr<sqlite3> db) {
    const auto select_query = std::string("SELECT file, parent FROM ") + LINKED_FILES_TABLE_NAME + ";";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), select_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
    }
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
        auto drop_table_query = std::string("DROP TABLE IF EXISTS ") + LINKED_FILES_TABLE_NAME + ";";
        auto rc = sqlite3_exec(db.get(), drop_table_query.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            const auto err_msg_str = std::string(err_msg);
            sqlite3_free(err_msg);
            throw std::runtime_error("Failed to drop table: " + err_msg_str);
        }

        drop_table_query = std::string("DROP TABLE IF EXISTS ") + HASHLIST_TABLE_NAME + ";";
        rc = sqlite3_exec(db.get(), drop_table_query.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            const auto err_msg_str = std::string(err_msg);
            sqlite3_free(err_msg);
            throw std::runtime_error("Failed to drop table: " + err_msg_str);
        }

        drop_table_query = std::string("DROP TABLE IF EXISTS ") + HASHLIST_LINKED_FILES_TABLE_NAME + ";";
        rc = sqlite3_exec(db.get(), drop_table_query.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            const auto err_msg_str = std::string(err_msg);
            sqlite3_free(err_msg);
            throw std::runtime_error("Failed to drop table: " + err_msg_str);
        }
    }
    char *err_msg = nullptr;
    auto create_table_query = std::string("CREATE TABLE IF NOT EXISTS ") + LINKED_FILES_TABLE_NAME + " (file TEXT PRIMARY KEY, parent TEXT, status INT NOT NULL);";
    auto rc = sqlite3_exec(db.get(), create_table_query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to create table: " + err_msg_str);
    }

    create_table_query = std::string("CREATE TABLE IF NOT EXISTS ") + HASHLIST_TABLE_NAME + " (piece_hash TEXT PRIMARY KEY, file TEXT NOT NULL);";
    rc = sqlite3_exec(db.get(), create_table_query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to create table: " + err_msg_str);
    }

    create_table_query = std::string("CREATE TABLE IF NOT EXISTS ") + HASHLIST_LINKED_FILES_TABLE_NAME + " (file TEXT PRIMARY KEY, parent TEXT NOT NULL);";
    rc = sqlite3_exec(db.get(), create_table_query.c_str(), nullptr, nullptr, &err_msg);
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

    sqlite3_stmt *stmt = nullptr;
    const auto delete_query = std::string("DELETE FROM ") + LINKED_FILES_TABLE_NAME + " WHERE parent=?;";

    rc = sqlite3_prepare_v2(db.get(), delete_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare delete statement: " + std::string(sqlite3_errmsg(db.get())));
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), name.size(), 0);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_finalize(stmt);

    const auto insert_query = std::string("INSERT OR IGNORE INTO ") + LINKED_FILES_TABLE_NAME + " (file, parent, status) VALUES (?, ?, 0);";
    sqlite3_stmt *stmt2 = nullptr;
    rc = sqlite3_prepare_v2(db.get(), insert_query.c_str(), -1, &stmt2, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert statement: " + std::string(sqlite3_errmsg(db.get())));
    }

    for(const auto &c : children) {
        sqlite3_bind_text(stmt2, 1, c.c_str(), c.size(), 0);
        sqlite3_bind_text(stmt2, 2, name.c_str(), name.size(), 0);
        rc = sqlite3_step(stmt2);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_reset(stmt2);
    }
    if (children.size() == 0) {
        sqlite3_bind_text(stmt2, 1, name.c_str(), name.size(), 0);
        sqlite3_bind_null(stmt2, 2);
        rc = sqlite3_step(stmt2);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_reset(stmt2);
    }
    sqlite3_finalize(stmt2);

    // additional update since previous delete might skip file that changed its parent status
    const auto update_query = std::string("UPDATE OR IGNORE ") + LINKED_FILES_TABLE_NAME + " SET parent=?, status=0 where file=?;";
    sqlite3_stmt *stmt3 = nullptr;
    rc = sqlite3_prepare_v2(db.get(), update_query.c_str(), -1, &stmt3, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare update statement: " + std::string(sqlite3_errmsg(db.get())));
    }

    for(const auto &c : children) {
        sqlite3_bind_text(stmt3, 1, name.c_str(), name.size(), 0);
        sqlite3_bind_text(stmt3, 2, c.c_str(), c.size(), 0);
        rc = sqlite3_step(stmt3);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_reset(stmt3);
    }
    if (children.size() == 0) {
        sqlite3_bind_null(stmt3, 1);
        sqlite3_bind_text(stmt3, 2, name.c_str(), name.size(), 0);
        rc = sqlite3_step(stmt3);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_reset(stmt3);
    }
    sqlite3_finalize(stmt3);

    rc = sqlite3_exec(db.get(), "COMMIT TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to commit transaction: " + err_msg_str);
    }
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

void AppState::save_hashlist(file_hashlist_t hashlist) {
    char *err_msg = nullptr;
    auto rc = sqlite3_exec(db.get(), "BEGIN TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to begin transaction: " + err_msg_str);
    }

    // delete all previous hashes
    const auto delete_hashes_query = std::string("DELETE FROM ") + HASHLIST_TABLE_NAME + ";";
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db.get(), delete_hashes_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare delete statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_finalize(stmt);

    // delete all previous linked files
    const auto delete_files_query = std::string("DELETE FROM ") + HASHLIST_LINKED_FILES_TABLE_NAME + ";";
    sqlite3_stmt *stmt2 = nullptr;
    rc = sqlite3_prepare_v2(db.get(), delete_files_query.c_str(), -1, &stmt2, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare delete statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    rc = sqlite3_step(stmt2);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
    }
    sqlite3_finalize(stmt2);

    for (const auto &f : hashlist) {
        const auto &name = f.first;
        const auto &hashes = f.second.hashes;

        const auto insert_hashes_query = std::string("INSERT OR IGNORE INTO ") + HASHLIST_TABLE_NAME + " (piece_hash, file) VALUES (?, ?);";
        sqlite3_stmt *stmt3 = nullptr;
        rc = sqlite3_prepare_v2(db.get(), insert_hashes_query.c_str(), -1, &stmt3, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare insert statement: " + std::string(sqlite3_errmsg(db.get())));
        }

        for(const auto &h : hashes) {
            sqlite3_bind_text(stmt3, 1, h.c_str(), h.size(), 0);
            sqlite3_bind_text(stmt3, 2, name.c_str(), name.size(), 0);
            rc = sqlite3_step(stmt3);
            if (rc != SQLITE_DONE) {
                throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
            }
            sqlite3_reset(stmt3); 
        }
        sqlite3_finalize(stmt3);
    }

    for (const auto &f : hashlist) {
        const auto &name = f.first;
        const auto &files = f.second.linked_files;

        if (files.size() == 0) {
            continue;
        }

        const auto insert_files_query = std::string("INSERT OR IGNORE INTO ") + HASHLIST_LINKED_FILES_TABLE_NAME + " (file, parent) VALUES (?, ?);";
        sqlite3_stmt *stmt4 = nullptr;
        rc = sqlite3_prepare_v2(db.get(), insert_files_query.c_str(), -1, &stmt4, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare insert statement: " + std::string(sqlite3_errmsg(db.get())));
        }

        for(const auto &f : files) {
            sqlite3_bind_text(stmt4, 1, f.c_str(), f.size(), 0);
            sqlite3_bind_text(stmt4, 2, name.c_str(), name.size(), 0);
            rc = sqlite3_step(stmt4);
            if (rc != SQLITE_DONE) {
                throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
            }
            sqlite3_reset(stmt4); 
        }
        sqlite3_finalize(stmt4);
    }

    rc = sqlite3_exec(db.get(), "COMMIT TRANSACTION", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        const auto err_msg_str = std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error("Failed to commit transaction: " + err_msg_str);
    }
}

file_hashlist_t AppState::get_hashlist() const {
    file_hashlist_t hashlist;
    const auto select_hashes_query = std::string("SELECT piece_hash, file FROM ") + HASHLIST_TABLE_NAME + ";";
    sqlite3_stmt *stmt = nullptr;
    auto rc = sqlite3_prepare_v2(db.get(), select_hashes_query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
    }
    while (true) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
        }
        const auto hash = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        const auto file = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
        auto &hashes = hashlist[file].hashes;
        hashes.push_back(hash);
    }
    sqlite3_finalize(stmt);

    for (const auto &f : hashlist) {
        const auto &name = f.first;
        const auto select_linked_files_query = std::string("SELECT file FROM ") + HASHLIST_LINKED_FILES_TABLE_NAME + " WHERE parent=?;";
        sqlite3_stmt *stmt2 = nullptr;
        rc = sqlite3_prepare_v2(db.get(), select_linked_files_query.c_str(), -1, &stmt2, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(db.get())));
        }
        sqlite3_bind_text(stmt2, 1, name.c_str(), name.size(), 0);
        while (true) {
            rc = sqlite3_step(stmt2);
            if (rc == SQLITE_DONE) {
                break;
            }
            if (rc != SQLITE_ROW) {
                throw std::runtime_error("Failed to step: " + std::string(sqlite3_errmsg(db.get())));
            }
            const auto file_name = std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt2, 0)));
            auto &linked_files = hashlist[name].linked_files;
            linked_files.push_back(file_name);
        }
        sqlite3_finalize(stmt2);
    }
    return hashlist;
}
