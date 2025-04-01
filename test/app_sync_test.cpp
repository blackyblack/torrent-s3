#include <gtest/gtest.h>

#include "../src/db/sqlite.hpp"
#include "../src/app_sync/sync.hpp"
#include "test_utils.hpp"

TEST(app_sync_test, basic_check) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    auto app_state = std::make_shared<AppState>(db, true);
    const auto download_path = (std::filesystem::path(get_tmp_dir()) / "download").string();
    auto s3_uploader = std::make_shared<S3Uploader>(0, "play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", download_path, "upload");
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = download_path;
    const auto path = std::filesystem::canonical(get_asset("starwars.torrent"));
    torrent_params.ti = std::make_shared<lt::torrent_info>(path.string());
    auto torrent_downloader = std::make_shared<TorrentDownloader>(torrent_params);
    // in this test we start with empty hashlist
    file_hashlist_t hashlist;
    const auto ti = torrent_downloader->get_torrent_info();
    const auto new_files_set = get_updated_files(hashlist, *torrent_params.ti);
    const auto test_file_name = "Star Wars books\\jq_07_tmot.jpg";
    for (const auto &f : new_files_set) {
        app_state->add_uploading_files(f, {});
        if (f == test_file_name) {
            continue;
        }
        app_state->file_complete(f);
    }
    EXPECT_EQ(s3_uploader->delete_file(test_file_name), std::nullopt);
    EXPECT_FALSE(std::get<bool>(s3_uploader->is_file_existing(test_file_name)));

    AppSync app_sync(
        app_state,
        s3_uploader,
        torrent_downloader,
        hashlist,
        LLONG_MAX,
        download_path,
        false
    );
    const auto sync_ret = app_sync.full_sync();
    const auto file_errors = std::get<std::vector<file_upload_error_t>>(sync_ret);
    EXPECT_EQ(file_errors.size(), 0);
    EXPECT_TRUE(std::get<bool>(s3_uploader->is_file_existing(test_file_name)));
}

TEST(app_sync_test, restore_state) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    auto app_state = std::make_shared<AppState>(db, true);
    const auto download_path = (std::filesystem::path(get_tmp_dir()) / "download").string();
    auto s3_uploader = std::make_shared<S3Uploader>(0, "play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", download_path, "upload");
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = download_path;
    const auto path = std::filesystem::canonical(get_asset("starwars.torrent"));
    torrent_params.ti = std::make_shared<lt::torrent_info>(path.string());
    auto torrent_downloader = std::make_shared<TorrentDownloader>(torrent_params);
    // in this test we start with empty hashlist
    file_hashlist_t hashlist;
    const auto ti = torrent_downloader->get_torrent_info();
    const auto new_files_set = get_updated_files(hashlist, *torrent_params.ti);
    const auto test_file_names = std::unordered_set<std::string>({"Star Wars books\\jq_07_tmot.jpg", "Star Wars books\\bhw_2_ss.jpg"});
    for (const auto &f : new_files_set) {
        app_state->add_uploading_files(f, {});
        if (test_file_names.count(f) > 0) {
            continue;
        }
        app_state->file_complete(f);
    }
    for (const auto &f : test_file_names) {
        EXPECT_EQ(s3_uploader->delete_file(f), std::nullopt);
        EXPECT_FALSE(std::get<bool>(s3_uploader->is_file_existing(f)));
    }

    AppSync app_sync(
        app_state,
        s3_uploader,
        torrent_downloader,
        hashlist,
        LLONG_MAX,
        download_path,
        false
    );
    const auto sync_start_ret = app_sync.start();
    EXPECT_EQ(sync_start_ret, std::nullopt);

    auto &download_progress = torrent_downloader->get_progress_queue();
    auto &upload_progress = s3_uploader->get_progress_queue();
    // upload only one file and stop syncing. Try to restore sync from the state.
    bool file_uploaded = false;
    std::string uploaded_file_name;
    while(true) {
        if (app_sync.is_completed()) break;
        if (file_uploaded) break;
        while (!download_progress.empty()) {
            const auto torrent_event = download_progress.pop_front_waiting();
            const auto torrent_file_downloaded = std::get<TorrentProgressDownloadOk>(torrent_event);
            app_sync.process_torrent_file(torrent_file_downloaded.file_name);
            continue;
        }
        while (!upload_progress.empty()) {
            const auto s3_event = upload_progress.pop_front_waiting();
            const auto s3_file_uploaded = std::get<S3ProgressUploadOk>(s3_event);
            app_sync.process_s3_file(s3_file_uploaded.file_name);
            file_uploaded = true;
            uploaded_file_name = s3_file_uploaded.file_name;
            break;
        }
    }
    auto file_errors = app_sync.stop();
    EXPECT_EQ(file_errors.size(), 0);
    EXPECT_TRUE(std::get<bool>(s3_uploader->is_file_existing(uploaded_file_name)));

    s3_uploader = std::make_shared<S3Uploader>(0, "play.min.io", "Q3AM3UQ867SPQQA43P2F", "zuf+tfteSlswRu7BJ86wekitnifILbZam1KYY3TG", "test", "", download_path, "upload");
    lt::add_torrent_params torrent_params_restored;
    torrent_params_restored.save_path = download_path;
    torrent_params_restored.ti = std::make_shared<lt::torrent_info>(path.string());
    torrent_downloader = std::make_shared<TorrentDownloader>(torrent_params_restored);
    // in this test we start with empty hashlist
    file_hashlist_t hashlist_restored;

    AppSync app_sync_restored(
        app_state,
        s3_uploader,
        torrent_downloader,
        hashlist_restored,
        LLONG_MAX,
        download_path,
        false
    );

    const auto sync_ret = app_sync_restored.full_sync();
    file_errors = std::get<std::vector<file_upload_error_t>>(sync_ret);
    EXPECT_EQ(file_errors.size(), 0);

    for (const auto &f : test_file_names) {
        EXPECT_TRUE(std::get<bool>(s3_uploader->is_file_existing(f)));
    }
}
