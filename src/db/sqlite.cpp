#include <filesystem>

#include "./sqlite.hpp"

std::variant<std::shared_ptr<sqlite3>, std::string> db_open(const std::string &path) {
    sqlite3 *db_raw = nullptr;
    if (path != ":memory:") {
        const auto fs_path = std::filesystem::path(path);
        if (!std::filesystem::exists(fs_path) && !std::filesystem::is_directory(path)) {
            const auto parent_path = fs_path.parent_path();
            if (!std::filesystem::exists(parent_path)) {
                std::filesystem::create_directory(fs_path.parent_path());
            }
        }
    }
    const auto db_open_ret = sqlite3_open_v2(path.c_str(), &db_raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (db_open_ret != SQLITE_OK) {
        return sqlite3_errmsg(db_raw);
    }
    std::shared_ptr<sqlite3> db(nullptr);
    db.reset(db_raw, sqlite3_close);
    return db;
}
