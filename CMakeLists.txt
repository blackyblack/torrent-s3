cmake_minimum_required(VERSION 3.16.3)

project(torrent-s3 VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)

MESSAGE(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(LibtorrentRasterbar REQUIRED)
find_package(CURL REQUIRED)
find_package(miniocpp REQUIRED)
find_package(LibArchive REQUIRED)

add_executable (torrent-s3 src/main.cpp src/torrent/torrent_download.cpp src/hashlist/hashlist.cpp src/s3/s3.cpp src/curl/curl.cpp src/archive/archive.cpp)

target_include_directories(torrent-s3 PRIVATE ${CURL_INCLUDE_DIRS} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)
target_link_libraries(torrent-s3 PRIVATE ${CURL_LIBRARIES} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)

add_definitions(-DCMAKE_PROJECT_NAME=${CMAKE_PROJECT_NAME})
add_definitions(-DCMAKE_PROJECT_VERSION=${CMAKE_PROJECT_VERSION})
