#include "./sqlite.hpp"

std::variant<std::shared_ptr<sqlite3>, std::string> db_open(const std::string &path) {
    sqlite3 *db_raw = nullptr;
    const auto db_open_ret = sqlite3_open(path.c_str(), &db_raw);
    if (db_open_ret != SQLITE_OK) {
        return sqlite3_errmsg(db_raw);
    }
    std::shared_ptr<sqlite3> db(nullptr);
    db.reset(db_raw, sqlite3_close);
    return db;
}
