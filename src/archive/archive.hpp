#pragma once

#include <stdexcept>
#include <string>
#include <vector>

class ArchiveError : public std::runtime_error {
  public:
    ArchiveError(std::string message);
};

struct file_unpack_info_t {
    std::string name;
    std::string error_message;
};

bool is_packed(std::string file_name);

std::vector<file_unpack_info_t> unpack_file(std::string file_name, std::string output_directory);
