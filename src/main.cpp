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

static void populate_folders(LinkedFiles &folders, std::vector<std::string> files) {
    for (const auto &f : files) {
        auto child = std::filesystem::path(f);
        while (true) {
            const auto parent = child.parent_path();
            if (parent.empty()) {
                break;
            }
            if (folders.get_parent(child.string()).has_value()) {
                break;
            }
            folders.add_files(parent.string(), {child.string()});
            child = parent;
        }
    }
}

static void delete_child(LinkedFiles &folders, std::string file_name, const std::filesystem::path path_from) {
    while (true) {
        if (file_name.empty()) {
            break;
        }
        if (file_name == ".") {
            break;
        }
        const auto full_name = path_from / file_name;
        fprintf(stdout, "Deleting %s\n", full_name.string().c_str());
        std::filesystem::remove(std::filesystem::u8path(full_name.string()));

        const auto parent = folders.get_parent(file_name);
        if (!parent.has_value()) {
            break;
        }
        const auto parent_name = parent.value();
        folders.remove_child(file_name);

        const auto children = folders.get_files();
        const auto parent_iter = children.find(parent_name);
        if (parent_iter != children.end() && parent_iter->second.size() > 0) {
            break;
        }
        folders.remove_parent(parent_name);
        file_name = parent_name;
    }
}

static std::string strip_prefix(const std::string &from, const std::string &prefix) {
    return from.find(prefix) == 0 ? from.substr(prefix.size()) : from;
}

std::unordered_set<std::string> filter_complete_files(const std::unordered_set<std::string>& files, const AppState &state) {
    std::unordered_set<std::string> ret;
    for (const auto &f : files) {
        const auto status = state.get_file_status(f);
        if (status != file_status_t::FILE_STATUS_READY) {
            ret.insert(f);
        }
    }
    return ret;
}

