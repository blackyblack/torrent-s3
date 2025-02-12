#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

class LinkedFiles {
  public:
    void add_files(std::string parent, std::vector<std::string> files);
    void remove_child(std::string child);
    void remove_parent(std::string parent);
    std::unordered_map<std::string, std::vector<std::string>> get_files() const;
    std::optional<std::string> get_parent(std::string child) const;

  private:
    // key - parent file
    // value - linked files
    std::unordered_map<std::string, std::vector<std::string>> linked_files;
    // key - linked file
    // value - parent file
    std::unordered_map<std::string, std::string> parent_files;
};
