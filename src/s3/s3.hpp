#pragma once

#include <iostream>
#include <vector>
#include <thread>

#include <miniocpp/client.h>

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
  // path_from_ - where to look for files to upload
  // path_to_ - where to upload them
  S3Uploader(const std::string &path_from_, unsigned int thread_count_, const std::string &url_, const std::string &access_key_, const std::string &secret_key_, const std::string &bucket_, const std::string &path_to_);

  void start();
  void stop();
  void new_file(const std::string &file_name);
  void delete_file(const std::string &file_name);
private:
  ThreadSafeDeque<std::shared_ptr<S3TaskEvent>> message_queue;
  const std::string path_from;
  unsigned int thread_count;

  // S3 credentials
  const std::string url;
  const std::string access_key;
  const std::string secret_key;
  const std::string bucket;
  const std::string path_to;
  std::unique_ptr<minio::creds::Provider> provider;
  std::unique_ptr<minio::s3::Client> client;

  std::vector<std::thread> tasks;
};
