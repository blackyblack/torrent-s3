#include <iostream>
#include <thread>
#include <cstdio>
#include <cstdlib>

#include <libtorrent/add_torrent_params.hpp>
#include <curl/curl.h>

#include "./deque/deque.hpp"
#include "./torrent/torrent_download.hpp"
#include "./hashlist/hashlist.hpp"
#include "./command_line/cxxopts.hpp"
#include "./s3/s3.hpp"
#include "./path_utils/path_utils.hpp"

#define FILE_HASHES_STORAGE_NAME ".torrent_s3_hashlist"

static void print_usage(const cxxopts::Options &options) {
  fprintf(stderr, options.help().c_str());
}

static bool download_file(const std::string &url, const std::string &save_to) {
  auto curl = curl_easy_init();
  if (curl == nullptr) {
    fprintf(stderr, "Could not start Curl\n");
    return false;
  }

  auto fp = fopen(save_to.c_str(), "wb");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  const auto res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  fclose(fp);
  if (res != CURLE_OK) {
    fprintf(stderr, "Curl error: %s\n", curl_easy_strerror(res));
    return false;
  }
  return true;
}

int main(int argc, char const* argv[])
{
  cxxopts::Options options("torrent-s3");

  options.add_options()
      ("u,torrent-url", "Torrent file URL", cxxopts::value<std::string>()) 
      ("t,torrent-file", "Torrent file path", cxxopts::value<std::string>())
      ("m,magnet-link", "Magnet link", cxxopts::value<std::string>())
      ("d,download-path", "Temporary directory for downloaded files", cxxopts::value<std::string>())
      ("l,limit-size", "Temporary directory maximum size in bytes", cxxopts::value<unsigned long long>())
      ("p,hashlist-file", std::string("Path to hashlist. Default is <download-path>/") + std::string(FILE_HASHES_STORAGE_NAME), cxxopts::value<std::string>())
      ("h,help", "Show help");
  
  cxxopts::ParseResult args;

  try {
    args = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception &x) {
    fprintf(stderr, "torrent-s3: %s\n", x.what());
    print_usage(options);
    return EXIT_FAILURE;
  }

  if (args.count("help")) {
    print_usage(options);
    return EXIT_SUCCESS;
  }

  const auto torrent_url = args["torrent-url"];
  const auto torrent_file = args["torrent-file"];
  const auto magnet_link = args["magnet-link"];

  bool use_magnet = false;
  bool use_url = false;
  std::string source;
  if (args.count("torrent-url")) {
    if (args.count("torrent-file") || args.count("magnet-link")) {
      fprintf(stderr, "Using torrent URL for downloading. Ignoring other torrent file sources\n");
    }
    source = args["torrent-url"].as<std::string>();
  }
  if (!args.count("torrent-url") && args.count("torrent-file")) {
    if (args.count("magnet-link")) {
      fprintf(stderr, "Using torrent file for downloading. Ignoring other torrent file sources\n");
    }
    source = args["torrent-file"].as<std::string>();
  }
  if (!args.count("torrent-url") && !args.count("torrent-file") && args.count("magnet-link")) {
    use_magnet = true;
    source = args["magnet-link"].as<std::string>();
  }
  if (args.count("torrent-url")) {
    use_url = true;
  }
  if (source.empty()) {
    fprintf(stderr, "Torrent URL is not set.\n");
    print_usage(options);
    return EXIT_FAILURE;
  }
  std::string download_path = ".";
  if (args.count("download-path")) {
    download_path = args["download-path"].as<std::string>();
  }
  unsigned long long limit_size_bytes = LLONG_MAX;
  if (args.count("limit-size")) {
    limit_size_bytes = args["limit-size"].as<unsigned long long>();
  }
  auto hashlist_path = path_join(download_path, FILE_HASHES_STORAGE_NAME);
  if (args.count("hashlist-file")) {
    hashlist_path = args["hashlist-file"].as<std::string>();
  }

  fprintf(stdout, "Torrent-S3 starting\n");
  std::string what = std::string("\"") + source + ("\"");
  if (!use_url) {
    if (use_magnet) {
      what = std::string("magnet link \"") + source + ("\"");
    } else {
      what = std::string("file \"") + source + ("\"");
    }
  }
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
        auto ti = load_magnet_link_info(source);
        torrent_params.ti = std::make_shared<lt::torrent_info>(ti);
      }
      catch (TorrentError& e) {
        fprintf(stderr, "Error during downloading magnet link info: %s\n", e.what());
        return EXIT_FAILURE;
      }
    }
    if (!use_url && !use_magnet) {
      torrent_params.ti = std::make_shared<lt::torrent_info>(source);
    }
  } catch (std::exception e) {
    fprintf(stderr, "Failed to load torrent info\n");
    return EXIT_FAILURE;
  }

  if (use_url) {
    //download_file(source);
    fprintf(stderr, "Downloading torrents from URL is not currently supported\n");
    return EXIT_FAILURE;
  }

  file_hashlist_t hashlist;
  try {
    hashlist = load_hashlist(hashlist_path);
  }
  catch (StreamError &e) {
    fprintf(stderr, "Hashlist not found at %s. Creating new.\n", hashlist_path.c_str());
  }
  const auto new_hashlist = compare_hashlists(hashlist, *torrent_params.ti);

  std::vector<file_info_t> files;

  for (const auto &file_index: torrent_params.ti->files().file_range()) {
    const auto file_name = torrent_params.ti->files().file_name(file_index).to_string();
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

  S3Uploader s3_uploader(download_path, 0);
  s3_uploader.start();

  try {
    download_torrent_files(torrent_params, files, s3_uploader, limit_size_bytes);
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
      fprintf(stdout, "File \"%s\" was deleted. Remove from S3\n", f.first.c_str());
    }
  }

  // save updated hashlist
  save_hashlist(hashlist_path, new_hashlist);
  return EXIT_SUCCESS;
}