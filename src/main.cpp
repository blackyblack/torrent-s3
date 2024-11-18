#include <iostream>
#include <thread>
#include <cstdio>
#include <cstdlib>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "./deque.hpp"
#include "../include/torrent_download.hpp"
#include "../include/hashlist.hpp"

#define FILE_HASHES_STORAGE_NAME ".torrent_s3_hashlist"

class InputParser{
    public:
        InputParser (int &argc, const char **argv){
            for (int i=1; i < argc; ++i)
                this->tokens.push_back(std::string(argv[i]));
        }
        /// @author iain
        const std::string& getCmdOption(const std::string &option) const{
            std::vector<std::string>::const_iterator itr;
            itr =  std::find(this->tokens.begin(), this->tokens.end(), option);
            if (itr != this->tokens.end() && ++itr != this->tokens.end()){
                return *itr;
            }
            static const std::string empty_string("");
            return empty_string;
        }
        /// @author iain
        bool cmdOptionExists(const std::string &option) const{
            return std::find(this->tokens.begin(), this->tokens.end(), option)
                   != this->tokens.end();
        }
    private:
        std::vector <std::string> tokens;
};

enum file_status_t {
  WAITING,
  DOWNLOADING,
  COMPLETED
};

struct file_info_t {
  unsigned long long size;
  file_status_t status;
};

struct s3_task_event_t {
  bool terminate;
  std::string new_file;
};

static std::vector<file_info_t> files;
static ThreadSafeDeque<s3_task_event_t> upload_files_queue;
static std::string download_path;
static unsigned long long limit_size_bytes = LLONG_MAX;

std::string path_join(const std::string& p1, const std::string& p2)
{
  char sep = '/';
  std::string tmp = p1;

#ifdef _WIN32
  sep = '\\';
#endif

  // Add separator if it is not included in the first path:
  if (p1[p1.length() - 1] != sep) {
    tmp += sep;
    return tmp + p2;
  }
  return p1 + p2;
}

static std::vector<int> next_downloadable_indexes(const std::vector<file_info_t> &files, unsigned long long size_limit_bytes) {
  unsigned long long total_size = 0;
  std::vector<int> to_download_file_indexes;
  int first_uncompleted_index = -1;
  // calculate currently downloading size
  for (const auto &f: files) {
    if (f.status != DOWNLOADING) continue;
    total_size += f.size;
  }
  // try to find next files that fits the size limit
  for (int i = 0; i < files.size(); i++) {
    auto f = files[i];
    if (f.status != WAITING) continue;
    if (first_uncompleted_index < 0) first_uncompleted_index = i;
    auto file_size = f.size;
    if (total_size + file_size > size_limit_bytes) {
      continue;
    }
    to_download_file_indexes.push_back(i);
    total_size += file_size;
  }
  // if no file fits a size limit, add first available file and download one by one
  if (total_size == 0 && first_uncompleted_index >= 0) {
    to_download_file_indexes.push_back(first_uncompleted_index);
  }
  return to_download_file_indexes;
}

static bool is_download_complete(const std::vector<file_info_t> &files) {
  for (const auto &f: files) {
    if (f.status != COMPLETED) return false;
  }
  return true;
}

void s3_upload_task(ThreadSafeDeque<s3_task_event_t> &message_queue)
{
  fprintf(stderr, "Starting S3 upload task\n");

  while (true) {
    auto message = message_queue.pop_front_waiting();
    if (message.terminate) {
      return;
    }

    auto filename = message.new_file;
    fprintf(stderr, "Uploading %s\n", filename.c_str());
    fprintf(stderr, "Deleting %s\n", filename.c_str());
    std::remove(filename.c_str());
  }

  fprintf(stderr, "S3 upload task completed\n");
}

static bool downloaded_file_event_handler(lt::torrent_handle &handle, unsigned int file_index) {
  files[file_index].status = COMPLETED;
  std::cout << "File #" << file_index + 1 << " completed" << std::endl;

  const auto file_name = path_join(download_path, handle.torrent_file()->files().file_path((lt::file_index_t) file_index));
  upload_files_queue.push_back(s3_task_event_t { false, file_name });

  // now we can allow additional downloads
  auto to_download_indexes = next_downloadable_indexes(files, limit_size_bytes);
  if (to_download_indexes.size() == 0) {
    if (is_download_complete(files)) {
      return true;
    }
    return false;
  }

  auto file_priorities = handle.get_file_priorities();
  for (auto i: to_download_indexes) {
    file_priorities[i] = libtorrent::default_priority;
    files[i].status = DOWNLOADING;
  }
  handle.prioritize_files(file_priorities);
  return false;
}