static void s3_file_upload_complete(const std::filesystem::path path_from, LinkedFiles &folders, const std::string relative_filename, DownloadingFiles &downloading_files, AppState &state) {
    const auto parent = get_uploading_parent(state, relative_filename);

    delete_child(folders, relative_filename, path_from);
    state.file_complete(relative_filename);
    if (!parent.has_value()) {
        downloading_files.complete_file(relative_filename);
        return;
    }

    const auto parent_file_name = parent.value();
    const auto linked_files = state.get_uploading_files();
    const auto parent_iter = linked_files.find(parent_file_name);

    // if parent is still not completed, keep uploading
    if (parent_iter != linked_files.end() && parent_iter->second.size() > 0) {
        return;
    }
    downloading_files.complete_file(parent_file_name);
    state.file_complete(parent_file_name);
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

    S3Uploader s3_uploader(0, s3_url, s3_access_key, s3_secret_key, s3_bucket, s3_region, download_path, upload_path);
    const auto s3_start_ret = s3_uploader.start();
    if (s3_start_ret.has_value()) {
        fprintf(stderr, "Could not start S3 uploader. Error:\n%s\n", s3_start_ret.value().c_str());
        return EXIT_FAILURE;
    }

    AppState app_state(db, true);

    const auto new_files_set = get_updated_files(hashlist, *torrent_params.ti);
    const auto new_files_set_filtered = filter_complete_files(new_files_set, app_state);
    // TODO: check if files have been deleted from S3

    std::vector new_files(new_files_set_filtered.begin(), new_files_set_filtered.end());
    DownloadingFiles downloading_files(*torrent_params.ti, new_files, limit_size_bytes);
    const auto next_chunk = downloading_files.download_next_chunk();
    // use this struct to track empty folders
    LinkedFiles folders;
    bool has_uploading_files = false;

    populate_folders(folders, new_files);

    TorrentDownloader torrent_downloader(torrent_params);
    torrent_downloader.start();
    torrent_downloader.download_files(next_chunk);

    auto &download_progress = torrent_downloader.get_progress_queue();
    auto &upload_progress = s3_uploader.get_progress_queue();
    std::vector<file_upload_error_t> file_errors;
    bool download_error = false;
    while(true) {
        if ((downloading_files.is_completed() || download_error) && !has_uploading_files) break;
        while (!download_progress.empty()) {
            const auto torrent_event = download_progress.pop_front_waiting();
            if (std::holds_alternative<TorrentProgressDownloadError>(torrent_event)) {
                const auto torrent_error = std::get<TorrentProgressDownloadError>(torrent_event);
                fprintf(stderr, "Error during downloading torrent files: %s\n", torrent_error.error.c_str());
                torrent_downloader.stop();
                download_error = true;
                continue;
            }
            const auto torrent_file_downloaded = std::get<TorrentProgressDownloadOk>(torrent_event);

            const auto file_name_full = std::filesystem::path(download_path) / torrent_file_downloaded.file_name;
            const auto file_name_str = file_name_full.string();
            std::vector<std::string> linked_file_names;

            if (extract_files && is_packed(file_name_str)) {
                // automatically create folder for extracted files
                const auto extract_folder = folder_for_unpacked_file(file_name_str);
                const auto extract_ret = unpack_file(file_name_str, extract_folder);
                // upload without unpacking if extraction failed
                if (std::holds_alternative<std::string>(extract_ret)) {
                    fprintf(stderr, "Could not extract file \"%s\": %s\n", file_name_str.c_str(), std::get<std::string>(extract_ret).c_str());
                } else {
                    const auto files = std::get<std::vector<file_unpack_info_t>>(extract_ret);
                    std::vector<file_unpack_info_t> filtered_files;
                    std::copy_if(files.begin(), files.end(), std::back_inserter(filtered_files), [](const auto &f) {
                        return !f.error_message.has_value();
                    });
                    // upload without unpacking if some files have not been extracted properly
                    if (filtered_files.size() != files.size()) {
                        fprintf(stderr, "Some files were not extracted from \"%s\"\n", file_name_str.c_str());
                    } else {
                        std::transform(filtered_files.begin(), filtered_files.end(), std::back_inserter(linked_file_names), [&download_path](const file_unpack_info_t &f) {
                            auto linked_file_stripped = strip_prefix(path_to_relative(f.name, download_path).string(), "./");
                            linked_file_stripped = strip_prefix(linked_file_stripped, ".\\");
                            return linked_file_stripped;
                        });
                        // erase archive after extraction
                        std::filesystem::remove(std::filesystem::u8path(file_name_str));
                        folders.remove_child(torrent_file_downloaded.file_name);
                    }
                    populate_folders(folders, linked_file_names);
                }
            }
            app_state.add_files(torrent_file_downloaded.file_name, linked_file_names);
            // upload parent file if linked files are empty
            if (linked_file_names.empty()) {
                has_uploading_files = true;
                app_state.file_upload(torrent_file_downloaded.file_name);
                s3_uploader.new_file(torrent_file_downloaded.file_name);
                continue;
            }
            // upload linked files
            for (const auto &f: linked_file_names) {
                has_uploading_files = true;
                app_state.file_upload(f);
                s3_uploader.new_file(f);
            }
            continue;
        }
        while (!upload_progress.empty()) {
            const auto s3_event = upload_progress.pop_front_waiting();
            if (std::holds_alternative<S3ProgressUploadError>(s3_event)) {
                const auto s3_error = std::get<S3ProgressUploadError>(s3_event);
                fprintf(stderr, "Error during uploading files to S3: %s\n", s3_error.error.c_str());
                file_errors.push_back(file_upload_error_t { s3_error.file_name, s3_error.error });
                // process as completed to avoid infinite loop
                s3_file_upload_complete(download_path, folders, s3_error.file_name, downloading_files, app_state);
                if (app_state.get_uploading_files().empty()) {
                    has_uploading_files = false;
                }
                continue;
            }
            const auto s3_file_uploaded = std::get<S3ProgressUploadOk>(s3_event);

            s3_file_upload_complete(download_path, folders, s3_file_uploaded.file_name, downloading_files, app_state);
            if (app_state.get_uploading_files().empty()) {
                has_uploading_files = false;
            }

            if (download_error) {
                continue;
            }
            // if some linked files are not uploaded yet, don't start next download
            const auto maybe_parent = get_uploading_parent(app_state, s3_file_uploaded.file_name);
            if (maybe_parent.has_value()) {
                continue;
            }
            // check for completed downloads only on S3 events for optimization purpose
            const auto next_chunk = downloading_files.download_next_chunk();
            if (next_chunk.empty()) {
                continue;
            }
            torrent_downloader.download_files(next_chunk);
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
                const auto delete_file_ret = s3_uploader.delete_file(linked_file);
                if (delete_file_ret.has_value()) {
                    const auto file_name_full = std::filesystem::path(download_path) / linked_file;
                    fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", file_name_full.string().c_str(), delete_file_ret.value().c_str());
                }
            }
        }
        // This file was deleted - remove from S3
        const auto delete_file_ret = s3_uploader.delete_file(f);
        if (delete_file_ret.has_value()) {
            const auto file_name_full = std::filesystem::path(download_path) / f;
            fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", file_name_full.string().c_str(), delete_file_ret.value().c_str());
        }
    }

    // save updated hashlist
    auto new_hashlist = create_hashlist(*torrent_params.ti, app_state.get_linked_files());
    // remove files with errors from hashlist
    for (const auto &f : file_errors) {
        new_hashlist.erase(f.file_name);
    }
    save_hashlist(hashlist_path.string(), new_hashlist);

    fprintf(stdout, "Torrent-S3 sync completed\n");
    return EXIT_SUCCESS;
}