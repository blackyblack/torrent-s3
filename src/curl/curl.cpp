#include <curl/curl.h>

#include "./curl.hpp"

static size_t write_buffer_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto& mem = *static_cast<std::string*>(userp);
    mem.append(static_cast<char*>(contents), realsize);
    return realsize;
}

std::variant<lt::torrent_info, std::string> download_torrent_info(const std::string &url) {
    auto curl = curl_easy_init();
    if (curl == nullptr) {
        return std::string("Cannot start Curl");
    }

    std::string data;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_buffer_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    const auto res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        return std::string("Download with Curl error: ") + std::string(curl_easy_strerror(res)) + std::string("\n");
    }
    try {
        lt::torrent_info torrent_file(data.data(), (int) data.size());
        return torrent_file;
    } catch (std::exception &e) {
        return std::string("Couldn't parse .torrent file");
    }
}
