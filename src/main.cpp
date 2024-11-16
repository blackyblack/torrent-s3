#include <iostream>
#include <fstream>
#include <thread>
#include <cstdio>
#include <cstdlib>

#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>

#include "./deque.hpp"

#define FILE_HASHES_STORAGE_NAME ".torrent_s3_hashlist"

// return the name of a torrent status enum
char const* state(lt::torrent_status::state_t s)
{
	switch(s) {
		case lt::torrent_status::checking_files: return "checking";
		case lt::torrent_status::downloading_metadata: return "dl metadata";
		case lt::torrent_status::downloading: return "downloading";
		case lt::torrent_status::finished: return "finished";
		case lt::torrent_status::seeding: return "seeding";
		case lt::torrent_status::checking_resume_data: return "checking resume";
		default: return "<>";
	}
}

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

struct maybe_torrent_info_t {
  std::shared_ptr<const lt::torrent_info> info;
  std::string error;
};

struct s3_task_event_t {
  bool terminate;
  std::string new_file;
};

struct file_hash_info_t {
  std::vector<std::vector<char>> hashes;

  std::ostream& serialize(std::ostream& os, std::string file_name) const {
    auto hashes_size = hashes.size();
    os.write(reinterpret_cast<char *>(&hashes_size), sizeof(uint32_t));
    for (const auto &h : hashes) {
      auto hash_size = h.size();
      os.write(reinterpret_cast<char *>(&hash_size), sizeof(uint32_t));
      os.write(h.data(), h.size());
    }
    auto file_name_size = file_name.size();
    os.write(reinterpret_cast<char *>(&file_name_size), sizeof(uint32_t));
    os.write(file_name.c_str(), file_name.size());
    return os;
  }

  static std::pair<std::pair<file_hash_info_t, std::string>, std::string> deserialize(std::istream& is) {
    uint32_t hashes_size;
    is.read(reinterpret_cast<char *>(&hashes_size), sizeof(uint32_t));
    if (!is) {
      return std::pair<std::pair<file_hash_info_t, std::string>, std::string>(std::pair<file_hash_info_t, std::string>(file_hash_info_t(), ""), "eof");
    }
    std::vector<std::vector<char>> hashes;
    for (auto i = 0; i < hashes_size; i++) {
      uint32_t hash_size;
      is.read(reinterpret_cast<char *>(&hash_size), sizeof(uint32_t));
      if (!is) {
        return std::pair<std::pair<file_hash_info_t, std::string>, std::string>(std::pair<file_hash_info_t, std::string>(file_hash_info_t(), ""), "eof");
      }
      char* hash_bytes = new char[hash_size + 1]();
      is.read(hash_bytes, hash_size);
      std::vector<char> hash(hash_bytes, hash_bytes + hash_size);
      hashes.push_back(hash);
    }
    uint32_t name_size;
    is.read(reinterpret_cast<char *>(&name_size), sizeof(uint32_t));
    if (!is) {
      return std::pair<std::pair<file_hash_info_t, std::string>, std::string>(std::pair<file_hash_info_t, std::string>(file_hash_info_t(), ""), "eof");
    }
    char* name_bytes = new char[name_size + 1]();
    is.read(name_bytes, name_size);
    std::string name(name_bytes);
    return std::pair<std::pair<file_hash_info_t, std::string>, std::string>(std::pair<file_hash_info_t, std::string>(file_hash_info_t {hashes}, name), "");
  }
};

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

std::ostream& serialize_file_hashes(const std::unordered_map<std::string, file_hash_info_t>& files, std::ostream& os) {
  for (auto const &f: files) {
    f.second.serialize(os, f.first);
  }
  return os;
}

std::unordered_map<std::string, file_hash_info_t> deserialize_file_hashes(std::istream& is) {
  std::unordered_map<std::string, file_hash_info_t> files;
  while (true) {
    auto ret = file_hash_info_t::deserialize(is);
    if (ret.second == "eof") {
      break;
    }
    files[ret.first.second] = ret.first.first;
  }
  return files;
}

std::unordered_map<std::string, file_hash_info_t> load_hashlist(std::string download_path) {
  auto file_path = path_join(download_path, FILE_HASHES_STORAGE_NAME);
  std::ifstream ifs(file_path, std::ios::in | std::ios::binary);
  const auto ret = deserialize_file_hashes(ifs);
  ifs.close();
  return ret;
}

void save_hashlist(std::string download_path, const std::unordered_map<std::string, file_hash_info_t>& files) {
  auto file_path = path_join(download_path, FILE_HASHES_STORAGE_NAME);
  std::ofstream ofs(file_path, std::ios::out | std::ios::binary);
  serialize_file_hashes(files, ofs);
  ofs.close();
}

