#include <filesystem>
#include <vector>

#include "../archive/archive.hpp"
#include "../path/path_utils.hpp"

#include "./sync.hpp"

static std::unordered_set<std::string> filter_complete_files(const std::unordered_set<std::string>& files, const AppState &state) {
    std::unordered_set<std::string> ret;
    for (const auto &f : files) {
        const auto status = state.get_file_status(f);
        if (status != file_status_t::FILE_STATUS_READY) {
            ret.insert(f);
        }
    }
    return ret;
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

static std::string strip_prefix(const std::string &from, const std::string &prefix) {
    return from.find(prefix) == 0 ? from.substr(prefix.size()) : from;
}

void AppSync::init_downloading() {
    const auto ti = torrent_downloader->get_torrent_info();
    const auto new_files_set = get_updated_files(ti, app_state->get_hashlist());
    const auto new_files_set_filtered = filter_complete_files(new_files_set, *app_state);
    // TODO: check if files have been deleted from S3
    // TODO: erase updated files from the state

    std::vector new_files(new_files_set_filtered.begin(), new_files_set_filtered.end());

    downloading_files = std::make_shared<DownloadingFiles>(ti, new_files, limit_size);
    folders = std::make_shared<LinkedFiles>();
    populate_folders(*folders, new_files);

    download_error = false;
    has_uploading_files = false;
    file_errors.clear();
}

AppSync::AppSync(
    std::shared_ptr<AppState> app_state_,
    std::shared_ptr<S3Uploader> s3_uploader_,
    std::shared_ptr<TorrentDownloader> torrent_downloader_,
    unsigned long long limit_size_bytes,
    std::string download_path_,
    bool extract_files_,
    bool archive_files_) :
    app_state {app_state_},
    s3_uploader {s3_uploader_},
    torrent_downloader {torrent_downloader_},
    download_path {download_path_},
    extract_files {extract_files_},
    archive_files {archive_files_},
    limit_size {limit_size_bytes},
    download_error {false},
    has_uploading_files {false},
    file_errors {}
{
    init_downloading();
}

std::optional<std::string> AppSync::start() {
    init_downloading();

    torrent_downloader->start();
    const auto s3_start_ret = s3_uploader->start();
    if (s3_start_ret.has_value()) {
        return s3_start_ret.value();
    }
    const auto chunk = downloading_files->download_next_chunk();
    torrent_downloader->download_files(chunk);
    return std::nullopt;
}

std::vector<file_upload_error_t> AppSync::stop() {
    torrent_downloader->stop();
    s3_uploader->stop();
    return file_errors;
}

std::variant<std::string, std::vector<file_upload_error_t>> AppSync::full_sync() {
    const auto sync_start_ret = start();
    if (sync_start_ret.has_value()) {
        return sync_start_ret.value();
    }

    auto &download_progress = torrent_downloader->get_progress_queue();
    auto &upload_progress = s3_uploader->get_progress_queue();
    while(true) {
        if (is_completed()) break;
        while (!download_progress.empty()) {
            const auto torrent_event = download_progress.pop_front_waiting();
            if (std::holds_alternative<TorrentProgressDownloadError>(torrent_event)) {
                const auto torrent_error = std::get<TorrentProgressDownloadError>(torrent_event);
                fprintf(stderr, "Error during downloading torrent files: %s\n", torrent_error.error.c_str());
                process_torrent_error(torrent_error.error);
                continue;
            }
            const auto torrent_file_downloaded = std::get<TorrentProgressDownloadOk>(torrent_event);
            process_torrent_file(torrent_file_downloaded.file_name);
            continue;
        }
        while (!upload_progress.empty()) {
            const auto s3_event = upload_progress.pop_front_waiting();
            if (std::holds_alternative<S3ProgressUploadError>(s3_event)) {
                const auto s3_error = std::get<S3ProgressUploadError>(s3_event);
                fprintf(stderr, "Error during uploading files to S3: %s\n", s3_error.error.c_str());
                process_s3_file_error(s3_error.file_name, s3_error.error);
                continue;
            }
            const auto s3_file_uploaded = std::get<S3ProgressUploadOk>(s3_event);
            process_s3_file(s3_file_uploaded.file_name);
            continue;
        }
    }

    fprintf(stdout, "Downloading torrent completed\n");
    update_hashlist();
    return stop();
}

void AppSync::process_torrent_file(std::string file_name) {
    const auto file_name_full = std::filesystem::path(download_path) / file_name;
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
                std::transform(filtered_files.begin(), filtered_files.end(), std::back_inserter(linked_file_names), [this](const file_unpack_info_t &f) {
                    auto linked_file_stripped = strip_prefix(path_to_relative(f.name, download_path).string(), "./");
                    linked_file_stripped = strip_prefix(linked_file_stripped, ".\\");
                    return linked_file_stripped;
                });
                // erase archive after extraction
                std::filesystem::remove(std::filesystem::u8path(file_name_str));
                folders->remove_child(file_name);
            }
            populate_folders(*folders, linked_file_names);
        }
    }
    app_state->add_uploading_files(file_name, linked_file_names);
    // upload parent file if linked files are empty
    if (linked_file_names.empty()) {
        has_uploading_files = true;
        auto should_archive = false;
        if (!is_packed(file_name) && archive_files) {
            should_archive = true;
        }
        s3_uploader->new_file(file_name, should_archive);
    }
    // upload linked files
    for (const auto &f: linked_file_names) {
        has_uploading_files = true;
        auto should_archive = false;
        if (!is_packed(f) && archive_files) {
            should_archive = true;
        }
        s3_uploader->new_file(f, should_archive);
    }
}

void AppSync::process_torrent_error(std::string error_message) {
    download_error = true;
    torrent_downloader->stop();
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

static void s3_file_upload_complete(const std::filesystem::path path_from, LinkedFiles &folders, const std::string relative_filename, DownloadingFiles &downloading_files, AppState &state) {
    const auto parent = state.get_uploading_parent(relative_filename);

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

// update state after uploading file to s3
void AppSync::process_s3_file(std::string file_name) {
    s3_file_upload_complete(download_path, *folders, file_name, *downloading_files, *app_state);
    if (app_state->get_uploading_files().empty()) {
        has_uploading_files = false;
    }

    if (download_error) {
        return;
    }

    // if some linked files are not uploaded yet, don't start next download
    const auto maybe_parent = app_state->get_uploading_parent(file_name);
    if (maybe_parent.has_value()) {
        return;
    }
    // check for completed downloads only on S3 events for optimization purpose
    const auto next_chunk = downloading_files->download_next_chunk();
    if (next_chunk.empty()) {
        return;
    }
    torrent_downloader->download_files(next_chunk);
}

void AppSync::process_s3_file_error(std::string file_name, std::string error_message) {
    file_errors.push_back(file_upload_error_t { file_name, error_message });
    // process as completed to avoid infinite loop
    s3_file_upload_complete(download_path, *folders, file_name, *downloading_files, *app_state);
    if (app_state->get_uploading_files().empty()) {
        has_uploading_files = false;
    }
}

bool AppSync::is_completed() const {
    return (downloading_files->is_completed() || download_error) && !has_uploading_files;
}

void AppSync::update_hashlist() {
    const auto ti = torrent_downloader->get_torrent_info();
    auto new_hashlist = create_hashlist(ti, app_state->get_completed_files());
    // remove files with errors from hashlist
    for (const auto &f : file_errors) {
        new_hashlist.erase(f.file_name);
    }
    app_state->save_hashlist(new_hashlist);
}