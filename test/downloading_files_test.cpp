#include <gtest/gtest.h>

#include "./test_utils.hpp"

#include "../src/downloading_files/downloading_files.hpp"

TEST(downloading_files_test, unlimited_size) {
    const auto torrent_file = get_asset("test.torrent");
    lt::torrent_info ti(torrent_file);
    std::vector<std::string> new_files;
    for (const auto &file_index: ti.files().file_range()) {
        const auto file_name = ti.files().file_path(file_index);
        new_files.push_back(file_name);
    }
    EXPECT_EQ(new_files.size(), 3);
    DownloadingFiles downloading_files(ti, new_files, LLONG_MAX);
    EXPECT_FALSE(downloading_files.is_completed());
    auto files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 3);
    downloading_files.complete_file(new_files[0]);
    EXPECT_FALSE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    // all files are in `downloading` state so no new files are selected for download
    EXPECT_EQ(files_to_download.size(), 0);
    downloading_files.complete_file(new_files[1]);
    EXPECT_FALSE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 0);
    downloading_files.complete_file(new_files[2]);
    EXPECT_TRUE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 0);
}

TEST(downloading_files_test, one_file_size) {
    const auto torrent_file = get_asset("test.torrent");
    lt::torrent_info ti(torrent_file);
    std::vector<std::string> new_files;
    for (const auto &file_index: ti.files().file_range()) {
        const auto file_name = ti.files().file_path(file_index);
        new_files.push_back(file_name);
    }
    EXPECT_EQ(new_files.size(), 3);
    // limit to 100 bytes - should result to one file in downloads at a time
    DownloadingFiles downloading_files(ti, new_files, 100);
    EXPECT_FALSE(downloading_files.is_completed());
    auto files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 1);
    downloading_files.complete_file(files_to_download[0]);
    EXPECT_FALSE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 1);
    downloading_files.complete_file(files_to_download[0]);
    EXPECT_FALSE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 1);
    downloading_files.complete_file(files_to_download[0]);
    EXPECT_TRUE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 0);
}

TEST(downloading_files_test, two_file_size) {
    const auto torrent_file = get_asset("test.torrent");
    lt::torrent_info ti(torrent_file);
    std::vector<std::string> new_files;
    unsigned long max_size = 0;
    for (const auto &file_index: ti.files().file_range()) {
        const auto file_name = ti.files().file_path(file_index);
        new_files.push_back(file_name);
        const auto file_size = ti.files().file_size(file_index);
        if (file_size > max_size) {
            max_size = file_size;
        }
    }
    EXPECT_EQ(new_files.size(), 3);
    // limit to largest file bytes - should result to largest file downloaded first
    DownloadingFiles downloading_files(ti, new_files, max_size + 1);
    EXPECT_FALSE(downloading_files.is_completed());
    auto files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 1);
    downloading_files.complete_file(files_to_download[0]);
    EXPECT_FALSE(downloading_files.is_completed());
    // two smaller files should go next
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 2);
    downloading_files.complete_file(files_to_download[0]);
    EXPECT_FALSE(downloading_files.is_completed());
    downloading_files.complete_file(files_to_download[1]);
    EXPECT_TRUE(downloading_files.is_completed());
    files_to_download = downloading_files.download_next_chunk();
    EXPECT_EQ(files_to_download.size(), 0);
}
