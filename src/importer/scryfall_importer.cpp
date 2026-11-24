#include <filesystem>

#include "importer/scryfall_importer.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <memory>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "importer/bulk_metadata.h"

using json = nlohmann::json;

namespace magindex::importer {
    constexpr const char *BULK_DATA_URL = "https://api.scryfall.com/bulk-data/default-cards";
    constexpr const char *SET_METADATA_URL = "https://api.scryfall.com/sets";
    constexpr const char *USER_AGENT = "magindex";
    //Maximum number of cards we will handle for incremental update before switching to bulk download. Each page is 175 cards
    constexpr std::uint64_t MAX_CARD_COUNT_FOR_INCREMENTAL_UPDATE = 175 * 5;
    constexpr std::uint64_t MILLISECONDS_BETWEEN_INCREMENTAL_UPGRADE_PAGE_REQUEST = 600;

    struct CurlDeleter {
        void operator()(CURL *curl) const {
            if (curl)
                curl_easy_cleanup(curl);
        }
    };

    using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;

    class CurlHeaders {
    public:
        CurlHeaders(CURL *curl, curl_slist *headers) : curl_(curl), headers_(headers) {
        }

        CurlHeaders(const CurlHeaders &) = delete;

        CurlHeaders &operator=(const CurlHeaders &) = delete;

        ~CurlHeaders() {
            if (curl_)
                curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, static_cast<curl_slist *>(nullptr));
            if (headers_)
                curl_slist_free_all(headers_);
        }

        curl_slist *get() const noexcept { return headers_; }

