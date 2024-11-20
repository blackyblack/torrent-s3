#include <iostream>

#include "../path_utils/path_utils.hpp"

#include "./s3.hpp"

// how many S3 upload tasks to run simultaneously
#define TASKS_COUNT_DEFAULT 16

S3Error::S3Error(std::string message) : std::runtime_error(message.c_str()) {}

message_type_t S3TaskEventTerminate::message_type() {
  return TERMINATE;
}

S3TaskEventNewFile::S3TaskEventNewFile(std::string name) : file_name {name} {}

message_type_t S3TaskEventNewFile::message_type() {
  return NEW_FILE;
}

std::string S3TaskEventNewFile::get_name() {
  return file_name;
}

S3Uploader::S3Uploader(const std::string &temporary_path_, unsigned int thread_count_) : temporary_path {temporary_path_}, thread_count {thread_count_}, message_queue {} {
  if (!thread_count) {
    thread_count = TASKS_COUNT_DEFAULT;
  }
}

static void s3_upload_task(ThreadSafeDeque<std::shared_ptr<S3TaskEvent>> &message_queue, const std::string &temporary_path, unsigned int task_index)
{
  fprintf(stdout, "Starting S3 upload task #%u\n", task_index + 1);

  while (true) {
    auto event = message_queue.pop_front_waiting();
    if (event == nullptr) continue;
    if (event->message_type() == TERMINATE) {
      break;
    }
    const auto file_event = std::dynamic_pointer_cast<S3TaskEventNewFile>(event);
    auto filename = path_join(temporary_path, file_event->get_name());
    fprintf(stdout, "[Task %u] Uploading %s\n", task_index + 1, filename.c_str());
    fprintf(stdout, "[Task %u] Deleting %s\n", task_index + 1, filename.c_str());
    std::remove(filename.c_str());
  }

  fprintf(stdout, "S3 upload task #%u completed\n", task_index + 1);
}

void S3Uploader::start() {
  tasks.clear();
  for (unsigned int i = 0; i < thread_count; i++) {
    // use lambda to MSVC workaround
    std::thread task([&, i](){ s3_upload_task(message_queue, temporary_path, i); });
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

void S3Uploader::new_file(const std::string &file_name) {
  std::shared_ptr<S3TaskEvent> message = std::make_shared<S3TaskEventNewFile>(file_name);
  message_queue.push_back(std::move(message));
}
