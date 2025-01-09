#include <iostream>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <fstream>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>

#include "./deque/deque.hpp"
#include "./torrent/torrent_download.hpp"
#include "./hashlist/hashlist.hpp"
#include "./command_line/cxxopts.hpp"
#include "./s3/s3.hpp"
#include "./curl/curl.hpp"
#include "./archive/archive.hpp"

#define STRING(x) #x
#define XSTRING(x) STRING(x)

#define APP_NAME XSTRING(CMAKE_PROJECT_NAME)
#define APP_VERSION XSTRING(CMAKE_PROJECT_VERSION)

#define FILE_HASHES_STORAGE_NAME ".torrent_s3_hashlist"

struct file_upload_error_t {
    std::string file_name;
    std::string error_message;
};

static void print_usage(const cxxopts::Options &options) {
    fprintf(stderr, "%s", options.help().c_str());
}

static bool is_http_url(const std::string &url) {
    std::regex url_regex(R"(^(https?:\/\/)?([\da-z\.-]+)\.([a-z\.]{2,6})([\/\w \.-]*)*\/?$)");
    return std::regex_match(url, url_regex);
}

static std::vector<std::string> next_downloadable_files(const std::unordered_set<std::string> &all_files, const std::unordered_set<std::string> &completed_files, const std::unordered_set<std::string> &downloading_files, const lt::torrent_info &torrent, unsigned long long size_limit_bytes) {
    unsigned long long total_size = 0;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);

        if (all_files.find(file_name) == all_files.end()) {
            continue;
        }
        if (completed_files.find(file_name) != completed_files.end()) {
            continue;
        }
        if (downloading_files.find(file_name) != downloading_files.end()) {
            total_size += file_size;
        }
    }

    std::vector<std::string> to_download_files;
    int first_uncompleted_index = -1;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_size = torrent.files().file_size(file_index);
        const auto file_name = torrent.files().file_path(file_index);

        if (all_files.find(file_name) == all_files.end()) {
            continue;
        }
        if (completed_files.find(file_name) != completed_files.end()) {
            continue;
        }
        if (downloading_files.find(file_name) != downloading_files.end()) {
            continue;
        }
        if (first_uncompleted_index < 0) first_uncompleted_index = (int) file_index;
        if (total_size + file_size > size_limit_bytes) {
            continue;
        }
        to_download_files.push_back(file_name);
        total_size += file_size;
    }

    // if no file fits a size limit, add first available file and download one by one
    if (total_size == 0 && first_uncompleted_index >= 0) {
        const auto file_name = torrent.files().file_path(lt::file_index_t{first_uncompleted_index});
        to_download_files.push_back(file_name);
    }
    return to_download_files;
}

