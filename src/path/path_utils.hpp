#pragma once

#include <vector>

#include <filesystem>

// folder for extracted files has the same name as archive, but .extension is replaced with _extension
std::string folder_for_unpacked_file(const std::string file_name);

// convert absolute path to relative by stripping root folder.
// If file_name is not in root folder, return file_name
std::string path_to_relative(const std::string file_name, const std::string root);
