#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <sqlite3.h>

#include "./torrent/torrent_download.hpp"
#include "./command_line/cxxopts.hpp"
#include "./s3/s3.hpp"
#include "./curl/curl.hpp"
#include "./path/path_utils.hpp"
#include "./db/sqlite.hpp"
#include "./app_state/state.hpp"
#include "./app_sync/sync.hpp"

#define STRING(x) #x
#define XSTRING(x) STRING(x)

#define APP_NAME XSTRING(CMAKE_PROJECT_NAME)
#define APP_VERSION XSTRING(CMAKE_PROJECT_VERSION)

#define STATE_STORAGE_NAME "default.sqlite"

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
           ("x,extract-files", "Extract downloaded archives before uploading")
           ("q,state-file", std::string("Path to application state file. Default is <download-path>/") + std::string(STATE_STORAGE_NAME), cxxopts::value<std::string>())
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
    auto app_state_path = (std::filesystem::path(download_path) / std::filesystem::path(STATE_STORAGE_NAME)).string();
    if (args.count("state-file")) {
        app_state_path = args["state-file"].as<std::string>();
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

    const auto db_open_ret = db_open(app_state_path);
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

    auto app_state = std::make_shared<AppState>(db, false);
    auto s3_uploader = std::make_shared<S3Uploader>(0, s3_url, s3_access_key, s3_secret_key, s3_bucket, s3_region, download_path, upload_path);
    auto torrent_downloader = std::make_shared<TorrentDownloader>(torrent_params);
    AppSync app_sync(
        app_state,
        s3_uploader,
        torrent_downloader,
        limit_size_bytes,
        download_path,
        extract_files
    );

    const auto sync_ret = app_sync.full_sync();
    if (std::holds_alternative<std::string>(sync_ret)) {
        fprintf(stderr, "Could not execute sync. Error:\n%s\n", std::get<std::string>(sync_ret).c_str());
        return EXIT_FAILURE;
    }

    fprintf(stdout, "Torrent-S3 sync completed\n");
    return EXIT_SUCCESS;
}