#include <iostream>
#include <sstream>
#include <fstream>

#include "../backoffxx/backoffxx.h"

#include "./s3.hpp"

// how many S3 upload tasks to run simultaneously
#define TASKS_COUNT_DEFAULT 16

#define RANDOM_FILE_NAME_LENGTH 16

#define RETRIES 5
#define INITIAL_DELAY_SECONDS 5
#define MAX_DELAY_SECONDS 60

S3Uploader::S3Uploader(
    unsigned int thread_count_,
    const std::string &url_,
    const std::string &access_key_,
    const std::string &secret_key_,
    const std::string &bucket_,
    const std::string &region_,
    const std::filesystem::path &path_from_,
    const std::filesystem::path &path_to_
) :
    thread_count {thread_count_},
    url {url_},
    access_key {access_key_},
    secret_key {secret_key_},
    bucket {bucket_},
    region {region_},
    path_from {path_from_},
    path_to {path_to_} {
    if (!thread_count) {
        thread_count = TASKS_COUNT_DEFAULT;
    }

    minio::s3::BaseUrl base_url(url);
    provider = std::make_unique<minio::creds::StaticProvider>(access_key, secret_key);
    client = std::make_unique<minio::s3::Client>(base_url, provider.get());
}

static std::string gen_random(const int len) {
    static const char alphanum[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string tmp_s;
    tmp_s.reserve(len);
    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return tmp_s;
}

static std::string replace(std::string subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
    return subject;
}

static std::optional<std::string> write_stream_s3(std::istream &stream, unsigned long content_size, minio::s3::Client &client, const std::string &bucket, const std::string &region, const std::filesystem::path &path) {
    minio::s3::PutObjectArgs args(stream, content_size, 0);
    args.bucket = bucket;
    args.object = replace(path.string(), "\\", "/");
    if (!region.empty()) {
        args.region = region;
    }

    std::string error = "Retry limit reached";
    const auto result = backoffxx::attempt(backoffxx::make_exponential(std::chrono::seconds(INITIAL_DELAY_SECONDS), RETRIES, std::chrono::seconds(MAX_DELAY_SECONDS)), [&] {
        args.stream.clear();
        args.stream.seekg(0);
        const auto resp = client.PutObject(args);
        if (!resp) {
            // throttling - make retry
            if (resp.status_code == 429 || resp.status_code == 0) {
                return backoffxx::attempt_rc::failure;
            }
            error = resp.Error().String();
            return backoffxx::attempt_rc::hard_error;
        }
        return backoffxx::attempt_rc::success;
    });

    if (!result.ok()) {
        return error;
    }
    return std::nullopt;
}

static std::optional<std::string> write_content_to_file_s3(const std::string &content, minio::s3::Client &client, const std::string &bucket, const std::string &region, const std::filesystem::path &path) {
    const auto content_size = content.size();
    std::stringstream stream(content);
    return write_stream_s3(stream, content_size, client, bucket, region, path);
}

static std::optional<std::string> write_file_s3(const std::filesystem::path &file_path, minio::s3::Client &client, const std::string &bucket, const std::string &region, const std::filesystem::path &path) {
    std::ifstream file_stream(std::filesystem::u8path(file_path.string()), std::ios::binary);
    unsigned long file_size;
    try {
        file_size = std::filesystem::file_size(std::filesystem::u8path(file_path.string()));
    } catch (const std::filesystem::filesystem_error& e) {
        return e.what();
    }
    return write_stream_s3(file_stream, file_size, client, bucket, region, path);
}

static std::optional<std::string> delete_file_s3(minio::s3::Client &client, const std::string &bucket, const std::string &region, const std::filesystem::path &path) {
    minio::s3::RemoveObjectArgs args;
    args.bucket = bucket;
    args.object = replace(path.string(), "\\", "/");
    if (!region.empty()) {
        args.region = region;
    }

    std::string error = "Retry limit reached";
    const auto result = backoffxx::attempt(backoffxx::make_exponential(std::chrono::seconds(INITIAL_DELAY_SECONDS), RETRIES, std::chrono::seconds(MAX_DELAY_SECONDS)), [&] {
        const auto resp = client.RemoveObject(args);
        if (!resp) {
            // throttling - make retry
            if (resp.status_code == 429 || resp.status_code == 0) {
                return backoffxx::attempt_rc::failure;
            }
            error = resp.Error().String();
            return backoffxx::attempt_rc::hard_error;
        }
        return backoffxx::attempt_rc::success;
    });

    if (!result.ok()) {
        return error;
    }
    return std::nullopt;
}

static std::variant<bool, std::string> exists_bucket_s3(minio::s3::Client &client, const std::string &bucket, const std::string &region) {
    minio::s3::BucketExistsArgs args;
    args.bucket = bucket;
    if (!region.empty()) {
        args.region = region;
    }

    std::string error = "Retry limit reached";
    bool exists;
    const auto result = backoffxx::attempt(backoffxx::make_exponential(std::chrono::seconds(INITIAL_DELAY_SECONDS), RETRIES, std::chrono::seconds(MAX_DELAY_SECONDS)), [&] {
        const auto resp = client.BucketExists(args);
        if (!resp) {
            // throttling - make retry
            if (resp.status_code == 429 || resp.status_code == 0) {
                return backoffxx::attempt_rc::failure;
            }
            error = resp.Error().String();
            return backoffxx::attempt_rc::hard_error;
        }
        if (resp.exist) {
            exists = true;
        }
        return backoffxx::attempt_rc::success;
    });

    if (!result.ok()) {
        return error;
    }

    return exists;
}

static void s3_upload_task(
    ThreadSafeDeque<S3ProgressEvent> &progress_queue,
    const std::string &url,
    const std::string &access_key,
    const std::string &secret_key,
    const std::string &bucket,
    const std::string &region,
    const std::filesystem::path &path_from,
    const std::filesystem::path &path_to,
    ThreadSafeDeque<S3TaskEvent> &message_queue,
    unsigned int task_index
) {
    fprintf(stdout, "Starting S3 upload task #%u\n", task_index + 1);

    minio::s3::BaseUrl base_url(url);
    minio::creds::StaticProvider provider(access_key, secret_key);
    minio::s3::Client client(base_url, &provider);

    while (true) {
        const auto event = message_queue.pop_front_waiting();
        if (std::holds_alternative<S3TaskEventTerminate>(event)) {
            break;
        }
        const auto file_event = std::get<S3TaskEventNewFile>(event);
        const auto save_from_filename = (path_from / file_event.file_name).lexically_normal();
        fprintf(stdout, "[Task %u] Uploading %s\n", task_index + 1, save_from_filename.string().c_str());

        const auto save_to_filename = (path_to / file_event.file_name).lexically_normal();

        const auto ret = write_file_s3(save_from_filename, client, bucket, region, save_to_filename);
        if (ret.has_value()) {
            fprintf(stderr, "[Task %u] Could not upload file \"%s\". Error %s\n", task_index + 1, save_from_filename.string().c_str(), ret.value().c_str());
            progress_queue.push_back(S3ProgressUploadError { file_event.file_name, ret.value() });
            continue;
        }
        progress_queue.push_back(S3ProgressUploadOk { file_event.file_name });
    }

    fprintf(stdout, "S3 upload task #%u completed\n", task_index + 1);
}

std::optional<std::string> S3Uploader::start() {
    const auto exists_variant = exists_bucket_s3(*client, bucket, region);
    if (std::holds_alternative<std::string>(exists_variant)) {
        return std::get<std::string>(exists_variant);
    }
    const auto exists = std::get<bool>(exists_variant);
    if (!exists) {
        return std::string("Bucket \"") + bucket + std::string("\" does not exist");
    }

    std::string empty_file;
    const auto file_name = gen_random(RANDOM_FILE_NAME_LENGTH);
    const auto save_to_filename = path_to / file_name;
    const auto write_option = write_content_to_file_s3(empty_file, *client, bucket, region, save_to_filename);
    if (write_option.has_value()) {
        return std::string("Could not write to bucket \"") + bucket + std::string("\". Error: ") + write_option.value();
    }

    const auto delete_option = delete_file_s3(*client, bucket, region, save_to_filename);
    if (delete_option.has_value()) {
        return std::string("Could not delete from bucket \"") + bucket + std::string("\". Error: ") + delete_option.value();
    }

    tasks.clear();
    for (unsigned int i = 0; i < thread_count; i++) {
        // use lambda to MSVC workaround
        std::thread task([&, i]() {
            s3_upload_task(progress_queue, url, access_key, secret_key, bucket, region, path_from, path_to, message_queue, i);
        });
        tasks.push_back(std::move(task));
    }
    return std::nullopt;
}

void S3Uploader::stop() {
    for (auto i = 0; i < tasks.size(); i++) {
        message_queue.push_back(S3TaskEventTerminate {});
    }
    for (auto &t : tasks) {
        t.join();
    }
    tasks.clear();
}

ThreadSafeDeque<S3ProgressEvent> &S3Uploader::get_progress_queue() {
    return progress_queue;
}

void S3Uploader::new_file(const std::string &file_name) {
    message_queue.push_back(S3TaskEventNewFile { file_name });
}

std::optional<std::string> S3Uploader::delete_file(const std::string &file_name) {
    return delete_file_s3(*client, bucket, region, path_to / file_name);
}

std::variant<bool, std::string> S3Uploader::is_file_existing(const std::string &file_name) {
    const auto file_path = path_to / file_name;
    minio::s3::StatObjectArgs args;
    args.bucket = bucket;
    args.object = replace(file_path.string(), "\\", "/");
    if (!region.empty()) {
        args.region = region;
    }

    std::string error = "Retry limit reached";
    minio::s3::StatObjectResponse response;
    const auto result = backoffxx::attempt(backoffxx::make_exponential(std::chrono::seconds(INITIAL_DELAY_SECONDS), RETRIES, std::chrono::seconds(MAX_DELAY_SECONDS)), [&] {
        response = client->StatObject(args);
        if (!response) {
            // throttling - make retry
            if (response.status_code == 429 || response.status_code == 0) {
                return backoffxx::attempt_rc::failure;
            }
            error = response.Error().String();
            return backoffxx::attempt_rc::hard_error;
        }
        return backoffxx::attempt_rc::success;
    });

    if (!result.ok()) {
        if (error == "NoSuchKey: Object does not exist") {
            return false;
        }
        if (error == "NoSuchBucket: Bucket does not exist") {
            return false;
        }
        return error;
    }
    // delete marker is not processed properly by minio so we skip checking it
    return response.etag.size() > 0;
}