    private:
        CURL *curl_;
        curl_slist *headers_;
    };

    struct CurlUrlDeleter {
        void operator()(CURLU *url) const {
            if (url)
                curl_url_cleanup(url);
        }
    };

    using CurlUrlHandle = std::unique_ptr<CURLU, CurlUrlDeleter>;

    struct CurlFreeDeleter {
        void operator()(char *value) const {
            if (value)
                curl_free(value);
        }
    };

    using CurlAllocatedString = std::unique_ptr<char, CurlFreeDeleter>;

    class TemporaryFileGuard {
    public:
        explicit TemporaryFileGuard(std::filesystem::path path) : path_(std::move(path)) {
        }

        ~TemporaryFileGuard() {
            if (active_) {
                std::error_code ignored;
                std::filesystem::remove(path_, ignored);
            }
        }

        void Release() noexcept { active_ = false; }

    private:
        std::filesystem::path path_;
        bool active_ = true;
    };

    void LogInfo(const std::string &message) {
        std::clog << "[ScryfallImporter] INFO: " << message << '\n';
    }

    void LogError(const std::string &message) {
        std::clog << "[ScryfallImporter] ERROR: " << message << '\n';
    }

    CurlHeaders MakeJsonHeaders(CURL *curl) {
        curl_slist *headers = curl_slist_append(nullptr, "Accept: application/json");
        if (!headers) {
            LogError("Failed to allocate Scryfall request headers");
            throw std::runtime_error("Failed to allocate Scryfall request headers");
        }
        return CurlHeaders(curl, headers);
    }

    template<typename T>
    void SetCurlOption(CURL *curl, CURLoption option, T value, const char *context) {
        const CURLcode result = curl_easy_setopt(curl, option, value);
        if (result != CURLE_OK) {
            const std::string message = std::string(context) + ": " + curl_easy_strerror(result);
            LogError(message);
            throw std::runtime_error(message);
        }
    }

    long GetHttpStatus(CURL *curl, const char *context) {
        long status = 0;
        const CURLcode result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (result != CURLE_OK) {
            const std::string message = std::string(context) + ": " + curl_easy_strerror(result);
            LogError(message);
            throw std::runtime_error(message);
        }
        return status;
    }

    void InitializeCurl() {
        static const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);

        if (result != CURLE_OK) {
            LogError(std::string("Failed to initialize libcurl: ") + curl_easy_strerror(result));
            throw std::runtime_error("Failed to initialize libcurl");
        }
    }

    std::chrono::sys_time<std::chrono::milliseconds> parseTimestamp(const std::string &timestamp) {
        using namespace std::chrono;

        if (timestamp.size() < 20 || timestamp[10] != 'T')
            throw std::runtime_error("Invalid timestamp");

        std::tm tm{};

        std::istringstream ss(timestamp.substr(0, 19));

        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");

        if (ss.fail())
            throw std::runtime_error("Invalid timestamp");

        // Accept an optional ISO-8601 fractional part and normalize it to milliseconds.
        size_t pos = 19;
        milliseconds ms{0};
        if (pos < timestamp.size() && timestamp[pos] == '.') {
            const size_t start = ++pos;
            while (pos < timestamp.size() && timestamp[pos] >= '0' && timestamp[pos] <= '9')
                ++pos;
            if (pos == start)
                throw std::runtime_error("Invalid timestamp fraction");
            std::string fraction = timestamp.substr(start, std::min<size_t>(3, pos - start));
            while (fraction.size() < 3)
                fraction.push_back('0');
            ms = milliseconds{std::stoi(fraction)};
        }

        seconds offset{0};
        if (pos < timestamp.size() && (timestamp[pos] == '+' || timestamp[pos] == '-')) {
            const int sign = timestamp[pos++] == '+' ? 1 : -1;
            if (pos + 5 != timestamp.size() || timestamp[pos + 2] != ':')
                throw std::runtime_error("Invalid timestamp timezone");
            const int hoursOffset = std::stoi(timestamp.substr(pos, 2));
            const int minutesOffset = std::stoi(timestamp.substr(pos + 3, 2));
            if (hoursOffset > 23 || minutesOffset > 59)
                throw std::runtime_error("Invalid timestamp timezone");
            offset = sign * (hours{hoursOffset} + minutes{minutesOffset});
            pos += 5;
        } else if (pos < timestamp.size() && timestamp[pos] == 'Z') {
            ++pos;
        }
        if (pos != timestamp.size())
            throw std::runtime_error("Invalid timestamp timezone");

        // Construct UTC timestamp
        auto date = sys_days{
            year{tm.tm_year + 1900} /
            month{static_cast<unsigned>(tm.tm_mon + 1)} /
            day{static_cast<unsigned>(tm.tm_mday)}
        };

        return date
               + hours{tm.tm_hour}
               + minutes{tm.tm_min}
               + seconds{tm.tm_sec}
               + ms - offset;
    }


    ScryfallImporter::ScryfallImporter() {
        InitializeCurl();
        LogInfo("Importer initialized");
    }


    static size_t CurlWriteCallback(
        char *data,
        size_t size,
        size_t count,
        void *userdata
    ) {
        auto *file = static_cast<std::ofstream *>(userdata);

        if (size != 0 && count > (std::numeric_limits<size_t>::max)() / size)
            return 0;

        size_t bytes = size * count;

        file->write(data, static_cast<std::streamsize>(bytes));

        return *file ? bytes : 0;
    }

    static size_t CurlStringWriteCallback(char *data, size_t size, size_t count, void *userdata) {
        auto *response = static_cast<std::string *>(userdata);
        if (size != 0 && count > (std::numeric_limits<size_t>::max)() / size)
            return 0;
        const size_t bytes = size * count;
        response->append(data, bytes);
        return bytes;
    }


    BulkMetadata ParseBulkMetadata(const json &item) {
        BulkMetadata metadata;

        metadata.id = item.value("id", "");
        metadata.type = item.value("type", "");
        metadata.name = item.value("name", "");
        metadata.updated_at = item.value("updated_at", "");

        metadata.jsonl_download_uri = item.value("jsonl_download_uri", "");

        metadata.compressed_size = item.value("compressed_size", std::uint64_t{0});

        return metadata;
    }

    void ParseSetMetadata(const json &data, std::vector<SetMetadata> &OUTMetadata) {
        for (const auto &set: data) {
            SetMetadata metadata;

            metadata.id = set["id"].get<std::string>();
            metadata.code = set["code"].get<std::string>();
            metadata.name = set["name"].get<std::string>();
            metadata.uri = set["uri"].get<std::string>();
            metadata.searchUri = set["search_uri"].get<std::string>();
            metadata.scryfallUri = set["scryfall_uri"].get<std::string>();

            if (set.contains("released_at") && !set["released_at"].is_null()) {
                const std::string releaseDate = set["released_at"].get<std::string>();
                int year = std::stoi(releaseDate.substr(0, 4));
                unsigned month = std::stoi(releaseDate.substr(5, 2));
                unsigned day = std::stoi(releaseDate.substr(8, 2));
                metadata.releasedAt = std::chrono::year_month_day(
                    std::chrono::year(year), std::chrono::month(month), std::chrono::day(day));
            }

            metadata.setType = set["set_type"].get<std::string>();
            metadata.cardCount = set["card_count"].get<unsigned>();
            metadata.parentSetCode = set.contains("parent_set_code") && !set["parent_set_code"].is_null()
                                         ? set["parent_set_code"].get<std::string>()
                                         : std::string{};
            metadata.bDigital = set["digital"].get<bool>();
            metadata.bNonfoilOnly = set["nonfoil_only"].get<bool>();
            metadata.bFoilOnly = set["foil_only"].get<bool>();
            metadata.iconSvgUri = set["icon_svg_uri"].get<std::string>();

            OUTMetadata.push_back(metadata);
        }
    }

    BulkMetadata ScryfallImporter::GetBulkMetadata() {
        CurlHandle curl(curl_easy_init());

        if (!curl)
            throw std::runtime_error("Failed to create CURL handle");

        std::string response;

        CurlHeaders headers = MakeJsonHeaders(curl.get());
        LogInfo("Requesting bulk metadata");
        SetCurlOption(curl.get(), CURLOPT_URL, BULK_DATA_URL, "Configure bulk metadata URL");
        SetCurlOption(curl.get(), CURLOPT_HTTPHEADER, headers.get(), "Configure bulk metadata headers");
        SetCurlOption(curl.get(), CURLOPT_USERAGENT, USER_AGENT, "Configure bulk metadata user agent");
        SetCurlOption(curl.get(), CURLOPT_WRITEFUNCTION, CurlStringWriteCallback, "Configure bulk metadata callback");
        SetCurlOption(curl.get(), CURLOPT_WRITEDATA, &response, "Configure bulk metadata response buffer");
        SetCurlOption(curl.get(), CURLOPT_FOLLOWLOCATION, 1L, "Configure bulk metadata redirects");
        SetCurlOption(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L, "Configure bulk metadata connect timeout");
        SetCurlOption(curl.get(), CURLOPT_TIMEOUT, 60L, "Configure bulk metadata timeout");
        SetCurlOption(curl.get(), CURLOPT_FAILONERROR, 1L, "Configure bulk metadata HTTP errors");

        CURLcode result = curl_easy_perform(curl.get());

        const long status = GetHttpStatus(curl.get(), "Read bulk metadata HTTP status");

        if (status == 429) {
            LogError("Bulk metadata request was rate limited (HTTP 429)");
            throw std::runtime_error("Scryfall rate limit reached while requesting bulk metadata (HTTP 429)");
        }

        if (status != 0 && status != 200) {
            const std::string message = "Bulk metadata request returned HTTP " + std::to_string(status);
            LogError(message);
            throw std::runtime_error(message);
        }
        if (result != CURLE_OK) {
            const std::string message = std::string("Bulk metadata request failed: ") + curl_easy_strerror(result);
            LogError(message);
            throw std::runtime_error(message);
        }
        if (status != 200) {
            const std::string message = "Bulk metadata request returned HTTP " + std::to_string(status);
            LogError(message);
            throw std::runtime_error(message);
        }

        json document;
        try {
            document = json::parse(response);
        } catch (const json::exception &error) {
            const std::string message = std::string("Failed to parse bulk metadata JSON: ") + error.what();
            LogError(message);
            throw std::runtime_error(message);
        }

        LogInfo("Received bulk metadata");

        try {
            return ParseBulkMetadata(document);
        } catch (const std::exception &error) {
            const std::string message = std::string("Invalid bulk metadata response: ") + error.what();
            LogError(message);
            throw std::runtime_error(message);
        }
    }


    void ScryfallImporter::GetBulkData(
        const std::string &url,
        const std::filesystem::path &outputPath
    ) {
        auto tempPath = outputPath;
        tempPath += ".part";
        TemporaryFileGuard tempFileGuard(tempPath);

        if (!outputPath.parent_path().empty()) {
            std::error_code directoryError;
            std::filesystem::create_directories(outputPath.parent_path(), directoryError);
            if (directoryError) {
                const std::string message = "Failed to create bulk download directory: " + directoryError.message();
                LogError(message);
                throw std::runtime_error(message);
            }
        }

        std::ofstream file(tempPath, std::ios::binary);

        if (!file) {
            const std::string message = "Failed to create bulk download file: " + tempPath.string();
            LogError(message);
            throw std::runtime_error(message);
        }

        CurlHandle curl(curl_easy_init());

        if (!curl) {
            std::filesystem::remove(tempPath);
            LogError("Failed to create curl handle for bulk download");
            throw std::runtime_error("Failed to create curl handle for bulk download");
        }

        char errorBuffer[CURL_ERROR_SIZE] = {};

        LogInfo("Downloading bulk card data to " + outputPath.string());
        SetCurlOption(curl.get(), CURLOPT_URL, url.c_str(), "Configure bulk download URL");
        SetCurlOption(curl.get(), CURLOPT_WRITEFUNCTION, CurlWriteCallback, "Configure bulk download callback");
        SetCurlOption(curl.get(), CURLOPT_WRITEDATA, &file, "Configure bulk download file");
        SetCurlOption(curl.get(), CURLOPT_FOLLOWLOCATION, 1L, "Configure bulk download redirects");
        SetCurlOption(curl.get(), CURLOPT_FAILONERROR, 1L, "Configure bulk download HTTP errors");
        SetCurlOption(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L, "Configure bulk download connect timeout");
        SetCurlOption(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1L, "Configure bulk download low-speed limit");
        SetCurlOption(curl.get(), CURLOPT_LOW_SPEED_TIME, 60L, "Configure bulk download low-speed timeout");
        SetCurlOption(curl.get(), CURLOPT_USERAGENT, USER_AGENT, "Configure bulk download user agent");
        SetCurlOption(curl.get(), CURLOPT_ERRORBUFFER, errorBuffer, "Configure bulk download error buffer");

        CURLcode result = curl_easy_perform(curl.get());

        const long responseCode = GetHttpStatus(curl.get(), "Read bulk download HTTP status");

        file.close();

        if (responseCode == 429) {
            std::filesystem::remove(tempPath);
            LogError("Bulk data download was rate limited (HTTP 429)");
            throw std::runtime_error("Scryfall rate limit reached while downloading bulk data (HTTP 429)");
        }

        if (responseCode != 0 && (responseCode < 200 || responseCode >= 300)) {
            std::filesystem::remove(tempPath);
            const std::string message = "Bulk download returned HTTP " + std::to_string(responseCode);
            LogError(message);
            throw std::runtime_error(message);
        }

        if (result != CURLE_OK || !file) {
            std::filesystem::remove(tempPath);

            const std::string message = std::string("Bulk download failed: ") +
                                        (errorBuffer[0] ? errorBuffer : curl_easy_strerror(result));
            LogError(message);
            throw std::runtime_error(message);
        }

        // Preserve the prior file if installing the completed download fails.
        std::error_code ec;
        auto backupPath = outputPath;
        backupPath += ".previous";
        const bool hadPreviousFile = std::filesystem::exists(outputPath);
        if (hadPreviousFile) {
            if (std::filesystem::exists(backupPath)) {
                std::filesystem::remove(tempPath);
                LogError("A previous bulk file backup already exists at " + backupPath.string());
                throw std::runtime_error("A previous bulk file backup already exists");
            }
            std::filesystem::rename(outputPath, backupPath, ec);
            if (ec) {
                std::filesystem::remove(tempPath);
                const std::string message = "Failed to preserve existing bulk file: " + ec.message();
                LogError(message);
                throw std::runtime_error(message);
            }
        }

        std::filesystem::rename(tempPath, outputPath, ec);
        if (ec) {
            const std::string installError = ec.message();
            std::string message = "Failed to install downloaded bulk file: " + installError;
            if (hadPreviousFile) {
                std::error_code restoreError;
                std::filesystem::rename(backupPath, outputPath, restoreError);
                if (restoreError)
                    message += "; prior file remains at " + backupPath.string() + " (" + restoreError.message() + ")";
            }
            std::filesystem::remove(tempPath);
            LogError(message);
            throw std::runtime_error(message);
        }
        if (hadPreviousFile) {
            std::error_code cleanupError;
            std::filesystem::remove(backupPath, cleanupError);
            if (cleanupError)
                LogError("Downloaded file is installed, but backup cleanup failed: " + cleanupError.message());
        }
        tempFileGuard.Release();
        LogInfo("Bulk card data download completed");
    }

    void ScryfallImporter::GetSetMetadata(std::vector<SetMetadata> &OUTMetadata) {
        CurlHandle curl(curl_easy_init());

        if (!curl) {
            throw std::runtime_error("Failed to create CURL handle");
        }
        std::string response;

        CurlHeaders headers = MakeJsonHeaders(curl.get());
        LogInfo("Requesting set metadata");
        SetCurlOption(curl.get(), CURLOPT_URL, SET_METADATA_URL, "Configure set metadata URL");
        SetCurlOption(curl.get(), CURLOPT_HTTPHEADER, headers.get(), "Configure set metadata headers");
        SetCurlOption(curl.get(), CURLOPT_USERAGENT, USER_AGENT, "Configure set metadata user agent");
        SetCurlOption(curl.get(), CURLOPT_WRITEFUNCTION, CurlStringWriteCallback, "Configure set metadata callback");
        SetCurlOption(curl.get(), CURLOPT_WRITEDATA, &response, "Configure set metadata response buffer");
        SetCurlOption(curl.get(), CURLOPT_FOLLOWLOCATION, 1L, "Configure set metadata redirects");
        SetCurlOption(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L, "Configure set metadata connect timeout");
        SetCurlOption(curl.get(), CURLOPT_TIMEOUT, 60L, "Configure set metadata timeout");
        SetCurlOption(curl.get(), CURLOPT_FAILONERROR, 1L, "Configure set metadata HTTP errors");

        CURLcode result = curl_easy_perform(curl.get());

        const long status = GetHttpStatus(curl.get(), "Read set metadata HTTP status");

        if (status == 429) {
            LogError("Set metadata request was rate limited (HTTP 429)");
            throw std::runtime_error("Scryfall rate limit reached while requesting set metadata (HTTP 429)");
        }

        if (status != 0 && status != 200) {
            const std::string message = "Set metadata request returned HTTP " + std::to_string(status);
            LogError(message);
            throw std::runtime_error(message);
        }
        if (result != CURLE_OK) {
            const std::string message = std::string("Set metadata request failed: ") + curl_easy_strerror(result);
            LogError(message);
            throw std::runtime_error(message);
        }
        if (status != 200) {
            const std::string message = "Set metadata request returned HTTP " + std::to_string(status);
            LogError(message);
            throw std::runtime_error(message);
        }
        json document;
        try {
            document = json::parse(response);
        } catch (const json::exception &error) {
            const std::string message = std::string("Failed to parse set metadata JSON: ") + error.what();
            LogError(message);
            throw std::runtime_error(message);
        }

        try {
            ParseSetMetadata(document.at("data"), OUTMetadata);
        } catch (const std::exception &error) {
            const std::string message = std::string("Invalid set metadata response: ") + error.what();
            LogError(message);
            throw std::runtime_error(message);
        }
        LogInfo("Received set metadata");
    }

    std::string buildSetQuery(const std::vector<std::string> &setCodes) {
        std::string query;

        for (size_t i = 0; i < setCodes.size(); ++i) {
            if (i > 0)
                query += " OR ";

            query += "set:" + setCodes[i];
        }

        return query;
    }

    std::string encodeURL(CURL *curl, const std::string &input) {
        CurlAllocatedString encoded(curl_easy_escape(
            curl,
            input.c_str(),
            static_cast<int>(input.length())
        ));

        if (!encoded) {
            throw std::runtime_error("Failed to encode URL");
        }

        return std::string(encoded.get());
    }

    std::string makeScryfallSetUrl(CURL *curl, const std::vector<std::string> &sets) {
        std::string query = buildSetQuery(sets);
        if (query.empty())
            throw std::runtime_error("Cannot build a Scryfall query for an empty set list");

        CurlUrlHandle url_handle(curl_url());
        if (!url_handle)
            throw std::runtime_error("Failed to create URL handle");
        const std::string escapedQuery = encodeURL(curl, query);
        const std::string queryParam = "unique=prints&q=" + escapedQuery;
        CURLUcode result = curl_url_set(url_handle.get(), CURLUPART_URL,
                                        "https://api.scryfall.com/cards/search", 0);
        if (result == CURLUE_OK)
            result = curl_url_set(url_handle.get(), CURLUPART_QUERY, queryParam.c_str(), 0);
        if (result != CURLUE_OK) {
            const std::string message = std::string("Failed to construct Scryfall search URL: ") +
                                        curl_url_strerror(result);
            LogError(message);
            throw std::runtime_error(message);
        }

        char *rawUrl = nullptr;
        result = curl_url_get(url_handle.get(), CURLUPART_URL, &rawUrl, 0);
        CurlAllocatedString finalUrl(rawUrl);
        if (result != CURLUE_OK || !finalUrl) {
            const std::string message = std::string("Failed to retrieve Scryfall search URL: ") +
                                        curl_url_strerror(result);
            LogError(message);
            throw std::runtime_error(message);
        }
        return std::string(finalUrl.get());
    }

    std::string makeScryfallCardUrl(CURL* curl, const std::string& cardName) {

        char* encodedName = curl_easy_escape(curl, cardName.c_str(), 0);
        if (!encodedName) {
            return {};
        }

        std::string url = "https://api.scryfall.com/cards/search?q=%21%22" + std::string(encodedName) + "%22";

        curl_free(encodedName);
        return url;
    }

    json RequestPage(CURL *curl, const std::string& url) {
        std::string response;

        CurlHeaders headers = MakeJsonHeaders(curl);
        LogInfo("Requesting card search page");
        SetCurlOption(curl, CURLOPT_URL, url.c_str(), "Configure card search URL");
        SetCurlOption(curl, CURLOPT_WRITEFUNCTION, CurlStringWriteCallback, "Configure card search callback");
        SetCurlOption(curl, CURLOPT_WRITEDATA, &response, "Configure card search response buffer");
        SetCurlOption(curl, CURLOPT_HTTPHEADER, headers.get(), "Configure card search headers");
        SetCurlOption(curl, CURLOPT_USERAGENT, USER_AGENT, "Configure card search user agent");
        SetCurlOption(curl, CURLOPT_FOLLOWLOCATION, 1L, "Configure card search redirects");
        SetCurlOption(curl, CURLOPT_CONNECTTIMEOUT, 15L, "Configure card search connect timeout");
        SetCurlOption(curl, CURLOPT_TIMEOUT, 60L, "Configure card search timeout");

        CURLcode curlResult = curl_easy_perform(curl);

        const long statusCode = GetHttpStatus(curl, "Read card search HTTP status");

        if (statusCode == 429) {
            LogError("Card search was rate limited (HTTP 429)");
            throw std::runtime_error("Scryfall rate limit reached while searching cards (HTTP 429)");
        }

        if (curlResult != CURLE_OK) {
            const std::string message = std::string("Card search request failed: ") + curl_easy_strerror(curlResult);
            LogError(message);
            throw std::runtime_error(message);
        }
        if (statusCode != 200) {
            const std::string message = "Card search returned HTTP " + std::to_string(statusCode);
            LogError(message);
            throw std::runtime_error(message);
        }

        try {
            json page = json::parse(response);
            if (!page.is_object() || !page.contains("data") || !page["data"].is_array() ||
                !page.contains("has_more") || !page["has_more"].is_boolean() ||
                !page.contains("total_cards") ||
                !(page["total_cards"].is_number_unsigned() || page["total_cards"].is_number_integer())) {
                throw std::runtime_error("Required card search fields are missing or have unexpected types");
            }
            if (page["total_cards"].is_number_integer() && page["total_cards"].get<std::int64_t>() < 0)
                throw std::runtime_error("Card search returned a negative total_cards value");
            if (page["has_more"].get<bool>() &&
                (!page.contains("next_page") || !page["next_page"].is_string())) {
                throw std::runtime_error("Card search indicates more pages but has no next_page URL");
            }
            return page;
        } catch (const json::exception &error) {
            const std::string message = std::string("Failed to parse card search JSON: ") + error.what();
            LogError(message);
            throw std::runtime_error(message);
        } catch (const std::exception &error) {
            const std::string message = std::string("Invalid card search response: ") + error.what();
            LogError(message);
            throw std::runtime_error(message);
        }
    }

    bool CrawlPages(CURL* curl, const std::string& url, json& OUTCards, std::optional<uint64_t> Limit) {
        json page = RequestPage(curl, url);

        if (Limit.has_value() && page["total_cards"].get<std::uint64_t>() > Limit) {
            return false;
        }

        std::string currentUrl;
        do {
            // Append this page's cards
            for (const auto &card: page["data"]) {
                OUTCards.push_back(card);
            }

            // Determine whether another page exists
            if (page["has_more"].get<bool>()) {
                currentUrl = page["next_page"];
            } else {
                currentUrl.clear();
                continue;
            }

            //Scryfall's api limit is 2 per second
            std::this_thread::sleep_for(
                std::chrono::milliseconds(MILLISECONDS_BETWEEN_INCREMENTAL_UPGRADE_PAGE_REQUEST));

            page = RequestPage(curl, currentUrl);
        } while (!currentUrl.empty());
        return true;
    }

    void SaveCardsInJson(const std::filesystem::path& FilePath, const json& Cards) {
        if (!FilePath.parent_path().empty()) {
            std::error_code directoryError;
            std::filesystem::create_directories(FilePath.parent_path(), directoryError);
            if (directoryError) {
                const std::string message = "Failed to create incremental update directory: " +
                                            directoryError.message();
                LogError(message);
                throw std::runtime_error(message);
            }
        }
        std::ofstream newFile(FilePath);
        if (!newFile) {
            const std::string message = "Failed to create incremental update file: " + FilePath.string();
            LogError(message);
            throw std::runtime_error(message);
        }

        for (auto &card: Cards) {
            newFile << card.dump() << '\n';
        }

        newFile.close();
        if (!newFile) {
            const std::string message = "Failed to write incremental update file: " + FilePath.string();
            LogError(message);
            throw std::runtime_error(message);
        }
    }

    ImportResult ScryfallImporter::ImportCardsForSetPatch(const std::vector<std::string>& Sets, const std::filesystem::path &directory) {
        constexpr const char *importFileName = "set_patch.jsonl";

        std::filesystem::path importFilePath = directory / importFileName;

        json OUTCards = json::array();

        // Retrieve the cards for sets that need an update.
        CurlHandle curl(curl_easy_init());
        if (!curl)
            throw std::runtime_error("Failed to create CURL handle");

        std::string url = makeScryfallSetUrl(curl.get(), Sets);

        CrawlPages(curl.get(), url, OUTCards, std::nullopt);

        SaveCardsInJson(importFilePath, OUTCards);

        return ImportResult{importFilePath, PatchSets{}};
    }

    ImportResult ScryfallImporter::ImportCardForPatch(const std::filesystem::path &directory, const std::string& cardName) {
        constexpr const char *importFileName = "card_patch.jsonl";

        std::filesystem::path importFilePath = directory / importFileName;

        json OUTCards = json::array();

        // Retrieve the cards for sets that need an update.
        CurlHandle curl(curl_easy_init());
        if (!curl)
            throw std::runtime_error("Failed to create CURL handle");

        std::string url = makeScryfallCardUrl(curl.get(), cardName);

        CrawlPages(curl.get(), url, OUTCards, std::nullopt);

        SaveCardsInJson(importFilePath, OUTCards);

        return ImportResult{importFilePath, PatchCards{}};
    }

    ImportResult ScryfallImporter::ImportCardsForUpdate(const std::filesystem::path& directory,
        const std::map<std::string, uint64_t>& TrackedSets,
        const std::set<std::string>& SetTypesToIgnore) {

        std::vector<SetMetadata> setMetadata;
        GetSetMetadata(setMetadata);

        constexpr const char *importFileName = "incremental_update.jsonl";
        constexpr const char *bulkDataFileName = "bulk-data.jsonl.gz";

        std::filesystem::path importFilePath = directory / importFileName;
        std::filesystem::path bulkImportFilepath = directory / bulkDataFileName;

        std::vector<std::string> setsToUpdate;

        for (auto &set: setMetadata) {
            if (SetTypesToIgnore.contains(set.setType)) {
                continue;
            }

            if (auto it = TrackedSets.find(set.id); it != TrackedSets.end()) {
                //Currently not checking if our count is greater or less than, typically our count would be less than
                //but perhaps its worth clarifying as we may need to remove a card
                if (it->second != set.cardCount) {
                    setsToUpdate.emplace_back(set.code);
                }
            } else {
                //If we dont have the set tracked at all add it.
                setsToUpdate.emplace_back(set.code);
            }
        }

        if (setsToUpdate.empty()) {
            //We are all up to date
            LogInfo("No sets need an incremental update");
            return ImportResult(NoUpdate{});
        }

        //If we have to update all sets, do a total reset
        if (setsToUpdate.size() == setMetadata.size()) {
            LogInfo("All sets need updating, switching to total reset");

            BulkMetadata bulkMetadata = GetBulkMetadata();
            GetBulkData(bulkMetadata.jsonl_download_uri, bulkImportFilepath);

            return ImportResult{bulkImportFilepath, TotalReset{}};
        }

        LogInfo("Found " + std::to_string(setsToUpdate.size()) + " sets to update");

        // Retrieve the cards for sets that need an update.
        CurlHandle curl(curl_easy_init());
        if (!curl)
            throw std::runtime_error("Failed to create CURL handle");

        json allCards = json::array();
        std::string url = makeScryfallSetUrl(curl.get(), setsToUpdate);

        if (!CrawlPages(curl.get(), url,allCards ,MAX_CARD_COUNT_FOR_INCREMENTAL_UPDATE)) {
            //Switch to bulk update.
            LogInfo("Incremental result exceeds threshold; switching to bulk update");

            BulkMetadata bulkMetadata = GetBulkMetadata();
            GetBulkData(bulkMetadata.jsonl_download_uri, bulkImportFilepath);

            return ImportResult(bulkImportFilepath, PatchSets{setsToUpdate});
        }

        if (allCards.empty()) {
            //We didnt get any cards. Scryfall potentially listed new sets with no leaked cards
            LogInfo("No cards returned for the selected sets");
            return ImportResult{NoUpdate{}};
        }

        SaveCardsInJson(importFilePath, allCards);

        LogInfo("Prepared incremental update with " + std::to_string(allCards.size()) + " cards");

        return ImportResult{importFilePath, PatchSets{}};
    }
}