int main(int argc, char const* argv[])
{
  if (argc < 3) {
    fprintf(stderr, "usage: torrent-s3 [-u <torrent-url>] [-t <torrent-file>] [-m <magnet-link>] [-d <download-path>] [-l <limit-size-bytes>] [-p <hashlist-file>]\n");
    return 1;
  }
  InputParser args(argc, argv);
  auto torrent_url = args.getCmdOption("-u");
  auto torrent_file = args.getCmdOption("-t");
  auto magnet_link = args.getCmdOption("-m");
  bool use_magnet = false;
  bool use_url = false;
  std::string source = torrent_file;
  if (torrent_url.empty() && torrent_file.empty() && magnet_link.empty()) {
    fprintf(stderr, "Torrent URL is not set.\nUse -u <torrent-url> to set URL for .torrent file.\nUse -f <torrent-file> to set path for .torrent file.\nUse -m <magnet-link> to set magnet link\n");
    return 1;
  }
  if (!torrent_url.empty()) {
    if (!torrent_file.empty() || !magnet_link.empty()) {
      fprintf(stderr, "Using torrent URL for downloading. Ignoring other torrent file sources\n");
    }
    source = torrent_url;
  }
  if (torrent_url.empty() && !torrent_file.empty()) {
    if (!magnet_link.empty()) {
      fprintf(stderr, "Using torrent file for downloading. Ignoring other torrent file sources\n");
    }
    source = torrent_file;
  }
  if (torrent_url.empty() && torrent_file.empty() && !magnet_link.empty()) {
    use_magnet = true;
    source = magnet_link;
  }
  if (!torrent_url.empty()) {
    use_url = true;
  }
  download_path = args.getCmdOption("-d");
  if (download_path.empty()) {
    download_path = ".";
  }
  auto limit_size_str = args.getCmdOption("-l");
  if (!limit_size_str.empty()) {
    limit_size_bytes = std::stoull(limit_size_str);
  }

  auto hashlist_path = args.getCmdOption("-p");
  if (hashlist_path.empty()) {
    hashlist_path = path_join(download_path, FILE_HASHES_STORAGE_NAME);
  }

  fprintf(stderr, "Torrent-S3 starting\n");
  std::ostringstream stringStream;
  stringStream << "\"" << source << "\"";
  std::string what = stringStream.str();
  if (!use_url) {
    if (use_magnet) {
      std::ostringstream stringStream;
      stringStream << "magnet link \"" << source << "\"";
      what = stringStream.str();
    } else {
      std::ostringstream stringStream;
      stringStream << "file \"" << source << "\"";
      what = stringStream.str();
    }
  }
  if (limit_size_bytes == LLONG_MAX) {
    fprintf(stderr, "Downloading from %s to temporary directory \"%s\" without size limit\n", what.c_str(), download_path.c_str());
  } else {
    fprintf(stderr, "Downloading from %s to temporary directory \"%s\" with size limit %.3f MB\n", what.c_str(), download_path.c_str(), ((double)limit_size_bytes) / 1024 / 1024);
  }

  lt::add_torrent_params torrent_params;
  torrent_params.save_path = download_path;

  try {
    if (use_magnet) {
      fprintf(stderr, "Loading magnet link metadata\n");
      try {
        auto ti = load_magnet_link_info(source);
        torrent_params.ti = std::make_shared<lt::torrent_info>(ti);
      }
      catch (TorrentError& e) {
        fprintf(stderr, "Error during downloading magnet link info: %s\n", e.what());
        return 1;
      }
    }
    if (!use_url && !use_magnet) {
      torrent_params.ti = std::make_shared<lt::torrent_info>(source);
    }
  } catch (std::exception e) {
    fprintf(stderr, "Failed to load torrent info\n");
    return 1;
  }

  if (use_url) {
    fprintf(stderr, "Downloading torrents from URL is not currently supported\n");
    return 1;
  }

  file_hashlist_t hashlist;
  try {
    hashlist = load_hashlist(hashlist_path);
  }
  catch (StreamError &e) {
    fprintf(stderr, "Hashlist not found at %s. Creating new.\n", hashlist_path.c_str());
  }
  const auto new_hashlist = compare_hashlists(hashlist, *torrent_params.ti);

  files.clear();
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

  auto to_download_indexes = next_downloadable_indexes(files, limit_size_bytes);

  // only download files, that are in 'file_indexes'
  const auto file_count = torrent_params.ti->num_files();
  std::vector<lt::download_priority_t> file_priorities(file_count, libtorrent::dont_download);
  for (const auto i: to_download_indexes) {
    file_priorities[i] = libtorrent::default_priority;
  }
  torrent_params.file_priorities = file_priorities;

  subscribe_downloaded_file(&downloaded_file_event_handler);

  // use lambda to MSVC workaround
  std::thread s3_task_handle([&](){ s3_upload_task(upload_files_queue); });

  try {
    download_torrent_files(torrent_params);
  }
  catch (TorrentError& e) {
    fprintf(stderr, "Error during downloading torrent files: %s\n", e.what());
    return 1;
  }

  upload_files_queue.push_back(s3_task_event_t { true, "" });

  fprintf(stderr, "Downloading torrent completed\n");

  s3_task_handle.join();
  fprintf(stderr, "S3 upload completed\n");

  for (const auto &f: hashlist) {
    if (new_hashlist.count(f.first) == 0) {
      // This file was deleted - remove from S3
      fprintf(stderr, "File \"%s\" was deleted. Remove from S3\n", f.first.c_str());
    }
  }

  // save updated hashlist
  save_hashlist(hashlist_path, new_hashlist);
  return 0;
}