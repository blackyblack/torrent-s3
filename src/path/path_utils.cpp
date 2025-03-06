#include "./path_utils.hpp"

std::string folder_for_unpacked_file(const std::string file_name) {
    const auto path = std::filesystem::path(file_name);
    auto ext = path.extension().string();
    if(ext.find_last_of(".") != std::string::npos) {
        ext = ext.substr(ext.find_last_of(".") + 1);
    }
    return (path.parent_path() / (path.stem().string() + "_" + ext)).string();
}

std::string path_to_relative(const std::string file_name, const std::string root) {
    const auto from = std::filesystem::absolute(file_name);
    const auto base = std::filesystem::absolute(root);
    auto from_it = from.begin();
    for (auto base_it = base.begin(); base_it != base.end(); base_it++) {
        if (from_it == from.end() || *from_it != *base_it) {
            return file_name;
        }
        from_it++;
    }
    auto save_to_filename = std::filesystem::path("./");
    while (from_it != from.end()) {
        save_to_filename /= *from_it;
        from_it++;
    }
    return save_to_filename.string();
}
