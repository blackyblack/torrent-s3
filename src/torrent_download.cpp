#include <iostream>

#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/alert_types.hpp>

#include "../include/torrent_download.hpp"

static std::function<bool(lt::torrent_handle &, unsigned int)> downloaded_file_event_handler;

// return the name of a torrent status enum
static char const* state(lt::torrent_status::state_t s)
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

TorrentError::TorrentError(std::string message) : std::runtime_error(message.c_str()) {}

lt::torrent_info load_magnet_link_info(const std::string magnet_link) {
  auto magnet_params = lt::parse_magnet_uri(magnet_link);
  magnet_params.save_path = ".";
  magnet_params.flags |= lt::torrent_flags::default_dont_download;
  lt::session magnet_session;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert::error_notification | lt::alert::status_notification);
  magnet_session.apply_settings(p);
  lt::torrent_handle h = magnet_session.add_torrent(magnet_params);

  while (true) {
		std::vector<lt::alert*> alerts;
		magnet_session.pop_alerts(&alerts);

		for (const auto a : alerts) {
      if (lt::alert_cast<lt::torrent_finished_alert>(a)) {
				return *h.torrent_file();
			}

			if (lt::alert_cast<lt::torrent_error_alert>(a)) {
        throw TorrentError(a->message());
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
}

void subscribe_downloaded_file(std::function<bool(lt::torrent_handle &, unsigned int)> event) {
  downloaded_file_event_handler = event;
}

void download_torrent_files(const lt::add_torrent_params& params) {
  const int file_count = params.ti->num_files();
  bool download_completed = true;

  for (const auto &p: params.file_priorities) {
    if (p != libtorrent::dont_download) {
      // at least on file needs download
      download_completed = false;
      break;
    }
  }

  if (download_completed || file_count == 0) {
    return;
  }

  lt::session session;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert::error_notification | lt::alert::status_notification | lt::alert::file_progress_notification);
  session.apply_settings(p);

  fprintf(stderr, "Downloading torrent\n");

  lt::torrent_handle h = session.add_torrent(params);

	while (true) {
    if (download_completed) {
      break;
    }
		std::vector<lt::alert*> alerts;
		session.pop_alerts(&alerts);

		for (lt::alert const* a : alerts) {
			if (lt::alert_cast<lt::torrent_error_alert>(a)) {
				throw TorrentError(a->message());
			}
      if (lt::alert_cast<lt::file_completed_alert>(a)) {
        auto event = lt::alert_cast<lt::file_completed_alert>(a);
        int file_index = (int)event->index;
        if (downloaded_file_event_handler) {
          download_completed = downloaded_file_event_handler(h, file_index);
          continue;
        }
        // if downloaded_file_event_handler is not set
        bool to_download = false;
        for (const auto &i : h.get_file_priorities()) {
          if (i != lt::dont_download) {
            to_download = true;
            break;
          }
        }
        download_completed = !to_download;
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
}