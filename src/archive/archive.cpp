#include <fstream>
#include <fcntl.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif // _WIN32

#include <archive.h>
#include <archive_entry.h>

#include "./archive.hpp"

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

bool is_packed(std::filesystem::path file_name) {
    if (!ends_with(file_name.string(), ".zip") && !ends_with(file_name.string(), ".rar") && !ends_with(file_name.string(), ".7z")) {
        return false;
    }
    archive *arch = archive_read_new();
    archive_read_support_format_7zip(arch);
    archive_read_support_format_zip(arch);
    archive_read_support_format_rar(arch);
    archive_read_support_format_rar5(arch);
    archive_read_support_filter_all(arch);
    const auto ret = archive_read_open_filename(arch, std::filesystem::u8path(file_name.string()).string().c_str(), READ_BLOCK_SIZE);
    archive_read_close(arch);
    archive_read_free(arch);
    return ret == ARCHIVE_OK;
}

std::variant<std::vector<file_unpack_info_t>, std::string> unpack_file(std::filesystem::path file_name, std::filesystem::path output_directory) {
    std::vector<file_unpack_info_t> unpacked_files;
    archive *arch = archive_read_new();
    archive_read_support_format_7zip(arch);
    archive_read_support_format_zip(arch);
    archive_read_support_format_rar(arch);
    archive_read_support_format_rar5(arch);
    archive_read_support_filter_all(arch);

    const auto ret = archive_read_open_filename(arch, std::filesystem::u8path(file_name.string()).string().c_str(), READ_BLOCK_SIZE);
    if (ret != ARCHIVE_OK) {
        archive_read_close(arch);
        archive_read_free(arch);
        return std::string("Failed to open archive \"") + file_name.string() + "\"";
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
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.string().c_str(), err_string);
            break;
        }
        const auto *entry_file_name = archive_entry_pathname_utf8(entry);
        if (entry_file_name == nullptr) {
            const auto err_string = "Could not get name of file in archive";
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.string().c_str(), err_string);
            continue;
        }
        const auto extracted_file_name = std::string(entry_file_name);
        const auto new_extracted_file_name = output_directory / extracted_file_name;
        archive_entry_set_pathname_utf8(entry, new_extracted_file_name.string().c_str());
        if (archive_write_header(write_file, entry) != ARCHIVE_OK) {
            const auto err_string = archive_error_string(write_file);
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.string().c_str(), err_string);
            unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name.string(), std::string(err_string) });
            continue;
        }
        const auto copy_error = copy_data(file_name.string(), arch, write_file);
        if (copy_error.length() > 0) {
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.string().c_str(), copy_error.c_str());
            unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name.string(), copy_error });
            continue;
        }
        if (archive_write_finish_entry(write_file) != ARCHIVE_OK) {
            const auto err_string = archive_error_string(write_file);
            fprintf(stderr, "Extracting file \"%s\" error: %s\n", file_name.string().c_str(), err_string);
            unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name.string(), std::string(err_string) });
            continue;
        }
        unpacked_files.push_back(file_unpack_info_t { new_extracted_file_name.string(), std::nullopt });
    }
    archive_read_close(arch);
    archive_read_free(arch);
    archive_write_close(write_file);
    archive_write_free(write_file);
    return unpacked_files;
}

std::optional<std::string> zip_file(std::filesystem::path source_path, std::filesystem::path dest_path) {
    const auto dest_file = std::filesystem::u8path(dest_path.string()).string();
    auto *arch = archive_write_new();
    archive_write_set_options(arch, "hdrcharset=UTF-8");
    auto ret = archive_write_set_format_zip(arch);
    if (ret != ARCHIVE_OK) {
        const auto err_string = archive_error_string(arch);
        archive_write_close(arch);
        archive_write_free(arch);
        return std::string("Failed to create archive from \"") + source_path.string() + "\": " + err_string;
    }
    ret = archive_write_zip_set_compression_deflate(arch);
    if (ret != ARCHIVE_OK) {
        const auto err_string = archive_error_string(arch);
        archive_write_close(arch);
        archive_write_free(arch);
        return std::string("Failed to create archive from \"") + source_path.string() + "\": " + err_string;
    }
    std::filesystem::create_directories(std::filesystem::u8path(dest_path.parent_path().string()));

    auto fd = open(dest_file.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return std::string("Failed to write file \"") + dest_file + "\"";
    }

    ret = archive_write_open_fd(arch, fd);
    if (ret != ARCHIVE_OK) {
        const auto err_string = archive_error_string(arch);
        archive_write_close(arch);
        archive_write_free(arch);
        close(fd);
        return std::string("Failed to create archive from \"") + source_path.string() + "\": " + err_string;
    }
    auto *entry = archive_entry_new();
    archive_entry_set_pathname(entry, source_path.filename().string().c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    ret = archive_write_header(arch, entry);
    if (ret != ARCHIVE_OK) {
        const auto err_string = archive_error_string(arch);
        archive_write_close(arch);
        archive_write_free(arch);
        close(fd);
        return std::string("Failed to create archive from \"") + source_path.string() + "\": " + err_string;
    }
    std::ifstream file(std::filesystem::u8path(source_path.string()), std::ios::binary);
    if (!file) {
        archive_entry_free(entry);
        archive_write_close(arch);
        archive_write_free(arch);
        close(fd);
        return std::string("Failed to create archive from \"") + source_path.string() + "\"";
    }

    char buff[READ_BLOCK_SIZE];
    while (!file.eof()) {
        file.read(buff, READ_BLOCK_SIZE);
        std::streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
            archive_write_data(arch, buff, static_cast<size_t>(bytes_read));
        }
    }
    archive_write_finish_entry(arch);
    archive_entry_free(entry);
    archive_write_close(arch);
    archive_write_free(arch);
    close(fd);
    return std::nullopt;
}
