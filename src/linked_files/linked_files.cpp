#include "./linked_files.hpp"

void LinkedFiles::add_files(std::string parent, std::vector<std::string> files) {
    linked_files[parent].insert(files.begin(), files.end());
    for (const auto &f : files) {
        parent_files[f] = parent;
    }
}

void LinkedFiles::remove_child(std::string child) {
    const auto parent_iter = parent_files.find(child);
    if (parent_iter == parent_files.end()) return;
    const auto parent = parent_iter->second;
    parent_files.erase(parent_iter);
    const auto children_iter = linked_files.find(parent);
    if (children_iter == linked_files.end()) {
        return;
    }
    auto &children_files_ref = children_iter->second;
    const auto child_iter = children_files_ref.find(child);
    if (child_iter == children_files_ref.end()) {
        return;
    }
    children_files_ref.erase(child_iter);
    if (children_files_ref.size() > 0) {
        return;
    }
    linked_files.erase(children_iter);
}

void LinkedFiles::remove_parent(std::string parent) {
    const auto children_iter = linked_files.find(parent);
    if (children_iter == linked_files.end()) return;
    for (const auto child : children_iter->second) {
        const auto parent_iter = parent_files.find(child);
        if (parent_iter == parent_files.end()) continue;;
        parent_files.erase(parent_iter);
    }
    linked_files.erase(children_iter);
}

std::unordered_map<std::string, std::vector<std::string>> LinkedFiles::get_files() const {
    std::unordered_map<std::string, std::vector<std::string>> linked_files_copy;
    for (const auto kv : linked_files) {
        std::vector<std::string> files;
        for (const auto f : kv.second) {
            files.push_back(f);
        }
        linked_files_copy[kv.first] = files;
    }
    return linked_files_copy;
}

std::optional<std::string> LinkedFiles::get_parent(std::string child) const {
    const auto parent_iter = parent_files.find(child);
    if (parent_iter == parent_files.end()) {
        return {};
    }
    return parent_iter->second;
}
