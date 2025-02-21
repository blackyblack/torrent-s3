#include <filesystem>
#include <gtest/gtest.h>

#include "./test_utils.hpp"

#include "../src/torrent/torrent_download.hpp"

TEST(torrent_test, start_stop) {
    const auto torrent_file = get_asset("alice.torrent");
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = get_tmp_dir();
    torrent_params.ti = std::make_shared<lt::torrent_info>(torrent_file);
    EXPECT_EQ(torrent_params.ti->num_files(), 1);
    // it won't download unless specifically told so with download_files() method
    TorrentDownloader downloader(torrent_params);
    downloader.start();
    downloader.stop();
}

TEST(torrent_test, no_files) {
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = get_tmp_dir();
    // dies with SEH exception which is not caught by the EXPECT_THROW call
    EXPECT_DEATH(TorrentDownloader downloader(torrent_params), "");
}


TEST(torrent_test, magnet_link) {
    const auto torrent_info = load_magnet_link_info("magnet:?xt=urn:btih:01FF5A2C8261D32B2F83007ECA4C5A94EFA66EC3");
    EXPECT_EQ(torrent_info.num_files(), 15);
}

TEST(torrent_test, download_files) {
    const auto torrent_file = get_asset("test.torrent");
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = get_tmp_dir();
    torrent_params.ti = std::make_shared<lt::torrent_info>(torrent_file);
    EXPECT_EQ(torrent_params.ti->num_files(), 3);
    const auto to_download = torrent_params.ti->files().file_path(lt::file_index_t {2});
    const auto to_download_path = std::filesystem::path(to_download);
    EXPECT_EQ(to_download_path.filename(), "README");
    TorrentDownloader downloader(torrent_params);
    auto &progress_queue = downloader.get_progress_queue();
    EXPECT_TRUE(progress_queue.empty());
    downloader.start();
    downloader.download_files({to_download});
    downloader.stop();
    EXPECT_FALSE(progress_queue.empty());
    const auto torrent_event = progress_queue.pop_front_waiting();
    const auto &download_ok = std::get<TorrentProgressDownloadOk>(torrent_event);
    EXPECT_EQ(download_ok.file_name, to_download);
    EXPECT_EQ(download_ok.file_index, 2);
    EXPECT_TRUE(progress_queue.empty());
    const auto filename = std::filesystem::path(get_tmp_dir()) / "test_folder" / "README";
    EXPECT_TRUE(std::filesystem::exists(filename));
    std::filesystem::remove_all(get_tmp_dir());
}

// This torrent assumes README download is completed while it's download has not been requested.
// Make sure this case is handled correctly.
TEST(torrent_test, download_files_overlapping_pieces) {
    const auto torrent_file = get_asset("test.torrent");
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = get_tmp_dir();
    torrent_params.ti = std::make_shared<lt::torrent_info>(torrent_file);
    EXPECT_EQ(torrent_params.ti->num_files(), 3);
    const auto to_download = torrent_params.ti->files().file_path(lt::file_index_t {1});
    const auto to_download_path = std::filesystem::path(to_download);
    EXPECT_EQ(to_download_path.filename(), "melk-abbey-library.jpg");
    TorrentDownloader downloader(torrent_params);
    auto &progress_queue = downloader.get_progress_queue();
    EXPECT_TRUE(progress_queue.empty());
    downloader.start();
    downloader.download_files({to_download});
    downloader.stop();
    EXPECT_FALSE(progress_queue.empty());
    const auto torrent_event = progress_queue.pop_front_waiting();
    const auto &download_ok = std::get<TorrentProgressDownloadOk>(torrent_event);
    EXPECT_EQ(download_ok.file_name, to_download);
    EXPECT_EQ(download_ok.file_index, 1);
    EXPECT_TRUE(progress_queue.empty());
    const auto filename = std::filesystem::path(get_tmp_dir()) / "test_folder" / "images" / "melk-abbey-library.jpg";
    EXPECT_TRUE(std::filesystem::exists(filename));
    std::filesystem::remove_all(get_tmp_dir());
}

// Make sure that the file can still be downloaded after it was marked as overlapped.
TEST(torrent_test, continue_after_overlapping_pieces) {
    const auto torrent_file = get_asset("test.torrent");
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = get_tmp_dir();
    torrent_params.ti = std::make_shared<lt::torrent_info>(torrent_file);
    EXPECT_EQ(torrent_params.ti->num_files(), 3);
    const auto to_download = torrent_params.ti->files().file_path(lt::file_index_t {1});
    const auto to_download_path = std::filesystem::path(to_download);
    EXPECT_EQ(to_download_path.filename(), "melk-abbey-library.jpg");
    TorrentDownloader downloader(torrent_params);
    auto &progress_queue = downloader.get_progress_queue();
    EXPECT_TRUE(progress_queue.empty());
    downloader.start();
    downloader.download_files({to_download});
    const auto torrent_event = progress_queue.pop_front_waiting();
    const auto &download_ok = std::get<TorrentProgressDownloadOk>(torrent_event);
    EXPECT_EQ(download_ok.file_name, to_download);
    EXPECT_EQ(download_ok.file_index, 1);
    const auto filename = std::filesystem::path(get_tmp_dir()) / "test_folder" / "images" / "melk-abbey-library.jpg";
    EXPECT_TRUE(std::filesystem::exists(filename));
    EXPECT_TRUE(progress_queue.empty());
    const auto to_download_next = torrent_params.ti->files().file_path(lt::file_index_t {2});
    const auto to_download_next_path = std::filesystem::path(to_download_next);
    EXPECT_EQ(to_download_next_path.filename(), "README");
    downloader.download_files({to_download_next});
    downloader.stop();
    EXPECT_FALSE(progress_queue.empty());

    const auto torrent_event_next = progress_queue.pop_front_waiting();
    const auto &download_ok_next = std::get<TorrentProgressDownloadOk>(torrent_event_next);
    EXPECT_EQ(download_ok_next.file_name, to_download_next);
    EXPECT_EQ(download_ok_next.file_index, 2);
    const auto filename_next = std::filesystem::path(get_tmp_dir()) / "test_folder" / "README";
    EXPECT_TRUE(std::filesystem::exists(filename_next));

    std::filesystem::remove_all(get_tmp_dir());
}
