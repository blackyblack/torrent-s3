#pragma once

#include <string>
#include <vector>
#include <variant>
#include <optional>

struct file_unpack_info_t {
    std::string name;
    std::optional<std::string> error_message;
};

bool is_packed(std::string file_name);

std::variant<std::vector<file_unpack_info_t>, std::string> unpack_file(std::string file_name, std::string output_directory);
