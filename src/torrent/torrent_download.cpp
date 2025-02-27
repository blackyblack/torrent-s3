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

// return the name of a torrent status enum
static char const* state(lt::torrent_status::state_t s) {
    switch(s) {
    case lt::torrent_status::checking_files:
        return "checking";
    case lt::torrent_status::downloading_metadata:
        return "dl metadata";
    case lt::torrent_status::downloading:
        return "downloading";
    case lt::torrent_status::finished:
        return "finished";
    case lt::torrent_status::seeding:
        return "seeding";
    case lt::torrent_status::checking_resume_data:
        return "checking resume";
    default:
        return "<>";
    }
}

std::variant<lt::torrent_info, std::string> load_magnet_link_info(const std::string magnet_link) {
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
                    return a->message();
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
            return std::string("Stale magnet link metadata");
        }
        magnet_session.abort();
    }
}

static std::unordered_map<std::string, unsigned int> get_file_indexes(const lt::torrent_info &torrent) {
    std::unordered_map<std::string, unsigned int> file_indexes;
    for (const auto &file_index: torrent.files().file_range()) {
        const auto file_name = torrent.files().file_path(file_index);
        file_indexes[file_name] = (int) file_index;
    }
    return file_indexes;
}

static void download_task(
    ThreadSafeDeque<TorrentProgressEvent> &progress_queue,
    ThreadSafeDeque<TorrentTaskEvent> &message_queue,
    const lt::add_torrent_params& torrent_params
) {
    fprintf(stdout, "Starting Torrent download upload task\n");

    lt::session session;
    lt::settings_pack p;
    p.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status | lt::alert_category::file_progress);
    session.apply_settings(p);

    lt::add_torrent_params params { torrent_params };
    params.file_priorities = std::vector<lt::download_priority_t>(params.ti->num_files(), libtorrent::dont_download);

    lt::torrent_handle torrent_handle = session.add_torrent(params);
    bool download_error = false;
    bool stop_download = false;
    std::set<unsigned int> downloaded_indexes;
    std::set<unsigned int> requested_indexes;

    const auto files = get_file_indexes(*torrent_handle.torrent_file());

    while (true) {
        if (download_error) {
            break;
        }
        if (stop_download) {
            std::set<unsigned int> intersect;
            std::set_intersection(requested_indexes.begin(), requested_indexes.end(), downloaded_indexes.begin(), downloaded_indexes.end(),
                 std::inserter(intersect, intersect.begin()));
            if (intersect == requested_indexes) {
                break;
            }
        }
        while (!message_queue.empty()) {
            const auto event = message_queue.pop_front_waiting();
            if (std::holds_alternative<TorrentTaskEventTerminate>(event)) {
                stop_download = true;
                break;
            }
            const auto file_event = std::get<TorrentTaskEventNewFile>(event);
            const auto filename = file_event.file_name;
            if (files.count(filename) == 0) {
                continue;
            }
            const auto file_index = files.at(filename);
            requested_indexes.insert(file_index);
            torrent_handle.file_priority(lt::file_index_t {(int) file_index}, libtorrent::default_priority);
            // check if it was already downloaded
            if (downloaded_indexes.count(file_index) > 0) {
                progress_queue.push_back(TorrentProgressDownloadOk { filename, file_index });
            }
            continue;
        }

        std::vector<lt::alert*> alerts;
        session.pop_alerts(&alerts);

        for (lt::alert const* a : alerts) {
            if (lt::alert_cast<lt::torrent_error_alert>(a)) {
                progress_queue.push_back(TorrentProgressDownloadError { a->message() });
                download_error = true;
                break;
            }
            if (lt::alert_cast<lt::file_completed_alert>(a)) {
                auto event = lt::alert_cast<lt::file_completed_alert>(a);
                unsigned int file_index = (int) event->index;

                std::cout << "File #" << file_index + 1 << " downloaded" << std::endl;
                downloaded_indexes.insert(file_index);

                if (requested_indexes.count(file_index) > 0) {
                    const auto file_name = torrent_handle.torrent_file()->files().file_path(lt::file_index_t {(int) file_index});
                    progress_queue.push_back(TorrentProgressDownloadOk { file_name, file_index });
                }
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

    fprintf(stdout, "Torrent dowload task completed\n");
}

TorrentDownloader::TorrentDownloader(const lt::add_torrent_params& params) :
    torrent_params {params} {
    const int file_count = torrent_params.ti->num_files();
    torrent_params.file_priorities = std::vector<lt::download_priority_t>(file_count, libtorrent::dont_download);
}

void TorrentDownloader::start() {
    task = std::thread([&]() {
        download_task(progress_queue, message_queue, torrent_params);
    });
}

void TorrentDownloader::stop() {
    message_queue.push_back(TorrentTaskEventTerminate {});
    task.join();
}

ThreadSafeDeque<TorrentProgressEvent> &TorrentDownloader::get_progress_queue() {
    return progress_queue;
}

void TorrentDownloader::download_files(const std::vector<std::string> &files) {
    // There is no check if file exists in the torrent. Make sure to get the file paths
    // from the torrent_info object.
    for (const auto &f: files) {
        message_queue.push_back(TorrentTaskEventNewFile { f });
    }
}
