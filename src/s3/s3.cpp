#include <iostream>

#include "../path_utils/path_utils.hpp"

#include "./s3.hpp"

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

void s3_upload_task(ThreadSafeDeque<std::shared_ptr<S3TaskEvent>> &message_queue, const std::string &temporary_path)
{
  fprintf(stdout, "Starting S3 upload task\n");

  while (true) {
    auto event = message_queue.pop_front_waiting();
    if (event == nullptr) continue;
    if (event->message_type() == TERMINATE) {
      break;
    }
    const auto file_event = std::dynamic_pointer_cast<S3TaskEventNewFile>(event);
    auto filename = path_join(temporary_path, file_event->get_name());
    fprintf(stdout, "Uploading %s\n", filename.c_str());
    fprintf(stdout, "Deleting %s\n", filename.c_str());
    std::remove(filename.c_str());
  }

  fprintf(stdout, "S3 upload task completed\n");
}
