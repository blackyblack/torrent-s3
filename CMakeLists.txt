cmake_minimum_required(VERSION 3.16.3)

project(torrent-s3)

set(CMAKE_CXX_STANDARD 14)

MESSAGE(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(LibtorrentRasterbar REQUIRED)
add_executable (torrent-s3 src/main.cpp src/torrent/torrent_download.cpp src/hashlist/hashlist.cpp src/s3/s3.cpp src/path_utils/path_utils.cpp)

target_include_directories(torrent-s3 PRIVATE ${LibtorrentRasterbar_INCLUDE_DIR})
target_link_libraries(torrent-s3 PRIVATE LibtorrentRasterbar::torrent-rasterbar)