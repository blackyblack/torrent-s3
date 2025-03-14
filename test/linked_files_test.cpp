#include <gtest/gtest.h>

#include "../src/linked_files/linked_files.hpp"

TEST(linked_files_test, basic_check) {
    LinkedFiles files;
    EXPECT_EQ(files.get_files().size(), 0);
    EXPECT_EQ(files.get_parent("child"), std::nullopt);
    // nothing happens
    files.remove_child("child");
    files.remove_parent("parent");
}

TEST(linked_files_test, add_parent) {
    LinkedFiles files;
    EXPECT_EQ(files.get_files().size(), 0);
    files.add_files("parent", {});
    EXPECT_EQ(files.get_parent("parent"), std::nullopt);
    const auto linked_files = files.get_files();
    EXPECT_EQ(linked_files.size(), 1);
    EXPECT_EQ(linked_files.at("parent").size(), 0);
    // nothing happens
    files.remove_child("parent");
    EXPECT_EQ(files.get_files().size(), 1);
    files.remove_parent("parent");
    EXPECT_EQ(files.get_files().size(), 0);
}

TEST(linked_files_test, add_child) {
    LinkedFiles files;
    EXPECT_EQ(files.get_files().size(), 0);
    files.add_files("parent", {"child"});
    EXPECT_EQ(files.get_parent("parent"), std::nullopt);
    EXPECT_EQ(files.get_parent("child"), "parent");
    const auto linked_files = files.get_files();
    EXPECT_EQ(linked_files.size(), 1);
    EXPECT_EQ(linked_files.at("parent").size(), 1);
    EXPECT_EQ(linked_files.at("parent")[0], "child");
    files.remove_child("child");
    EXPECT_EQ(files.get_parent("child"), std::nullopt);
    EXPECT_EQ(files.get_files().size(), 0);
    files.remove_parent("parent");
    EXPECT_EQ(files.get_files().size(), 0);
}

TEST(linked_files_test, remove_children) {
    LinkedFiles files;
    EXPECT_EQ(files.get_files().size(), 0);
    files.add_files("parent", {"child1", "child2"});
    EXPECT_EQ(files.get_parent("child1"), "parent");
    EXPECT_EQ(files.get_parent("child2"), "parent");
    const auto linked_files = files.get_files();
    EXPECT_EQ(linked_files.size(), 1);
    EXPECT_EQ(linked_files.at("parent").size(), 2);
    EXPECT_EQ(linked_files.at("parent")[0], "child1");
    EXPECT_EQ(linked_files.at("parent")[1], "child2");
    files.remove_parent("parent");
    EXPECT_EQ(files.get_files().size(), 0);
    EXPECT_EQ(files.get_parent("child1"), std::nullopt);
    EXPECT_EQ(files.get_parent("child2"), std::nullopt);
}
