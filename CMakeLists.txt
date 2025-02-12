cmake_minimum_required(VERSION 3.16.3)

project(torrent-s3 VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)

MESSAGE(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(GTest CONFIG REQUIRED)
find_package(LibtorrentRasterbar REQUIRED)
find_package(CURL REQUIRED)
find_package(miniocpp REQUIRED)
find_package(LibArchive REQUIRED)

add_library (${PROJECT_NAME}_objects OBJECT
    src/torrent/torrent_download.cpp
    src/hashlist/hashlist.cpp
    src/s3/s3.cpp src/curl/curl.cpp
    src/archive/archive.cpp
    src/linked_files/linked_files.cpp
    src/downloading_files/downloading_files.cpp
)
target_include_directories(${PROJECT_NAME}_objects PRIVATE ${CURL_INCLUDE_DIRS} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)
target_link_libraries(${PROJECT_NAME}_objects PRIVATE ${CURL_LIBRARIES} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)

add_executable (${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_objects ${CURL_LIBRARIES} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)

add_definitions(-DCMAKE_PROJECT_NAME=${CMAKE_PROJECT_NAME})
add_definitions(-DCMAKE_PROJECT_VERSION=${CMAKE_PROJECT_VERSION})
add_definitions(-DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR})

enable_testing()

add_executable(
    ${PROJECT_NAME}-test
    test/archive_test.cpp
)

target_include_directories(${PROJECT_NAME}-test PRIVATE ${CURL_INCLUDE_DIRS} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)
target_link_libraries(
    ${PROJECT_NAME}-test
    PRIVATE
    ${PROJECT_NAME}_objects
    GTest::gtest_main
    ${CURL_LIBRARIES} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive
)

include(GoogleTest)
gtest_discover_tests(torrent-s3-test)
