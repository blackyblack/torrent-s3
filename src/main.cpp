#include <iostream>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <fstream>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <sqlite3.h>

#include "./deque/deque.hpp"
#include "./torrent/torrent_download.hpp"
#include "./hashlist/hashlist.hpp"
#include "./command_line/cxxopts.hpp"
#include "./s3/s3.hpp"
#include "./curl/curl.hpp"
#include "./archive/archive.hpp"
#include "./linked_files/linked_files.hpp"
#include "./downloading_files/downloading_files.hpp"
#include "./path/path_utils.hpp"
#include "./db/sqlite.hpp"
#include "./app_state/state.hpp"
#include "./app_sync/sync.hpp"

#define STRING(x) #x
#define XSTRING(x) STRING(x)

#define APP_NAME XSTRING(CMAKE_PROJECT_NAME)
#define APP_VERSION XSTRING(CMAKE_PROJECT_VERSION)

#define FILE_HASHES_STORAGE_NAME ".torrent_s3_hashlist"

static void print_usage(const cxxopts::Options &options) {
    fprintf(stderr, "%s", options.help().c_str());
}

static bool is_http_url(const std::string &url) {
    std::regex url_regex(R"(^(https?:\/\/)?([\da-z\.-]+)\.([a-z\.]{2,6})([\/\w \.-]*)*\/?$)");
    return std::regex_match(url, url_regex);
}

