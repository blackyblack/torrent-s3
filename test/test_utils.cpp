#include <filesystem>

#include "./test_utils.hpp"

std::string get_asset(std::string file) {
    return (std::filesystem::path(SOURCE_DIR) / std::filesystem::path("test/assets") / std::filesystem::path(file)).string();
}

std::string get_tmp_dir() {
    return std::string("./tmp-") + APP_NAME;
}
