#pragma once

#include <vector>
#include <thread>
#include <variant>
#include <optional>
#include <filesystem>

#include <miniocpp/client.h>

#include "../deque/deque.hpp"

struct S3TaskEventTerminate {};

struct S3TaskEventNewFile {
    std::string file_name;
    bool should_archive; // if true, file will be zipped before upload
};

typedef std::variant<S3TaskEventTerminate, S3TaskEventNewFile> S3TaskEvent;

struct S3ProgressUploadOk {
    std::string file_name;
};

struct S3ProgressUploadError {
    std::string file_name;
    std::string error;
};

typedef std::variant<S3ProgressUploadOk, S3ProgressUploadError> S3ProgressEvent;

class S3Uploader {
public:
    // use default thread count (16) if thread_count is set to 0
    // path_from_ - where we store files
    // path_to_ - where to upload them
    S3Uploader(
        unsigned int thread_count_,
        const std::string &url_,
        const std::string &access_key_,
        const std::string &secret_key_,
        const std::string &bucket_,
        const std::string &region_,
        const std::filesystem::path &path_from_,
        const std::filesystem::path &path_to_
    );

    std::optional<std::string> start();
    void stop();
    // progress_queue allows to receive notifications on upload progress
    ThreadSafeDeque<S3ProgressEvent> &get_progress_queue();
    void new_file(const std::string &file_name, bool should_archive = false);
    // does not require S3 uploader to be started
    std::optional<std::string> delete_file(const std::string &file_name);
    // returns either file exists or an error message
    // does not require S3 uploader to be started
    // delete marker is not supported
    std::variant<bool, std::string> is_file_existing(const std::string &file_name);
private:
    ThreadSafeDeque<S3TaskEvent> message_queue;
    ThreadSafeDeque<S3ProgressEvent> progress_queue;
    unsigned int thread_count;

    // S3 credentials
    const std::string url;
    const std::string access_key;
    const std::string secret_key;
    const std::string bucket;
    const std::string region;

    const std::filesystem::path path_from;
    const std::filesystem::path path_to;
    std::unique_ptr<minio::creds::Provider> provider;
    std::unique_ptr<minio::s3::Client> client;

    std::vector<std::thread> tasks;
};
