#pragma once

#include <vector>
#include <filesystem>

// folder for extracted files has the same name as archive, but .extension is replaced with _extension
std::filesystem::path folder_for_unpacked_file(const std::filesystem::path file_name);

// convert absolute path to relative by stripping root folder.
// If file_name is not in root folder, return file_name
std::filesystem::path path_to_relative(const std::filesystem::path file_name, const std::filesystem::path root);
