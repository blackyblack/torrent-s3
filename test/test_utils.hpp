#pragma once

#include <string>

#define STRING(x) #x
#define XSTRING(x) STRING(x)

#define APP_NAME XSTRING(CMAKE_PROJECT_NAME)
#define SOURCE_DIR XSTRING(CMAKE_SOURCE_DIR)

std::string get_asset(std::string file);

std::string get_tmp_dir();
