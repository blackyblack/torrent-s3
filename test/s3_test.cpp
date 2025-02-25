#include <filesystem>
#include <chrono>
#include <thread>
#include <gtest/gtest.h>

#include "./test_utils.hpp"

#include "../src/s3/s3.hpp"

TEST(s3_test, start_stop) {
    S3Uploader uploader(1, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "asiatrip", "", "");
    uploader.start();
    uploader.stop();
}

TEST(s3_test, bad_file) {
    S3Uploader uploader(1, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "asiatrip", "", "");
    auto &progress_queue = uploader.get_progress_queue();
    uploader.start();
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
    S3Uploader uploader(4, "http://play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "asiatrip", "", "");
    auto &progress_queue = uploader.get_progress_queue();
    uploader.start();
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