int main(int argc, char const* argv[]) {
    cxxopts::Options options(APP_NAME);

    // NOTE: non-unicode and non-ascii symbols are not supported in command line arguments.
    // Windows might use other codepage by default, so make sure to use english characters in paths.
    options.add_options()
           ("t,torrent", "Torrent file path, HTTP URL or magnet link", cxxopts::value<std::string>())
           ("s,s3-url", "S3 service URL", cxxopts::value<std::string>())
           ("b,s3-bucket", "S3 bucket", cxxopts::value<std::string>())
           ("r,s3-region", "S3 region", cxxopts::value<std::string>())
           ("u,s3-upload-path", "S3 path to store uploaded files", cxxopts::value<std::string>())
           ("a,s3-access-key", "S3 access key", cxxopts::value<std::string>())
           ("k,s3-secret-key", "S3 secret key", cxxopts::value<std::string>())
           ("d,download-path", "Temporary directory for downloaded files", cxxopts::value<std::string>())
           ("l,limit-size", "Temporary directory maximum size in bytes", cxxopts::value<unsigned long long>())
           ("p,hashlist-file", std::string("Path to hashlist. Default is <download-path>/") + std::string(FILE_HASHES_STORAGE_NAME), cxxopts::value<std::string>())
           ("z,extract-files", "Extract downloaded archives before uploading")
           ("v,version", "Show version")
           ("h,help", "Show help");

    cxxopts::ParseResult args;

    try {
        args = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception &x) {
        fprintf(stderr, "%s: %s\n", APP_NAME, x.what());
        print_usage(options);
        return EXIT_FAILURE;
    }

    if (args.count("help")) {
        print_usage(options);
        return EXIT_SUCCESS;
    }

    if (args.count("version")) {
        fprintf(stderr, "%s: %s\n", APP_NAME, APP_VERSION);
        return EXIT_SUCCESS;
    }

    if (!args.count("torrent")) {
        fprintf(stderr, "Torrent file is not set.\n");
        print_usage(options);
        return EXIT_FAILURE;
    }
    const auto torrent_url = args["torrent"].as<std::string>();
    std::string download_path = ".";
    if (args.count("download-path")) {
        download_path = args["download-path"].as<std::string>();
    }
    unsigned long long limit_size_bytes = LLONG_MAX;
    if (args.count("limit-size")) {
        limit_size_bytes = args["limit-size"].as<unsigned long long>();
    }
    auto hashlist_path = std::filesystem::path(download_path) / std::filesystem::path(FILE_HASHES_STORAGE_NAME);
    if (args.count("hashlist-file")) {
        hashlist_path = args["hashlist-file"].as<std::string>();
    }

    bool use_magnet = false;
    bool use_url = false;

    std::string what = std::string("file \"") + torrent_url + ("\"");
    {
        lt::error_code error_code;
        lt::parse_magnet_uri(torrent_url, error_code);
        if (!error_code.failed()) {
            use_magnet = true;
            what = std::string("magnet link \"") + torrent_url + ("\"");
        }
    }

    if (!use_magnet && is_http_url(torrent_url)) {
        use_url = true;
        what = std::string("url \"") + torrent_url + ("\"");
    }

    if (!use_magnet && !use_url) {
        std::filesystem::path fs_path(torrent_url);
        bool exists = std::filesystem::exists(fs_path);
        if (!exists) {
            fprintf(stderr, "Torrent file is not found at %s.\n", torrent_url.c_str());
            return EXIT_FAILURE;
        }
    }

    if (!args.count("s3-url")) {
        fprintf(stderr, "S3 URL is not set.\n");
        print_usage(options);
        return EXIT_FAILURE;
    }

    if (!args.count("s3-bucket")) {
        fprintf(stderr, "S3 bucket is not set.\n");
        print_usage(options);
        return EXIT_FAILURE;
    }

    if (!args.count("s3-access-key")) {
        fprintf(stderr, "S3 access key is not set.\n");
        print_usage(options);
        return EXIT_FAILURE;
    }

    if (!args.count("s3-secret-key")) {
        fprintf(stderr, "S3 secret key is not set.\n");
        print_usage(options);
        return EXIT_FAILURE;
    }

    const auto s3_url = args["s3-url"].as<std::string>();
    const auto s3_bucket = args["s3-bucket"].as<std::string>();
    const auto s3_access_key = args["s3-access-key"].as<std::string>();
    const auto s3_secret_key = args["s3-secret-key"].as<std::string>();

    std::string upload_path = "";
    if (args.count("s3-upload-path")) {
        upload_path = args["s3-upload-path"].as<std::string>();
    }

    std::string s3_region = "";
    if (args.count("s3-region")) {
        s3_region = args["s3-region"].as<std::string>();
    }

    const auto extract_files = args.count("extract-files") > 0;

    fprintf(stdout, "Torrent-S3 starting\n");

    if (limit_size_bytes == LLONG_MAX) {
        fprintf(stdout, "Downloading from %s to temporary directory \"%s\" without size limit\n", what.c_str(), download_path.c_str());
    } else {
        fprintf(stdout, "Downloading from %s to temporary directory \"%s\" with size limit %.3f MB\n", what.c_str(), download_path.c_str(), ((double) limit_size_bytes) / 1024 / 1024);
    }

    const auto db_open_ret = db_open(":memory:");
    if (std::holds_alternative<std::string>(db_open_ret)) {
        fprintf(stderr, "Failed to open SQLite database: %s\n", std::get<std::string>(db_open_ret).c_str());
        return EXIT_FAILURE;
    }
    const auto db = std::get<std::shared_ptr<sqlite3>>(db_open_ret);

    lt::add_torrent_params torrent_params;
    torrent_params.save_path = download_path;

    try {
        if (use_magnet) {
            fprintf(stdout, "Loading magnet link metadata\n");
            const auto magnet_link_ret = load_magnet_link_info(torrent_url);
            if (std::holds_alternative<std::string>(magnet_link_ret)) {
                fprintf(stderr, "Failed to load magnet link metadata: %s\n", std::get<std::string>(magnet_link_ret).c_str());
                return EXIT_FAILURE;
            }
            const auto ti = std::get<lt::torrent_info>(magnet_link_ret);
            torrent_params.ti = std::make_shared<lt::torrent_info>(ti);
        }
        if (!use_url && !use_magnet) {
            const auto path = std::filesystem::canonical(torrent_url);
            torrent_params.ti = std::make_shared<lt::torrent_info>(path.string());
        }
    } catch (std::exception e) {
        fprintf(stderr, "Failed to load torrent info: %s\n", e.what());
        return EXIT_FAILURE;
    }

    if (use_url) {
        fprintf(stdout, "Downloading torrent from %s\n", torrent_url.c_str());
        const auto torrent_content_ret = download_torrent_info(torrent_url);
        if (std::holds_alternative<std::string>(torrent_content_ret)) {
            fprintf(stderr, "Failed to download torrent info: %s\n", std::get<std::string>(torrent_content_ret).c_str());
            return EXIT_FAILURE;
        }
        torrent_params.ti = std::make_shared<lt::torrent_info>(std::get<lt::torrent_info>(torrent_content_ret));
    }

    file_hashlist_t hashlist;
    // TODO: load hashlist from db
    try {
        hashlist = load_hashlist(hashlist_path.string());
    } catch (StreamError &e) {
        fprintf(stderr, "Hashlist not found at %s. Creating new.\n", hashlist_path.string().c_str());
    }

    auto app_state = std::make_shared<AppState>(db, true);
    auto s3_uploader = std::make_shared<S3Uploader>(0, s3_url, s3_access_key, s3_secret_key, s3_bucket, s3_region, download_path, upload_path);
    auto torrent_downloader = std::make_shared<TorrentDownloader>(torrent_params);
    AppSync app_sync(
        app_state,
        s3_uploader,
        torrent_downloader,
        hashlist,
        limit_size_bytes,
        download_path,
        extract_files
    );

    const auto sync_ret = app_sync.full_sync();
    if (std::holds_alternative<std::string>(sync_ret)) {
        fprintf(stderr, "Could not start sync. Error:\n%s\n", std::get<std::string>(sync_ret).c_str());
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Downloading torrent completed\n");

    const auto file_errors = std::get<std::vector<file_upload_error_t>>(sync_ret);

    // note, that files with errors are removed from new hashlist after comparing with old
    // hashlist. This way we won't try to delete failed files from S3
    for (const auto &f : file_errors) {
        hashlist.erase(f.file_name);
    }

    const auto removed_files = get_removed_files(hashlist, *torrent_params.ti);

    for (const auto &f: removed_files) {
        if (hashlist[f].linked_files.size() > 0) {
            // This file is a parent file - remove linked files
            for (const auto &linked_file: hashlist[f].linked_files) {
                const auto delete_file_ret = s3_uploader->delete_file(linked_file);
                if (delete_file_ret.has_value()) {
                    const auto file_name_full = std::filesystem::path(download_path) / linked_file;
                    fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", file_name_full.string().c_str(), delete_file_ret.value().c_str());
                }
            }
        }
        // This file was deleted - remove from S3
        const auto delete_file_ret = s3_uploader->delete_file(f);
        if (delete_file_ret.has_value()) {
            const auto file_name_full = std::filesystem::path(download_path) / f;
            fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", file_name_full.string().c_str(), delete_file_ret.value().c_str());
        }
    }

    // save updated hashlist
    auto new_hashlist = create_hashlist(*torrent_params.ti, app_state->get_completed_files());
    // remove files with errors from hashlist
    for (const auto &f : file_errors) {
        new_hashlist.erase(f.file_name);
    }
    save_hashlist(hashlist_path.string(), new_hashlist);

    fprintf(stdout, "Torrent-S3 sync completed\n");
    return EXIT_SUCCESS;
}