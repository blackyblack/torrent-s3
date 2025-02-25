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

list(APPEND APP_LIBS ${CURL_LIBRARIES} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)
list(APPEND APP_INCLUDES ${CURL_INCLUDE_DIRS} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive)

add_library (${PROJECT_NAME}_objects OBJECT
    src/torrent/torrent_download.cpp
    src/hashlist/hashlist.cpp
    src/s3/s3.cpp src/curl/curl.cpp
    src/archive/archive.cpp
    src/linked_files/linked_files.cpp
    src/downloading_files/downloading_files.cpp
)
target_include_directories(${PROJECT_NAME}_objects PRIVATE ${APP_INCLUDES})
target_link_libraries(${PROJECT_NAME}_objects PRIVATE ${APP_LIBS})

add_executable (${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${APP_INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_objects ${APP_LIBS})

add_definitions(-DCMAKE_PROJECT_NAME=${CMAKE_PROJECT_NAME})
add_definitions(-DCMAKE_PROJECT_VERSION=${CMAKE_PROJECT_VERSION})
add_definitions(-DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR})

enable_testing()

add_executable(
    ${PROJECT_NAME}-test
    test/test_utils.cpp
    test/archive_test.cpp
    test/curl_test.cpp
    test/torrent_test.cpp
    test/s3_test.cpp
)

target_include_directories(${PROJECT_NAME}-test PRIVATE ${APP_INCLUDES})
target_link_libraries(
    ${PROJECT_NAME}-test
    PRIVATE
    ${PROJECT_NAME}_objects
    GTest::gtest_main
    ${APP_LIBS}
)

include(GoogleTest)
gtest_discover_tests(torrent-s3-test)
