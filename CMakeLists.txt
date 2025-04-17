cmake_minimum_required(VERSION 3.17)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(torrent-s3 VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 17)

MESSAGE(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(GTest CONFIG REQUIRED)
find_package(LibtorrentRasterbar REQUIRED)
find_package(CURL REQUIRED)
find_package(miniocpp REQUIRED)
find_package(LibArchive REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

list(APPEND APP_LIBS ${CURL_LIBRARIES} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive unofficial::sqlite3::sqlite3)
list(APPEND APP_INCLUDES ${CURL_INCLUDE_DIRS} LibtorrentRasterbar::torrent-rasterbar miniocpp::miniocpp LibArchive::LibArchive unofficial::sqlite3::sqlite3)

add_library (${PROJECT_NAME}_objects OBJECT
    src/torrent/torrent_download.cpp
    src/hashlist/hashlist.cpp
    src/s3/s3.cpp src/curl/curl.cpp
    src/archive/archive.cpp
    src/linked_files/linked_files.cpp
    src/downloading_files/downloading_files.cpp
    src/path/path_utils.cpp
    src/db/sqlite.cpp
    src/app_state/state.cpp
    src/app_sync/sync.cpp
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
    test/downloading_files_test.cpp
    test/linked_files_test.cpp
    test/app_state_test.cpp
    test/app_sync_test.cpp
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

add_custom_target(format
  astyle --suffix=none --recursive "./src/*.cpp" "./src/*.hpp" "./test/*.cpp" "./test/*.hpp"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Source code formatting with Astyle"
  VERBATIM
)
