#include "./path_utils.hpp"

std::string path_join(const std::string& p1, const std::string& p2)
{
  char sep = '/';
  std::string tmp = p1;

#ifdef _WIN32
  sep = '\\';
#endif

  // Add separator if it is not included in the first path:
  if (p1[p1.length() - 1] != sep) {
    tmp += sep;
    return tmp + p2;
  }
  return p1 + p2;
}
