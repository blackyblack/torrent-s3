#include <iostream>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>

#include "./deque/deque.hpp"
#include "./torrent/torrent_download.hpp"
#include "./hashlist/hashlist.hpp"
#include "./command_line/cxxopts.hpp"
#include "./s3/s3.hpp"
#include "./curl/curl.hpp"

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

  options.add_options()
      ("t,torrent", "Torrent file path, HTTP URL or magnet link", cxxopts::value<std::string>())
      ("s,s3-url", "S3 service URL", cxxopts::value<std::string>())
      ("b,s3-bucket", "S3 bucket", cxxopts::value<std::string>())
      ("u,s3-upload-path", "S3 path to store uploaded files", cxxopts::value<std::string>())
      ("a,s3-access-key", "S3 access key", cxxopts::value<std::string>())
      ("k,s3-secret-key", "S3 secret key", cxxopts::value<std::string>())
      ("d,download-path", "Temporary directory for downloaded files", cxxopts::value<std::string>())
      ("l,limit-size", "Temporary directory maximum size in bytes", cxxopts::value<unsigned long long>())
      ("p,hashlist-file", std::string("Path to hashlist. Default is <download-path>/") + std::string(FILE_HASHES_STORAGE_NAME), cxxopts::value<std::string>())
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
      }
      catch (TorrentError& e) {
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
    }
    catch (DownloadError &e) {
      fprintf(stderr, "Failed to download torrent info. Exiting.\n");
      return EXIT_FAILURE;
    }
    catch (ParseError &e) {
      fprintf(stderr, "Failed to parse torrent info. Exiting.\n");
      return EXIT_FAILURE;
    }
  }

  file_hashlist_t hashlist;
  try {
    hashlist = load_hashlist(hashlist_path.string());
  }
  catch (StreamError &e) {
    fprintf(stderr, "Hashlist not found at %s. Creating new.\n", hashlist_path.string().c_str());
  }
  const auto new_hashlist = compare_hashlists(hashlist, *torrent_params.ti);

  std::vector<file_info_t> files;

  for (const auto &file_index: torrent_params.ti->files().file_range()) {
    const auto file_name = torrent_params.ti->files().file_path(file_index);
    if (new_hashlist.count(file_name) == 0) {
      continue;
    }
    const auto file_size = torrent_params.ti->files().file_size(file_index);
    if (hashlist.count(file_name) == 0) {
      files.push_back(file_info_t { (unsigned long long) file_size, WAITING });
      continue;
    }
    const auto new_hashes = new_hashlist.at(file_name);
    const auto stored_hashes = hashlist.at(file_name);
    if (new_hashes.size() != stored_hashes.size()) {
      files.push_back(file_info_t { (unsigned long long) file_size, WAITING });
      continue;
    }
    bool equal = true;
    for (auto i = 0; i < std::max(new_hashes.size(), stored_hashes.size()); i++) {
      if (new_hashes[i] != stored_hashes[i]) {
        equal = false;
        break;
      }
    }
    if (!equal) {
      files.push_back(file_info_t { (unsigned long long) file_size, WAITING });
      continue;
    }
    files.push_back(file_info_t { (unsigned long long) file_size, COMPLETED });
  }

  S3Uploader s3_uploader(download_path, 0, s3_url, s3_access_key, s3_secret_key, s3_bucket, upload_path);
  try {
    s3_uploader.start();
  }
  catch (S3Error& e) {
    fprintf(stderr, "Could not start S3 uploader. Error:\n%s\n", e.what());
    return EXIT_FAILURE;
  }

  std::vector<file_error_info_t> file_errors;
  try {
    file_errors = download_torrent_files(torrent_params, files, s3_uploader, limit_size_bytes);
  }
  catch (TorrentError& e) {
    fprintf(stderr, "Error during downloading torrent files: %s\n", e.what());
    return EXIT_FAILURE;
  }

  fprintf(stdout, "Downloading torrent completed\n");

  s3_uploader.stop();

  for (const auto &f: hashlist) {
    if (new_hashlist.count(f.first) == 0) {
      // This file was deleted - remove from S3
      try {
        s3_uploader.delete_file(f.first);
        fprintf(stdout, "File \"%s\" was deleted. Remove from S3\n", f.first.c_str());
      }
      catch (S3Error &e) {
        fprintf(stderr, "Could not delete file \"%s\" from S3. Error: %s\n", f.first.c_str(), e.what());
      }
    }
  }

  // note, that files with errors are removed from new hashlist after comparing with old
  // hashlist. This way we won't try to delete failed files from S3
  for (const auto &f : file_errors) {
    const auto file_name = torrent_params.ti->files().file_path((lt::file_index_t) f.file_index);
    hashlist.erase(file_name);
  }

  // save updated hashlist
  save_hashlist(hashlist_path.string(), new_hashlist);
  return EXIT_SUCCESS;
}