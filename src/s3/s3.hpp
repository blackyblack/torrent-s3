#pragma once

#include <iostream>
#include <vector>
#include <thread>

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

class S3Uploader {
public:
  // use default thread count (16) if thread_count is set to 0
  S3Uploader(const std::string &temporary_path_, unsigned int thread_count_);

  void start();
  void stop();
  void new_file(const std::string &file_name);
private:
  ThreadSafeDeque<std::shared_ptr<S3TaskEvent>> message_queue;
  const std::string temporary_path;
  unsigned int thread_count;

  std::vector<std::thread> tasks;
};
