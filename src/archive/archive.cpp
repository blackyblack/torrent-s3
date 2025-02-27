#include <archive.h>
#include <archive_entry.h>

#include "./archive.hpp"
#include <filesystem>

#define READ_BLOCK_SIZE 10240

static inline bool ends_with(std::string const &value, std::string const &ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static std::string copy_data(std::string file_name, archive *ar, archive *aw) {
    size_t size;
    int64_t offset;

    while (true) {
        const void *buff = nullptr;
        const auto read_ret = archive_read_data_block(ar, &buff, &size, &offset);
        if (read_ret == ARCHIVE_EOF)
            return "";
        if (read_ret != ARCHIVE_OK) {
            return std::string(archive_error_string(ar));
        }
        const auto write_ret = archive_write_data_block(aw, buff, size, offset);
        if (write_ret == ARCHIVE_OK)
            continue;
        return std::string(archive_error_string(aw));
    }
}

bool is_packed(std::string file_name) {
    if (!ends_with(file_name, ".zip") && !ends_with(file_name, ".rar") && !ends_with(file_name, ".7z")) {
        return false;
    }
    archive *arch = archive_read_new();
    archive_read_support_format_7zip(arch);
    archive_read_support_format_zip(arch);
    archive_read_support_format_rar(arch);
    archive_read_support_format_rar5(arch);
    archive_read_support_filter_all(arch);
    const auto ret = archive_read_open_filename(arch, file_name.c_str(), READ_BLOCK_SIZE);
    archive_read_close(arch);
    archive_read_free(arch);
    return ret == ARCHIVE_OK;
}

std::variant<std::vector<file_unpack_info_t>, std::string> unpack_file(std::string file_name, std::string output_directory) {
    std::vector<file_unpack_info_t> unpacked_files;
    archive *arch = archive_read_new();
    archive_read_support_format_7zip(arch);
    archive_read_support_format_zip(arch);
    archive_read_support_format_rar(arch);
    archive_read_support_format_rar5(arch);
    archive_read_support_filter_all(arch);
    const auto ret = archive_read_open_filename(arch, file_name.c_str(), READ_BLOCK_SIZE);
    if (ret != ARCHIVE_OK) {
        archive_read_close(arch);
        archive_read_free(arch);
        return std::string("Failed to open archive \"") + file_name + "\"";
    }
    archive *write_file = archive_write_disk_new();
    archive_write_disk_set_options(write_file, 0);
    while (true) {
        archive_entry *entry = nullptr;
        const auto read_result = archive_read_next_header(arch, &entry);
        if (read_result == ARCHIVE_EOF)
            break;
        if (read_result != ARCHIVE_OK) {
            const auto err_string = archive_error_string(arch);
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.c_str(), err_string);
            break;
        }
        bool is_utf8 = false;
        auto *entry_file_name = archive_entry_pathname(entry);
        if (entry_file_name == nullptr) {
            entry_file_name = archive_entry_pathname_utf8(entry);
            is_utf8 = true;
        }
        if (entry_file_name == nullptr) {
            const auto err_string = "Could not get name of file in archive";
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.c_str(), err_string);
            continue;
        }
        const auto extracted_file_name = std::string(entry_file_name);
        const auto new_extracted_file_name = (std::filesystem::path(output_directory) / std::filesystem::path(extracted_file_name)).string();
        if (is_utf8) {
            archive_entry_set_pathname_utf8(entry, new_extracted_file_name.c_str());
        } else {
            archive_entry_set_pathname(entry, new_extracted_file_name.c_str());
        }
        if (archive_write_header(write_file, entry) != ARCHIVE_OK) {
            const auto err_string = archive_error_string(write_file);
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.c_str(), err_string);
            unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name, std::string(err_string) });
            continue;
        }
        const auto copy_error = copy_data(file_name, arch, write_file);
        if (copy_error.length() > 0) {
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.c_str(), copy_error.c_str());
            unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name, copy_error });
            continue;
        }
        if (archive_write_finish_entry(write_file) != ARCHIVE_OK) {
            const auto err_string = archive_error_string(write_file);
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.c_str(), err_string);
            unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name, std::string(err_string) });
            continue;
        }
        unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name, "" });
    }
    archive_read_close(arch);
    archive_read_free(arch);
    archive_write_close(write_file);
    archive_write_free(write_file);
    return unpacked_files;
}
