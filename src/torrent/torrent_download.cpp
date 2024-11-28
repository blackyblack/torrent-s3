#include <iostream>
#include <ctime>

#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>

#include "./torrent_download.hpp"

// Workaround for stale torrent metadata. Will retry if no new peers found in STALE_TIMEOUT_SECONDS period.
#define STALE_TIMEOUT_SECONDS 60
// Up to STALE_RETRIES for stale torrent metadata.
#define STALE_RETRIES 5

TorrentError::TorrentError(std::string message) : std::runtime_error(message.c_str()) {}

// return the name of a torrent status enum
static char const* state(lt::torrent_status::state_t s) {
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

static bool is_download_complete(const std::vector<file_info_t> &files) {
  for (const auto &f: files) {
    if (f.status != COMPLETED && f.status != S3_ERROR) return false;
  }
  return true;
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

lt::torrent_info load_magnet_link_info(const std::string magnet_link) {  
  auto magnet_params = lt::parse_magnet_uri(magnet_link);
  magnet_params.save_path = ".";
  magnet_params.flags |= lt::torrent_flags::default_dont_download;
  unsigned int stale_download_retries = 0;

  while(true) {
    lt::session magnet_session;
    lt::settings_pack p;
    p.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status);
    magnet_session.apply_settings(p);
    lt::torrent_handle h = magnet_session.add_torrent(magnet_params);
    std::time_t stale_timeout_start = std::time(0);

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

          // we only have a single torrent, so we know which one the status is for
          lt::torrent_status const& s = st->status[0];
          std::cout << "\r" << state(s.state) << " "
            << (s.download_payload_rate / 1000) << " kB/s "
            << (s.total_done / 1000) << " kB ("
            << (s.progress_ppm / 10000) << "%) downloaded ("
            << s.num_peers << " peers)\x1b[K" << std::endl;
          std::cout.flush();

          if (s.num_peers > 0) {
            stale_timeout_start = std::time(0);
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      // ask the session to post a state_update_alert, to update our
      // state output for the torrent
      magnet_session.post_torrent_updates();

      std::time_t stale_timeout_end = std::time(0);

      if (stale_timeout_end >= stale_timeout_start + STALE_TIMEOUT_SECONDS) {
        fprintf(stderr, "Stale magnet link info. Retrying...\n");
        break;
      }
    }

    // Try to restart magnet link metadata download
    stale_download_retries++;
    if (stale_download_retries > STALE_RETRIES) {
      throw TorrentError("Stale magnet link metadata");
    }
    magnet_session.abort();
  }
}

std::vector<file_error_info_t> download_torrent_files(
  const lt::add_torrent_params& params,
  std::vector<file_info_t> &files,
  S3Uploader &uploader,
  unsigned long long limit_size_bytes
) {
  lt::add_torrent_params torrent_params(std::move(params));
  const int file_count = params.ti->num_files();
  auto to_download_indexes = next_downloadable_indexes(files, limit_size_bytes);
  std::vector<file_error_info_t> file_errors;

  // only download files, that are in 'file_indexes'
  std::vector<lt::download_priority_t> file_priorities(file_count, libtorrent::dont_download);
  for (const auto i: to_download_indexes) {
    file_priorities[i] = libtorrent::default_priority;
  }
  torrent_params.file_priorities = file_priorities;

  bool download_completed = true;
  for (const auto &p: torrent_params.file_priorities) {
    if (p != libtorrent::dont_download) {
      // at least on file needs download
      download_completed = false;
      break;
    }
  }

  if (download_completed || file_count == 0) {
    return file_errors;
  }

  lt::session session;
  lt::settings_pack p;
  p.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status | lt::alert_category::file_progress);
  session.apply_settings(p);

  fprintf(stdout, "Downloading torrent\n");

  lt::torrent_handle h = session.add_torrent(torrent_params);
  auto &s3_progress = uploader.get_progress_queue();

	while (true) {
    if (download_completed) {
      break;
    }

    // process S3 events before moving on to torrent events
    while (true) {
      if (s3_progress.empty()) {
        break;
      }

      const auto s3_event = s3_progress.pop_front_waiting();
      files[s3_event.file_index].status = s3_event.message_type == progress_type_t::UPLOAD_OK ? COMPLETED : S3_ERROR;

      if (s3_event.message_type == progress_type_t::UPLOAD_ERROR) {
        file_errors.push_back(file_error_info_t {s3_event.file_index, s3_event.error});
      }

      // check for completed downloads only on S3 events for optimization purpose
      if (is_download_complete(files)) {
        download_completed = true;
      }

      // now we can allow additional downloads
      auto to_download_indexes = next_downloadable_indexes(files, limit_size_bytes);
      if (to_download_indexes.size() == 0) {
        continue;
      }

      auto file_priorities = h.get_file_priorities();
      for (auto i: to_download_indexes) {
        file_priorities[i] = libtorrent::default_priority;
        files[i].status = DOWNLOADING;
      }
      h.prioritize_files(file_priorities);
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
        int file_index = (int) event->index;

        std::cout << "File #" << file_index + 1 << " downloaded" << std::endl;

        const auto file_name = h.torrent_file()->files().file_path((lt::file_index_t) file_index);
        uploader.new_file(file_name, file_index);
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
  return file_errors;
}