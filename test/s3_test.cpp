#include <filesystem>
#include <gtest/gtest.h>
#ifdef _WIN32
    #include <windows.h>
#endif // _WIN32

#include "./test_utils.hpp"

#include "../src/s3/s3.hpp"

TEST(s3_test, start_stop) {
    S3Uploader uploader(1, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", "./" , "");
    const auto ret = uploader.start();
    EXPECT_FALSE(ret.has_value());
    uploader.stop();
}

TEST(s3_test, bad_file) {
    #ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
    #endif // _WIN32

    S3Uploader uploader(1, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", "./", "");
    auto &progress_queue = uploader.get_progress_queue();
    const auto ret = uploader.start();
    EXPECT_FALSE(ret.has_value());
    EXPECT_TRUE(progress_queue.empty());
    const auto nonexisting_file = get_asset("nonexisting_file");
    uploader.new_file(nonexisting_file);
    uploader.stop();
    EXPECT_FALSE(progress_queue.empty());
    const auto s3_event = progress_queue.pop_front_waiting();
    const auto &download_error = std::get<S3ProgressUploadError>(s3_event);
    EXPECT_TRUE(download_error.error.length() > 0);
    EXPECT_EQ(download_error.file_name, nonexisting_file);
}

TEST(s3_test, parallel_files) {
    S3Uploader uploader(4, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", "./", "");
    auto &progress_queue = uploader.get_progress_queue();
    const auto ret = uploader.start();
    EXPECT_FALSE(ret.has_value());
    EXPECT_TRUE(progress_queue.empty());
    for (int i = 0; i < 4; i++) {
        uploader.new_file(get_asset("1.txt"));
        uploader.new_file(get_asset("2.txt"));
    }
    uploader.stop();
    EXPECT_FALSE(progress_queue.empty());
    for (int i = 0; i < 8; i++) {
        const auto s3_event = progress_queue.pop_front_waiting();
        const auto &download_ok = std::get<S3ProgressUploadOk>(s3_event);
        EXPECT_TRUE(download_ok.file_name.length() > 0);
    }
    EXPECT_TRUE(progress_queue.empty());
}

TEST(s3_test, use_path_from) {
    const auto path_from = std::filesystem::path(SOURCE_DIR) / std::filesystem::path("test/assets");
    const auto filename = "1.txt";
    S3Uploader uploader(1, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", path_from, "upload");
    auto &progress_queue = uploader.get_progress_queue();
    const auto ret = uploader.start();
    EXPECT_FALSE(ret.has_value());
    EXPECT_TRUE(progress_queue.empty());
    uploader.new_file(filename);
    uploader.stop();
    EXPECT_FALSE(progress_queue.empty());
    const auto s3_event = progress_queue.pop_front_waiting();
    const auto &download_ok = std::get<S3ProgressUploadOk>(s3_event);
    EXPECT_EQ(download_ok.file_name, filename);
}

TEST(s3_test, unicode_name) {
    #ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
    #endif // _WIN32

    const auto path_from = std::filesystem::path(SOURCE_DIR) / std::filesystem::path("test/assets");
    const auto unicode_file = std::string("Документ Microsoft Word (2).htm");
    S3Uploader uploader(1, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", path_from, "upload");
    auto &progress_queue = uploader.get_progress_queue();
    const auto ret = uploader.start();
    EXPECT_FALSE(ret.has_value());
    EXPECT_TRUE(progress_queue.empty());
    uploader.new_file(unicode_file);
    uploader.stop();
    EXPECT_FALSE(progress_queue.empty());
    const auto s3_event = progress_queue.pop_front_waiting();
    const auto &download_ok = std::get<S3ProgressUploadOk>(s3_event);
    EXPECT_EQ(download_ok.file_name, unicode_file);
}
