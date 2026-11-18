#pragma once
#include <map>
#include <string>
#include <set>
#include <chrono>

struct MagindexConfig {
    std::set<std::string> CardTypesToIgnore;
    std::set<std::string> SetsTypesToIgnore;
    std::map<std::string, uint64_t> TrackedSets;
    //Time since we last updated the database
    std::chrono::sys_time<std::chrono::days> LastUpdated;
};
