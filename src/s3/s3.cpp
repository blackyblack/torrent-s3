#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "./s3.hpp"

// how many S3 upload tasks to run simultaneously
#define TASKS_COUNT_DEFAULT 16

#define RANDOM_FILE_NAME_LENGTH 16

S3Error::S3Error(std::string message) : std::runtime_error(message.c_str()) {}

message_type_t S3TaskEventTerminate::message_type() {
  return TERMINATE;
}

S3TaskEventNewFile::S3TaskEventNewFile(std::string name, unsigned int file_index_) : file_name {name}, file_index {file_index_} {}

message_type_t S3TaskEventNewFile::message_type() {
  return NEW_FILE;
}

std::string S3TaskEventNewFile::get_name() {
  return file_name;
}

unsigned int S3TaskEventNewFile::get_index() {
  return file_index;
}

S3Uploader::S3Uploader(
  const std::string &path_from_,
  unsigned int thread_count_,
  const std::string &url_,
  const std::string &access_key_,
  const std::string &secret_key_,
  const std::string &bucket_,
  const std::string &path_to_
) :
  path_from {path_from_},
  thread_count {thread_count_},
  url {url_},
  access_key {access_key_},
  secret_key {secret_key_},
  bucket {bucket_},
  path_to {path_to_}
{
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

static void write_content_to_file_s3(const std::string &content, minio::s3::Client &client, const std::string &bucket, const std::string &path) {
  const auto content_size = content.size();
  std::stringstream stream(content);
  minio::s3::PutObjectArgs args(stream, content_size, 0);
  args.bucket = bucket;
  args.object = path;

  const auto resp = client.PutObject(args);

  if (!resp) {
    throw S3Error(resp.Error().String());
  }
}

static void write_file_s3(const std::string &file_path, minio::s3::Client &client, const std::string &bucket, const std::string &path) {
  auto file_stream = std::ifstream(file_path, std::ios::binary);
  std::filesystem::path fs_path(file_path);
  const auto file_size = std::filesystem::file_size(fs_path);
  minio::s3::PutObjectArgs args(file_stream, file_size, 0);
  args.bucket = bucket;
  args.object = path;

  const auto resp = client.PutObject(args);

  if (!resp) {
    throw S3Error(resp.Error().String());
  }
}

static void delete_file_s3(minio::s3::Client &client, const std::string &bucket, const std::string &path) {
  minio::s3::RemoveObjectArgs args;
  args.bucket = bucket;
  args.object = path;

  const auto resp = client.RemoveObject(args);

  if (!resp) {
    throw S3Error(resp.Error().String());
  }
}

static void s3_upload_task(
  ThreadSafeDeque<S3ProgressEvent> &progress_queue,
  const std::string &url,
  const std::string &access_key,
  const std::string &secret_key,
  const std::string &bucket,
  const std::string &path_to,
  ThreadSafeDeque<std::shared_ptr<S3TaskEvent>> &message_queue,
  const std::string &temporary_path,
  unsigned int task_index
)
{
  fprintf(stdout, "Starting S3 upload task #%u\n", task_index + 1);

  minio::s3::BaseUrl base_url(url);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  while (true) {
    auto event = message_queue.pop_front_waiting();
    if (event == nullptr) continue;
    if (event->message_type() == TERMINATE) {
      break;
    }
    const auto file_event = std::dynamic_pointer_cast<S3TaskEventNewFile>(event);
    auto filename = std::filesystem::path(temporary_path) / std::filesystem::path(file_event->get_name());
    fprintf(stdout, "[Task %u] Uploading %s\n", task_index + 1, filename.string().c_str());
    const auto save_to_filename = std::filesystem::path(path_to) / std::filesystem::path(file_event->get_name());
    bool uploaded = false;
    try {
      write_file_s3(filename.string(), client, bucket, save_to_filename.string());
      uploaded = true;
    }
    catch (S3Error &e) {
      fprintf(stderr, "[Task %u] Could not upload file \"%s\"\n", task_index + 1, filename.string().c_str());
      progress_queue.push_back(S3ProgressEvent {UPLOAD_ERROR, file_event->get_name(), file_event->get_index(), e.what()});
    }
    fprintf(stdout, "[Task %u] Deleting %s\n", task_index + 1, filename.string().c_str());
    std::remove(filename.string().c_str());

    if (uploaded) {
      progress_queue.push_back(S3ProgressEvent {UPLOAD_OK, file_event->get_name(), file_event->get_index(), ""});
    }
  }

  fprintf(stdout, "S3 upload task #%u completed\n", task_index + 1);
}

void S3Uploader::start() {
  minio::s3::BucketExistsArgs args;
  args.bucket = bucket;

  if (!client) {
    throw S3Error("S3 Client is invalid");
  }

  const auto &resp = client->BucketExists(args);
  if (!resp) {
    throw S3Error(resp.Error().String());
  }

  if (!resp.exist) {
    const auto err = resp.Error();
    throw S3Error(std::string("Bucket \"") + bucket + std::string("\" does not exist"));
  }

  std::string empty_file;
  const auto file_name = gen_random(RANDOM_FILE_NAME_LENGTH);
  const auto save_to_filename = std::filesystem::path(path_to) / std::filesystem::path(file_name);
  try {
    write_content_to_file_s3(empty_file, *client, bucket, save_to_filename.string());
  }
  catch (S3Error &e) {
    throw S3Error(std::string("Could not write to bucket \"") + bucket + std::string("\". Error: ") + std::string(e.what()));
  }

  try {
    delete_file_s3(*client, bucket, save_to_filename.string());
  }
  catch (S3Error &e) {
    throw S3Error(std::string("Could not delete from bucket \"") + bucket + std::string("\". Error: ") + std::string(e.what()));
  }

  tasks.clear();
  for (unsigned int i = 0; i < thread_count; i++) {
    // use lambda to MSVC workaround
    std::thread task([&, i](){ s3_upload_task(progress_queue, url, access_key, secret_key, bucket, path_to, message_queue, path_from, i); });
    tasks.push_back(std::move(task));
  }
}

void S3Uploader::stop() {
  std::shared_ptr<S3TaskEvent> message = std::make_shared<S3TaskEventTerminate>();
  for (auto i = 0; i < tasks.size(); i++) {
    message_queue.push_back(message);
  }
  for (auto &t : tasks) {
    t.join();
  }
  tasks.clear();
}

ThreadSafeDeque<S3ProgressEvent> &S3Uploader::get_progress_queue() {
  return progress_queue;
}

void S3Uploader::new_file(const std::string &file_name, unsigned int file_index) {
  std::shared_ptr<S3TaskEvent> message = std::make_shared<S3TaskEventNewFile>(file_name, file_index);
  message_queue.push_back(std::move(message));
}

void S3Uploader::delete_file(const std::string &file_name) {
  const auto delete_filename = std::filesystem::path(path_to) / std::filesystem::path(file_name);
  delete_file_s3(*client, bucket, delete_filename.string());
}
