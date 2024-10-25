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
  p.set_int(lt::settings_pack::alert_mask, lt::alert::all_categories);
  magnet_session.apply_settings(p);
  lt::torrent_handle h = magnet_session.add_torrent(magnet_params);

  bool download_completed = false;
  std::string error_message;
  while (true) {
    if (download_completed) break;
		std::vector<lt::alert*> alerts;
		magnet_session.pop_alerts(&alerts);

		for (lt::alert const* a : alerts) {
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
    fprintf(stderr, "usage: torrent-s3 [-u <torrent-url>] [-f <torrent-file>] [-m <magnet-link>] [-d <download-path>] [-l <limit-size-bytes>]\n");
    return 1;
  }
  InputParser args(argc, argv);
  auto torrent_url = args.getCmdOption("-u");
  auto torrent_file = args.getCmdOption("-f");
  auto magnet_link = args.getCmdOption("-m");
  bool use_magnet = false;
  bool use_url = false;
  std::string source;
  if (torrent_url.empty() && torrent_file.empty() && magnet_link.empty()) {
    fprintf(stderr, "Torrent URL is not set.\nUse -u <torrent-url> to set URL for .torrent file.\nUse -f <torrent-file> to set path for .torrent file.\nUse -m <magnet-link> to set magnet link\n");
    return 1;
  }
  if (!torrent_url.empty() && (!torrent_file.empty() || !magnet_link.empty())) {
    fprintf(stderr, "Using torrent URL for downloading. Ignoring other torrent file sources\n");
    source = torrent_url;
  }
  if (torrent_url.empty() && !torrent_file.empty() && !magnet_link.empty()) {
    fprintf(stderr, "Using torrent file for downloading. Ignoring other torrent file sources\n");
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
  if (use_url) {
    fprintf(stderr, "Downloading torrents from URL is not currently supported\n");
    return 1;
  }

  std::vector<file_info_t> files;
  for (auto file_index: atp.ti->files().file_range()) {
    auto file_size = atp.ti->files().file_size(file_index);
    files.push_back(file_info_t { (unsigned long long)file_size, WAITING });
  }

  auto to_download_indexes = next_downloadable_indexes(files, limit_size_bytes);

  // only download files, that are in 'to_download_indexes'
  std::vector<libtorrent::download_priority_t> file_priorities(files.size(), libtorrent::dont_download);
  for (auto i: to_download_indexes) {
    file_priorities[i] = libtorrent::default_priority;
    files[i].status = DOWNLOADING;
  }
  atp.file_priorities = file_priorities;

  lt::session session;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert::all_categories);
  session.apply_settings(p);

  fprintf(stderr, "Downloading torrent\n");

  lt::torrent_handle h = session.add_torrent(atp);
  bool download_completed = false;
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

  return 0;
}