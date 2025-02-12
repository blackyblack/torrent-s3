#include <filesystem>
#include <gtest/gtest.h>

#include "../src/archive/archive.hpp"

#define STRING(x) #x
#define XSTRING(x) STRING(x)

#define APP_NAME XSTRING(CMAKE_PROJECT_NAME)
#define SOURCE_DIR XSTRING(CMAKE_SOURCE_DIR)

std::string get_asset(std::string file) {
    return (std::filesystem::path(SOURCE_DIR) / std::filesystem::path("test/assets") / std::filesystem::path(file)).string();
}

std::string get_tmp_dir() {
    return std::string("./tmp-") + APP_NAME;
}

TEST(archive_test, is_packed) {
    EXPECT_FALSE(is_packed(get_asset("1.txt")));
    EXPECT_TRUE(is_packed(get_asset("1.rar")));
    EXPECT_TRUE(is_packed(get_asset("1.zip")));
    EXPECT_TRUE(is_packed(get_asset("2.rar")));
    EXPECT_TRUE(is_packed(get_asset("3.zip")));
    EXPECT_FALSE(is_packed(get_asset("4.zip")));
}

TEST(archive_test, unpack_no_file) {
    EXPECT_THROW(unpack_file(get_asset("0.txt"), get_tmp_dir()), ArchiveError);
}

TEST(archive_test, unpack_fail) {
    EXPECT_THROW(unpack_file(get_asset("1.txt"), get_tmp_dir()), ArchiveError);
}

TEST(archive_test, unpack_zip) {
    const auto ret = unpack_file(get_asset("1.zip"), get_tmp_dir());
    EXPECT_EQ(ret.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_rar) {
    const auto ret = unpack_file(get_asset("1.rar"), get_tmp_dir());
    EXPECT_EQ(ret.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_rar4) {
    const auto ret = unpack_file(get_asset("2.rar"), get_tmp_dir());
    EXPECT_EQ(ret.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_zip_multi) {
    const auto ret = unpack_file(get_asset("3.zip"), get_tmp_dir());
    EXPECT_EQ(ret.size(), 2);
    std::filesystem::remove_all(get_tmp_dir());
}