int main(int argc, char const* argv[]) {
    cxxopts::Options options(APP_NAME);

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
        ("f,extract-files-path", "Temporary directory for extracted files", cxxopts::value<std::string>())
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

    std::string extract_files_path = "";
    if (args.count("extract-files-path")) {
        extract_files_path = args["extract-files-path"].as<std::string>();
    }

    const auto extract_files = args.count("extract-files") > 0;

    fprintf(stdout, "Torrent-S3 starting\n");

    if (limit_size_bytes == LLONG_MAX) {
        fprintf(stdout, "Downloading from %s to temporary directory \"%s\" without size limit\n", what.c_str(), download_path.c_str());
    } else {
        fprintf(stdout, "Downloading from %s to temporary directory \"%s\" with size limit %.3f MB\n", what.c_str(), download_path.c_str(), ((double) limit_size_bytes) / 1024 / 1024);
    }

    lt::add_torrent_params torrent_params;
    torrent_params.save_path = download_path;

    try {
        if (use_magnet) {
            fprintf(stdout, "Loading magnet link metadata\n");
            try {
                auto ti = load_magnet_link_info(torrent_url);
                torrent_params.ti = std::make_shared<lt::torrent_info>(ti);
            } catch (TorrentError& e) {
                fprintf(stderr, "Error during downloading magnet link info: %s\n", e.what());
                return EXIT_FAILURE;
            }
        }
        if (!use_url && !use_magnet) {
            torrent_params.ti = std::make_shared<lt::torrent_info>(torrent_url);
        }
    } catch (std::exception e) {
        fprintf(stderr, "Failed to load torrent info\n");
        return EXIT_FAILURE;
    }

    if (use_url) {
        fprintf(stdout, "Downloading torrent from %s\n", torrent_url.c_str());
        try {
            const auto torrent_content = download_torrent_info(torrent_url);
            torrent_params.ti = std::make_shared<lt::torrent_info>(torrent_content);
        } catch (DownloadError &e) {
            fprintf(stderr, "Failed to download torrent info. Exiting.\n");
            return EXIT_FAILURE;
        } catch (ParseError &e) {
            fprintf(stderr, "Failed to parse torrent info. Exiting.\n");
            return EXIT_FAILURE;
        }
    }

    file_hashlist_t hashlist;
    try {
        hashlist = load_hashlist(hashlist_path.string());
    } catch (StreamError &e) {
        fprintf(stderr, "Hashlist not found at %s. Creating new.\n", hashlist_path.string().c_str());
    }

    S3Uploader s3_uploader(download_path, 0, s3_url, s3_access_key, s3_secret_key, s3_bucket, s3_region, upload_path);
    try {
        s3_uploader.start();
    } catch (S3Error& e) {
        fprintf(stderr, "Could not start S3 uploader. Error:\n%s\n", e.what());
        return EXIT_FAILURE;
    }

    auto new_files = get_updated_files(hashlist, *torrent_params.ti);
    auto downloading_files_list = next_downloadable_files(new_files, {}, {}, *torrent_params.ti, limit_size_bytes);
    std::unordered_set<std::string> completed_files;
    std::unordered_set<std::string> downloading_files(downloading_files_list.begin(), downloading_files_list.end());

    TorrentDownloader torrent_downloader(torrent_params);
    torrent_downloader.start();
    torrent_downloader.download_files(downloading_files_list);

    auto &download_progress = torrent_downloader.get_progress_queue();
    auto &upload_progress = s3_uploader.get_progress_queue();
    std::vector<file_upload_error_t> file_errors;
    bool download_completed = downloading_files.empty();
    while(true) {
        while (!download_progress.empty()) {
            const auto torrent_event = download_progress.pop_front_waiting();
            if (torrent_event.message_type == torrent_progress_type_t::DOWNLOAD_ERROR) {
                fprintf(stderr, "Error during downloading torrent files: %s\n", torrent_event.error.c_str());
                torrent_downloader.stop();
                s3_uploader.stop();
                return EXIT_FAILURE;
            }
            // TODO: unpack archived file before uploading and add to linked files
            s3_uploader.new_file(torrent_event.file_name, torrent_event.file_index);
            continue;
        }
        while (!upload_progress.empty()) {
            const auto s3_event = upload_progress.pop_front_waiting();
            if (s3_event.message_type == s3_progress_type_t::UPLOAD_ERROR) {
                fprintf(stderr, "Error during uploading files to S3: %s\n", s3_event.error.c_str());
                file_errors.push_back(file_upload_error_t {s3_event.file_name, s3_event.error});
                continue;
            }
            // check for completed downloads only on S3 events for optimization purpose
            auto file_to_erase = new_files.find(s3_event.file_name);
            if (file_to_erase != new_files.end()) {
                new_files.erase(file_to_erase);
            }
            if (new_files.size() == 0) {
                download_completed = true;
            }

            completed_files.insert(s3_event.file_name);
            file_to_erase = downloading_files.find(s3_event.file_name);
            if (file_to_erase != downloading_files.end()) {
                downloading_files.erase(file_to_erase);
            }
            auto next_downloading_files_chunk = next_downloadable_files(new_files, completed_files, downloading_files, *torrent_params.ti, limit_size_bytes);
            if (next_downloading_files_chunk.size() == 0) {
                continue;
            }
            for (const auto &f : next_downloading_files_chunk) {
                downloading_files.insert(f);
            }
            torrent_downloader.download_files(next_downloading_files_chunk);
        }
    }

    fprintf(stdout, "Downloading torrent completed\n");

    torrent_downloader.stop();
    s3_uploader.stop();

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
                try {
                    s3_uploader.delete_file(linked_file);
                    fprintf(stdout, "File \"%s\" was deleted. Remove from S3\n", linked_file.c_str());
                } catch (S3Error &e) {
                    fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", linked_file.c_str(), e.what());
                }
            }
            continue;
        }
        // This file was deleted - remove from S3
        try {
            s3_uploader.delete_file(f);
            fprintf(stdout, "File \"%s\" was deleted. Remove from S3\n", f.c_str());
        } catch (S3Error &e) {
            fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", f.c_str(), e.what());
        }
    }

    // save updated hashlist
    // TODO: generate linked files for archives
    const auto new_hashlist = create_hashlist(*torrent_params.ti, {});
    save_hashlist(hashlist_path.string(), new_hashlist);

    fprintf(stdout, "Torrent-S3 sync completed\n");
    return EXIT_SUCCESS;
}