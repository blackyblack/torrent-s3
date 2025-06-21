#include <filesystem>
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif // _WIN32

#include "./test_utils.hpp"

#include "../src/archive/archive.hpp"

TEST(archive_test, is_packed) {
    EXPECT_FALSE(is_packed(get_asset("1.txt")));
    EXPECT_TRUE(is_packed(get_asset("1.rar")));
    EXPECT_TRUE(is_packed(get_asset("1.zip")));
    EXPECT_TRUE(is_packed(get_asset("2.rar")));
    EXPECT_TRUE(is_packed(get_asset("3.zip")));
    EXPECT_FALSE(is_packed(get_asset("4.zip")));
}

TEST(archive_test, unpack_no_file) {
    const auto ret = unpack_file(get_asset("0.txt"), get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::string>(ret));
}

TEST(archive_test, unpack_fail) {
    const auto ret = unpack_file(get_asset("1.txt"), get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::string>(ret));
}

TEST(archive_test, unpack_zip) {
    const auto ret = unpack_file(get_asset("1.zip"), get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_rar) {
    const auto ret = unpack_file(get_asset("1.rar"), get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_rar4) {
    const auto ret = unpack_file(get_asset("2.rar"), get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_zip_multi) {
    const auto ret = unpack_file(get_asset("3.zip"), get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 2);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_with_autofolder) {
    const auto ret = unpack_file(get_asset("1.zip"), (std::filesystem::path(get_tmp_dir()) / "1.zip").string());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_with_autofolder_in_same_directory) {
    const auto dest_file = std::filesystem::path(get_tmp_dir()) / "1.zip";
    std::filesystem::create_directory(get_tmp_dir());
    std::filesystem::copy_file(get_asset("1.zip"), dest_file);
    const auto ret = unpack_file(dest_file.string(), dest_file.string());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_unicode_name) {
    const auto filename = std::filesystem::path(SOURCE_DIR) / "test/assets/я.zip";
    const auto ret = unpack_file(filename, get_tmp_dir());
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, unpack_unicode_output_name) {
    const auto filename = std::filesystem::path(SOURCE_DIR) / "test/assets/я.zip";

    // also test unicode console output here
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif // _WIN32
    fprintf(stderr, "Console output. File is \"%s\"\n", filename.string().c_str());

    const auto ret = unpack_file(filename, std::filesystem::path(get_tmp_dir()) / "я");
    EXPECT_TRUE(std::holds_alternative<std::vector<file_unpack_info_t>>(ret));
    const auto files = std::get<std::vector<file_unpack_info_t>>(ret);
    EXPECT_EQ(files.size(), 1);
    std::filesystem::remove_all(get_tmp_dir());
}

TEST(archive_test, zip_file) {
    const auto filename = std::filesystem::path(SOURCE_DIR) / "test/assets/Документ Microsoft Word (2).htm";
    const auto zip_file_path = std::filesystem::path(get_tmp_dir()) / "я" / "Документ Microsoft Word (2).htm.zip";
    const auto ret = zip_file(filename, zip_file_path);
    EXPECT_FALSE(ret.has_value());
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::u8path(zip_file_path.string()).string()));
    EXPECT_TRUE(is_packed(zip_file_path.string()));
    std::filesystem::remove_all(get_tmp_dir());
}

