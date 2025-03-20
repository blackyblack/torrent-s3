#pragma once

#include <memory>
#include <string>
#include <variant>

#include <sqlite3.h>

std::variant<std::shared_ptr<sqlite3>, std::string> db_open(const std::string &path);
