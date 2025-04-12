#include <filesystem>
#include <gtest/gtest.h>

#include "../src/db/sqlite.hpp"
#include "../src/app_state/state.hpp"
#include "../src/torrent/torrent_download.hpp"
#include "test_utils.hpp"

TEST(app_state_test, basic_check) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto files = state.get_uploading_files();
    EXPECT_EQ(files.size(), 0);
    const auto uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 0);
    state.add_uploading_files("parent", {});
    const auto new_files = state.get_uploading_files();
    EXPECT_EQ(new_files.size(), 1);
    EXPECT_EQ(new_files.at("parent").size(), 0);
    EXPECT_EQ(state.get_file_status("parent"), file_status_t::FILE_STATUS_UPLOADING);
    EXPECT_EQ(state.get_file_status("child"), std::nullopt);
    const auto completed_files = state.get_completed_files();
    EXPECT_EQ(completed_files.size(), 0);
}

TEST(app_state_test, add_child) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto files = state.get_uploading_files();
    EXPECT_EQ(files.size(), 0);
    EXPECT_EQ(state.get_uploading_parent("child"), std::nullopt);
    state.add_uploading_files("parent", {"child"});
    const auto new_files = state.get_uploading_files();
    EXPECT_EQ(new_files.size(), 1);
    EXPECT_EQ(new_files.at("parent").size(), 1);
    EXPECT_EQ(new_files.at("parent")[0], "child");
    EXPECT_EQ(state.get_uploading_parent("child"), "parent");
}

TEST(app_state_test, mark_complete) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto files = state.get_uploading_files();
    EXPECT_EQ(files.size(), 0);
    state.add_uploading_files("parent", {"child"});
    auto new_files = state.get_uploading_files();
    EXPECT_EQ(new_files.size(), 1);
    EXPECT_EQ(new_files.at("parent").size(), 1);
    EXPECT_EQ(new_files.at("parent")[0], "child");
    EXPECT_EQ(state.get_file_status("child"), file_status_t::FILE_STATUS_UPLOADING);
    state.file_complete("child");
    new_files = state.get_uploading_files();
    EXPECT_EQ(new_files.size(), 0);
    EXPECT_EQ(state.get_file_status("child"), file_status_t::FILE_STATUS_READY);
    auto completed_files = state.get_completed_files();
    EXPECT_EQ(completed_files.size(), 1);
    EXPECT_EQ(completed_files.at("parent").size(), 1);
    EXPECT_EQ(completed_files.at("parent")[0], "child");
    // parent is not a file, so marking it as uploaded does not affect anything
    state.file_complete("parent");
    new_files = state.get_uploading_files();
    EXPECT_EQ(new_files.size(), 0);
    EXPECT_EQ(state.get_file_status("parent"), std::nullopt);
    completed_files = state.get_completed_files();
    EXPECT_EQ(completed_files.size(), 1);
    EXPECT_EQ(completed_files.at("parent").size(), 1);
}

TEST(app_state_test, hashlist_save_load) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto empty_hashlist = state.get_hashlist();
    EXPECT_EQ(empty_hashlist.size(), 0);
    state.save_hashlist({{"file1", {{"hash1", "hash2"}, {"file3"}}}, {"file2", {{"hash3"}, {}}}});
    const auto hashlist = state.get_hashlist();
    EXPECT_EQ(hashlist.size(), 2);
    EXPECT_EQ(hashlist.at("file1").hashes.size(), 2);
    EXPECT_EQ(hashlist.at("file1").linked_files.size(), 1);
}

TEST(app_state_test, torrent_save_load) {
    const auto path = std::filesystem::canonical(get_asset("starwars.torrent"));
    lt::add_torrent_params torrent_params;
    torrent_params.save_path = get_tmp_dir();
    torrent_params.ti = std::make_shared<lt::torrent_info>(path.string());

    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto empty_hashlist = state.get_hashlist();
    EXPECT_EQ(empty_hashlist.size(), 0);

    for (const auto &i : torrent_params.ti->files().file_range()) {
        const auto f = torrent_params.ti->files().file_path(i);
        state.file_complete(f);
    }
    auto new_hashlist = create_hashlist(*torrent_params.ti, state.get_completed_files());
    state.save_hashlist(new_hashlist);

    auto hashlist = state.get_hashlist();
    EXPECT_TRUE(hashlist.size() > 0);

    const auto new_files_set = get_updated_files(*torrent_params.ti, hashlist);
    EXPECT_EQ(new_files_set.size(), 0);
}
