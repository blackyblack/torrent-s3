#include <gtest/gtest.h>

#include "../src/curl/curl.hpp"

TEST(curl_test, get_torrent) {
    const auto ret = download_torrent_info("https://webtorrent.io/torrents/sintel.torrent");
    EXPECT_TRUE(std::holds_alternative<lt::torrent_info>(ret));
    const auto torrent = std::get<lt::torrent_info>(ret);
    EXPECT_TRUE(torrent.is_valid());
    EXPECT_EQ(torrent.num_files(), 11);
}

TEST(curl_test, get_not_existing_torrent) {
    const auto ret = download_torrent_info("https://webtorrent.io/torrents/doesnotexist.torrent");
    EXPECT_TRUE(std::holds_alternative<std::string>(ret));
}

TEST(curl_test, get_not_torrent) {
    const auto ret = download_torrent_info("https://github.com/webtorrent/webtorrent-fixtures/blob/master/fixtures/alice.txt");
    EXPECT_TRUE(std::holds_alternative<std::string>(ret));
}

TEST(curl_test, bad_url) {
    const auto ret = download_torrent_info("https://doesnotexist123doesnotexist.io/doesnotexist.torrent");
    EXPECT_TRUE(std::holds_alternative<std::string>(ret));
}
