#include <gtest/gtest.h>

#include "../src/curl/curl.hpp"

TEST(curl_test, get_torrent) {
    const auto torrent = download_torrent_info("https://webtorrent.io/torrents/sintel.torrent");
    EXPECT_TRUE(torrent.is_valid());
    EXPECT_EQ(torrent.num_files(), 11);
}

TEST(curl_test, get_not_existing_torrent) {
    EXPECT_THROW(download_torrent_info("https://webtorrent.io/torrents/doesnotexist.torrent"), ParseError);
}

TEST(curl_test, get_not_torrent) {
    EXPECT_THROW(download_torrent_info("https://github.com/webtorrent/webtorrent-fixtures/blob/master/fixtures/alice.txt"), ParseError);
}

TEST(curl_test, bad_url) {
    EXPECT_THROW(download_torrent_info("https://doesnotexist123doesnotexist.io/doesnotexist.torrent"), DownloadError);
}
