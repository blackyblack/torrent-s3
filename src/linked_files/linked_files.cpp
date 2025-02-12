#include "./linked_files.hpp"

void LinkedFiles::add_files(std::string parent, std::vector<std::string> files) {
    linked_files[parent].insert(linked_files[parent].end(), files.begin(), files.end());
    for (const auto &f : files) {
        parent_files[f] = parent;
    }
}

void LinkedFiles::remove_child(std::string child) {
    const auto parent_iter = parent_files.find(child);
    if(parent_iter == parent_files.end()) return;
    const auto children_iter = linked_files.find(parent_iter->second);
    if(children_iter == linked_files.end()) return;
    auto &children_files_ref = children_iter->second;
    children_files_ref.erase(std::remove(children_files_ref.begin(), children_files_ref.end(), child), children_files_ref.end());
    if (children_files_ref.size() > 0) return;
    parent_files.erase(parent_iter);
}

void LinkedFiles::remove_parent(std::string parent) {
    const auto children_iter = linked_files.find(parent);
    if(children_iter == linked_files.end()) return;
    for (const auto child : children_iter->second) {
        const auto parent_iter = parent_files.find(child);
        if(parent_iter == parent_files.end()) continue;;
        parent_files.erase(parent_iter);
    }
    linked_files.erase(children_iter);
}

std::unordered_map<std::string, std::vector<std::string>> LinkedFiles::get_files() const {
    return std::unordered_map<std::string, std::vector<std::string>>(linked_files);
}

std::optional<std::string> LinkedFiles::get_parent(std::string child) const {
    const auto parent_iter = parent_files.find(child);
    if (parent_iter == parent_files.end()) {
        return {};
    }
    return parent_iter->second;
}
