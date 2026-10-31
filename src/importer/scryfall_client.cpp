#include "importer/scryfall_client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <memory>

using json = nlohmann::json;

namespace mtg::importer {

namespace {

constexpr const char* BULK_DATA_URL =
    "https://api.scryfall.com/bulk-data";

constexpr const char* USER_AGENT = "magindex";

struct CurlDeleter {
    void operator()(CURL* curl) const {
        if (curl)
            curl_easy_cleanup(curl);
    }
};

using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;

size_t WriteCallback(
    char* data,
    size_t size,
    size_t count,
    void* userdata
) {
    auto* output = static_cast<std::string*>(userdata);

    output->append(data, size * count);

    return size * count;
}

void InitializeCurl() {
    static const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);

    if (result != CURLE_OK)
        throw std::runtime_error("Failed to initialize libcurl");
}

BulkMetadata ParseMetadata(const json& item) {
    BulkMetadata metadata;

    metadata.id = item.value("id", "");
    metadata.type = item.value("type", "");
    metadata.name = item.value("name", "");
    metadata.updated_at = item.value("updated_at", "");

    metadata.download_uri =
        item.value("download_uri", "");

    metadata.jsonl_download_uri =
        item.value("jsonl_download_uri", "");

    metadata.compressed_size =
        item.value("compressed_size", std::uint64_t{0});

    return metadata;
}

}

ScryfallClient::ScryfallClient()
{
    InitializeCurl();
}

std::vector<BulkMetadata> ScryfallClient::GetBulkMetadata() {
    CurlHandle curl(curl_easy_init());

    if (!curl)
        throw std::runtime_error("Failed to create CURL handle");

    std::string response;

    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(
        headers,
        "Accept: application/json"
    );

    curl_easy_setopt(curl.get(), CURLOPT_URL, BULK_DATA_URL);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, USER_AGENT);

    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);

    CURLcode result = curl_easy_perform(curl.get());

    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);

    if (result != CURLE_OK) {
        throw std::runtime_error(
            std::string("Scryfall request failed: ") +
            curl_easy_strerror(result)
        );
    }

    if (status != 200)
        throw std::runtime_error("Unexpected Scryfall HTTP status");

    json document = json::parse(response);

    std::vector<BulkMetadata> metadata;

    for (const auto& item : document.at("data")) {
        metadata.push_back(ParseMetadata(item));
    }

    return metadata;
}

std::optional<BulkMetadata> ScryfallClient::GetBulkMetadata(
    const std::string& type
) {
    auto metadata = GetBulkMetadata();

    for (const auto& item : metadata) {
        if (item.type == type)
            return item;
    }

    return std::nullopt;
}

}