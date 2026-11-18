#pragma once
#include <string>
#include <chrono>

struct SetMetadata {
    std::string id;
    std::string code;
    std::string name;
    std::string uri;
    std::string scryfallUri;
    std::string searchUri;
    std::chrono::year_month_day releasedAt;
    std::string setType;
    uint64_t cardCount;
    std::string parentSetCode;
    bool bDigital = false;
    bool bNonfoilOnly = false;
    bool bFoilOnly = false;
    std::string iconSvgUri;
};
