#pragma once

#include <iostream>

#include "../deque/deque.hpp"

enum message_type_t {
  NEW_FILE,
  TERMINATE,
};

class S3TaskEvent {
public:
  virtual message_type_t message_type() = 0;
};

class S3TaskEventTerminate : public S3TaskEvent {
public:
  message_type_t message_type();
};

class S3TaskEventNewFile : public S3TaskEvent {
private:
  std::string file_name;
public:
  S3TaskEventNewFile(std::string name);
  message_type_t message_type();
  std::string get_name();
};

class S3Error : public std::runtime_error
{
public:
  S3Error(std::string message);
};

void s3_upload_task(ThreadSafeDeque<std::shared_ptr<S3TaskEvent>> &message_queue, const std::string &temporary_path);
