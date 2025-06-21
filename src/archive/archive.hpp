#pragma once

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <filesystem>

struct file_unpack_info_t {
    std::string name;
    std::optional<std::string> error_message;
};

bool is_packed(std::filesystem::path file_name);

std::variant<std::vector<file_unpack_info_t>, std::string> unpack_file(std::filesystem::path file_name, std::filesystem::path output_directory);

std::optional<std::string> zip_file(std::filesystem::path file_name, std::filesystem::path output_directory);