std::tuple<lt::piece_index_t, lt::piece_index_t> file_piece_range(lt::file_storage const& fs, lt::file_index_t const file)
{
  auto const range = fs.map_file(file, 0, 1);
  std::int64_t const file_size = fs.file_size(file);
  std::int64_t const piece_size = fs.piece_length();
  auto const end_piece = lt::piece_index_t(int((static_cast<int>(range.piece)
    * piece_size + range.start + file_size - 1) / piece_size + 1));
  return std::make_tuple(range.piece, end_piece);
}

bool is_download_complete(const std::vector<file_info_t> &files) {
  for (const auto &f: files) {
    if (f.status != COMPLETED) return false;
  }
  return true;
}

std::vector<int> next_downloadable_indexes(const std::vector<file_info_t> &files, unsigned long long size_limit_bytes) {
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

maybe_torrent_info_t load_magnet_link_info(std::string magnet_link) {
  auto magnet_params = lt::parse_magnet_uri(magnet_link);
  magnet_params.save_path = ".";
  magnet_params.flags |= lt::torrent_flags::default_dont_download;
  lt::session magnet_session;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert::error_notification | lt::alert::status_notification);
  magnet_session.apply_settings(p);
  lt::torrent_handle h = magnet_session.add_torrent(magnet_params);

  bool download_completed = false;
  std::string error_message;
  while (true) {
    if (download_completed) break;
		std::vector<lt::alert*> alerts;
		magnet_session.pop_alerts(&alerts);

		for (lt::alert const* a : alerts) {
      std::cout << "\r Alert: " << a->message() << std::endl;

      if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
				download_completed = true;
				break;
			}

			if (lt::alert_cast<lt::torrent_error_alert>(a)) {
        error_message = a->message();
        download_completed = true;
				break;
			}

			if (auto st = lt::alert_cast<lt::state_update_alert>(a)) {
				if (st->status.empty()) continue;

				// we only have a single torrent, so we know which one
				// the status is for
				lt::torrent_status const& s = st->status[0];
				std::cout << "\r" << state(s.state) << " "
					<< (s.download_payload_rate / 1000) << " kB/s "
					<< (s.total_done / 1000) << " kB ("
					<< (s.progress_ppm / 10000) << "%) downloaded ("
					<< s.num_peers << " peers)\x1b[K" << std::endl;
				std::cout.flush();
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		// ask the session to post a state_update_alert, to update our
		// state output for the torrent
		magnet_session.post_torrent_updates();
	}
  if (!error_message.empty()) {
    return maybe_torrent_info_t { 0, error_message };
  }
  return maybe_torrent_info_t { h.torrent_file(), "" };
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

int main(int argc, char const* argv[])
{
  if (argc < 3) {
    fprintf(stderr, "usage: torrent-s3 [-u <torrent-url>] [-t <torrent-file>] [-m <magnet-link>] [-d <download-path>] [-l <limit-size-bytes>]\n");
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
  auto download_path = args.getCmdOption("-d");
  if (download_path.empty()) {
    download_path = ".";
  }
  unsigned long long limit_size_bytes = LLONG_MAX;
  auto limit_size_str = args.getCmdOption("-l");
  if (!limit_size_str.empty()) {
    limit_size_bytes = std::stoull(limit_size_str);
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

  lt::add_torrent_params atp;
  atp.save_path = download_path;

  try {
    if (use_magnet) {
      fprintf(stderr, "Loading magnet link metadata\n");
      auto ti = load_magnet_link_info(source);
      if (!ti.error.empty()) {
        fprintf(stderr, "Error during downloading magnet link info: %s\n", ti.error.c_str());
        return 1;
      }
      atp.ti = std::const_pointer_cast<lt::torrent_info>(ti.info);
    }
    if (!use_url && !use_magnet) {
      atp.ti = std::make_shared<lt::torrent_info>(source);
    }
  } catch (std::exception e) {
    fprintf(stderr, "Failed to load torrent info\n");
    return 1;
  }

  if (use_url) {
    fprintf(stderr, "Downloading torrents from URL is not currently supported\n");
    return 1;
  }

  auto hashlist = load_hashlist(download_path);
  std::unordered_map<std::string, file_hash_info_t> new_hashlist;

  std::vector<file_info_t> files;
  int file_count = atp.ti->num_files();
  for (auto file_index: atp.ti->files().file_range()) {
    const auto file_size = atp.ti->files().file_size(file_index);
    const auto file_name = atp.ti->files().file_name(file_index).to_string();
    std::vector<std::vector<char>> loaded_file_hashes;
    if (hashlist.count(file_name) > 0) {
      const auto loaded_file_info = hashlist[file_name];
      loaded_file_hashes = loaded_file_info.hashes;
    }
    std::vector<std::vector<char>> torrent_file_hashes;
    const auto range = file_piece_range(atp.ti->files(), file_index);
    for (lt::piece_index_t pi = std::get<0>(range); pi != std::get<1>(range); pi++) {
      const auto piece_hash = atp.ti->hash_for_piece(pi);
      const auto hash_vector = std::vector<char>(piece_hash.data(), piece_hash.data() + piece_hash.size());
      torrent_file_hashes.push_back(hash_vector);
    }

    new_hashlist[file_name] = file_hash_info_t {torrent_file_hashes};
    if (torrent_file_hashes.size() != loaded_file_hashes.size()) {
      files.push_back(file_info_t { (unsigned long long) file_size, WAITING });
      continue;
    }
    bool equal = true;
    for (auto i = 0; i < std::max(torrent_file_hashes.size(), loaded_file_hashes.size()); i++) {
      if (torrent_file_hashes[i] != loaded_file_hashes[i]) {
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

  bool download_completed = true;

  // only download files, that are in 'to_download_indexes'
  std::vector<libtorrent::download_priority_t> file_priorities(file_count, libtorrent::dont_download);
  for (auto i: to_download_indexes) {
    file_priorities[i] = libtorrent::default_priority;
    files[i].status = DOWNLOADING;
    // at least on file needs download
    download_completed = false;
  }

  lt::session session;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert::error_notification | lt::alert::status_notification | lt::alert::file_progress_notification);
  session.apply_settings(p);

  fprintf(stderr, "Downloading torrent\n");

  lt::torrent_handle h = session.add_torrent(atp);
  std::string error_message = "";
  ThreadSafeDeque<s3_task_event_t> upload_files_queue;
  // use lambda to MSVC workaround
  std::thread s3_task_handle([&](){ s3_upload_task(upload_files_queue); });

	while (true) {
    if (download_completed) {
      break;
    }
		std::vector<lt::alert*> alerts;
		session.pop_alerts(&alerts);

		for (lt::alert const* a : alerts) {
			if (lt::alert_cast<lt::torrent_error_alert>(a)) {
				download_completed = true;
        error_message = a->message();
				break;
			}
      if (lt::alert_cast<lt::file_completed_alert>(a)) {
        auto event = lt::alert_cast<lt::file_completed_alert>(a);
        int file_index = (int)event->index;
        files[file_index].status = COMPLETED;
        std::cout << "File #" << file_index + 1 << " completed" << std::endl;

        upload_files_queue.push_back(s3_task_event_t { false, path_join(download_path, atp.ti->files().file_path(event->index)) });

        if (is_download_complete(files)) {
          download_completed = true;
          break;
        }

        // now we can allow additional downloads
        auto to_download_indexes = next_downloadable_indexes(files, limit_size_bytes);
        for (auto i: to_download_indexes) {
          file_priorities[i] = libtorrent::default_priority;
          files[i].status = DOWNLOADING;
        }
        h.prioritize_files(file_priorities);
        continue;
			}

			if (auto st = lt::alert_cast<lt::state_update_alert>(a)) {
				if (st->status.empty()) continue;

				lt::torrent_status const& s = st->status[0];
				std::cout << "\r" << state(s.state) << " "
					<< (s.download_payload_rate / 1000) << " kB/s "
					<< s.num_peers << " peers)\x1b[K" << std::endl;
				std::cout.flush();
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		// ask the session to post a state_update_alert, to update our
		// state output for the torrent
		session.post_torrent_updates();
	}

  upload_files_queue.push_back(s3_task_event_t { true, "" });

  if (!error_message.empty()) {
    fprintf(stderr, "Error during downloading torrent file: %s\n", error_message.c_str());
    return 1;
  }

  fprintf(stderr, "Downloading torrent completed\n");

  s3_task_handle.join();
  fprintf(stderr, "S3 upload completed\n");

  for (const auto &h: hashlist) {
    if (new_hashlist.count(h.first) == 0) {
      // This file was deleted - remove from S3
      fprintf(stderr, "File \"%s\" was deleted. Remove from S3\n", h.first.c_str());
    }
  }

  // save updated hashlist
  save_hashlist(download_path, new_hashlist);
  return 0;
}