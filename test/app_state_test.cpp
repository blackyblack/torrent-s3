#include <gtest/gtest.h>

#include "../src/db/sqlite.hpp"
#include "../src/app_state/state.hpp"

TEST(app_state_test, basic_check) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto files = state.get_linked_files();
    EXPECT_EQ(files.size(), 0);
    const auto uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 0);
    state.add_files("parent", {});
    const auto new_files = state.get_linked_files();
    EXPECT_EQ(new_files.size(), 1);
    EXPECT_EQ(new_files.at("parent").size(), 0);
    // uploading files have not changed
    const auto new_uploading_files = state.get_uploading_files();
    EXPECT_EQ(new_uploading_files.size(), 0);
    EXPECT_EQ(state.get_file_status("parent"), file_status_t::FILE_STATUS_WAITING);
    EXPECT_EQ(state.get_file_status("child"), std::nullopt);
}

TEST(app_state_test, add_child) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto files = state.get_linked_files();
    EXPECT_EQ(files.size(), 0);
    EXPECT_EQ(state.get_parent("child"), std::nullopt);
    state.add_files("parent", {"child"});
    const auto new_files = state.get_linked_files();
    EXPECT_EQ(new_files.size(), 1);
    EXPECT_EQ(new_files.at("parent").size(), 1);
    EXPECT_EQ(new_files.at("parent")[0], "child");
    EXPECT_EQ(state.get_parent("child"), "parent");
}

TEST(app_state_test, mark_download_upload_complete) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    const auto files = state.get_linked_files();
    EXPECT_EQ(files.size(), 0);
    state.add_files("parent", {"child"});
    const auto new_files = state.get_linked_files();
    EXPECT_EQ(new_files.size(), 1);
    EXPECT_EQ(new_files.at("parent").size(), 1);
    EXPECT_EQ(new_files.at("parent")[0], "child");
    auto uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 0);
    EXPECT_EQ(state.get_file_status("child"), file_status_t::FILE_STATUS_WAITING);
    // marking as 'download' does not change uploading files
    state.file_download("child");
    uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 0);
    EXPECT_EQ(state.get_file_status("child"), file_status_t::FILE_STATUS_DOWNLOADING);
    state.file_upload("child");
    uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 1);
    EXPECT_EQ(uploading_files.at("parent").size(), 1);
    EXPECT_EQ(uploading_files.at("parent")[0], "child");
    EXPECT_EQ(state.get_file_status("child"), file_status_t::FILE_STATUS_UPLOADING);
    state.file_complete("child");
    uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 0);
    EXPECT_EQ(state.get_file_status("child"), file_status_t::FILE_STATUS_READY);
    // parent is not a file, so marking it as uploaded does not affect anything
    state.file_upload("parent");
    uploading_files = state.get_uploading_files();
    EXPECT_EQ(uploading_files.size(), 0);
    EXPECT_EQ(state.get_file_status("parent"), std::nullopt);
}

TEST(app_state_test, remove_children) {
    const auto maybe_db = db_open(":memory:");
    const auto db = std::get<std::shared_ptr<sqlite3>>(maybe_db);
    AppState state(db, true);
    auto files = state.get_linked_files();
    EXPECT_EQ(files.size(), 0);
    state.add_files("parent", {"child1", "child2"});
    files = state.get_linked_files();
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(files.at("parent").size(), 2);
    // parent is not a file so removing it does not affect anything
    state.remove_file("parent");
    files = state.get_linked_files();
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(files.at("parent").size(), 2);
    state.remove_file("child1");
    files = state.get_linked_files();
    EXPECT_EQ(files.size(), 1);
    EXPECT_EQ(files.at("parent").size(), 1);
    EXPECT_EQ(files.at("parent")[0], "child2");
    state.remove_file("child2");
    files = state.get_linked_files();
    EXPECT_EQ(files.size(), 0);
}
