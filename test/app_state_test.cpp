#include <gtest/gtest.h>

#include "../src/db/sqlite.hpp"
#include "../src/app_state/state.hpp"

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
