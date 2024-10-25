#include <iostream>
#include <fstream>

#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>

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

  lt::session ses;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert::all_categories);
  ses.apply_settings(p);

  lt::add_torrent_params atp;
  if (use_magnet) {
    atp = lt::parse_magnet_uri(source);
  }
  if (!use_url && !use_magnet) {
    atp.ti = std::make_shared<lt::torrent_info>(source);
  }
  if (use_url) {
    fprintf(stderr, "Downloading torrents from URL is not currently supported\n");
    return 1;
  }
  atp.save_path = download_path;

  // this is how to stop some files from downloading
  //atp.file_priorities = std::vector<libtorrent::download_priority_t>{libtorrent::default_priority, libtorrent::dont_download};

  lt::torrent_handle h = ses.add_torrent(atp);
  int completed = 0;

	for (;;) {
		std::vector<lt::alert*> alerts;
		ses.pop_alerts(&alerts);

		for (lt::alert const* a : alerts) {
			// if we receive the finished alert or an error, we're done
			if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
				break;
			}
			if (lt::alert_cast<lt::torrent_error_alert>(a)) {
				std::cout << a->message() << std::endl;
				break;
			}
      if (lt::alert_cast<lt::file_completed_alert>(a)) {
        std::cout << "File #" << lt::alert_cast<lt::file_completed_alert>(a)->index << " completed" << std::endl;
        completed++;

        if (completed > 5) {
          h.prioritize_files(std::vector<libtorrent::download_priority_t>{libtorrent::default_priority});
        }
			}

			if (auto st = lt::alert_cast<lt::state_update_alert>(a)) {
				if (st->status.empty()) continue;

				// we only have a single torrent, so we know which one
				// the status is for
				lt::torrent_status const& s = st->status[0];
				std::cout << '\r' << state(s.state) << ' '
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
		ses.post_torrent_updates();
	}
}