#pragma once

#include <memory>
#include <string>
#include <optional>
#include <variant>

#include "../app_state/state.hpp"
#include "../s3/s3.hpp"
#include "../downloading_files/downloading_files.hpp"
#include "../torrent/torrent_download.hpp"
#include "../linked_files/linked_files.hpp"

struct file_upload_error_t {
    std::string file_name;
    std::string error_message;
};

// NOTE: AppSync is not thread-safe

class AppSync {
public:
    AppSync(
        std::shared_ptr<AppState> app_state_,
        std::shared_ptr<S3Uploader> s3_uploader_,
        std::shared_ptr<TorrentDownloader> torrent_downloader_,
        unsigned long long limit_size_bytes,
        std::string download_path_,
        bool extract_files_,
        bool archive_files_);

    // start sync by selecting next chunk and downloading it
    // optionally returns an error
    std::optional<std::string> start();

    // waits for sync to complete and terminate all tasks
    // returns a list of errors
    std::vector<file_upload_error_t> stop();

    // starts sync, waits for completion and stops
    // either returns an error or a list of errors
    std::variant<std::string, std::vector<file_upload_error_t>> full_sync();

    // update state after downloading file from a torrent
    void process_torrent_file(std::string file_name);

    // update state after torrent error
    void process_torrent_error(std::string error_message);

    // update state after uploading file to s3
    void process_s3_file(std::string file_name);

    // update state after failed uploading file to s3
    void process_s3_file_error(std::string file_name, std::string error_message);

    void update_hashlist();

    // true if sync is completed
    bool is_completed() const;

protected:
    void init_downloading();

private:
    std::shared_ptr<AppState> app_state;
    std::shared_ptr<DownloadingFiles> downloading_files;
    std::shared_ptr<LinkedFiles> folders;
    std::shared_ptr<S3Uploader> s3_uploader;
    std::shared_ptr<TorrentDownloader> torrent_downloader;
    std::string download_path;
    bool extract_files;
    bool archive_files;
    unsigned long long limit_size;
    bool download_error;
    bool has_uploading_files;
    std::vector<file_upload_error_t> file_errors;
};
