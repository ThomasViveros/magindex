#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <variant>

#include "types/magindex_config.h"
#include "types/update_type.h"


namespace magindex::importer {

    struct NoUpdate{};

    struct PatchSets {
        //Assign this when using a bulk file to patch sets.
        std::optional<std::vector<std::string>> SetsToPatchFromBulkFile;
    };
    struct PatchCards{};
    struct TotalReset{};

    struct ImportResult {

        ImportResult() = delete;

        ImportResult(NoUpdate noUpdate) {
            UpdateType = UpdateType::None;
        }

        ImportResult(const std::filesystem::path& InPath, PatchSets patch) {
            InstallFilePath = InPath;
            ImportVariant = patch;
        }
        ImportResult(const std::filesystem::path& InPath, PatchCards patch) {
            InstallFilePath = InPath;
            ImportVariant = patch;
        }
        ImportResult(const std::filesystem::path& InPath, TotalReset totalReset) {
            InstallFilePath = InPath;
            ImportVariant = totalReset;
        }


        UpdateType UpdateType = UpdateType::None;

        //May be empty
        std::filesystem::path InstallFilePath;

        std::variant<NoUpdate, PatchSets, PatchCards, TotalReset> ImportVariant;
    };

    class ImporterBase {
    public:
        virtual ~ImporterBase() = default;

        /**
            * Gets all cards from source
            *
            * @param directory The directory the importer will operate in.
            * @param lastUpdated When we last updated.
            */
        virtual ImportResult ImportCardsForInitialization(const std::filesystem::path& directory,
            std::chrono::sys_time<std::chrono::days> lastUpdated) = 0;

        /**
         * Gets all new cards
         * @param directory The directory the importer will operate in.
         * @param TrackedSets The sets we already have tracked. <set id, card count in set>
         * @param SetTypesToIgnore The set types to ignore. Some applications may want to ignore digital only sets etc.
         */
        virtual ImportResult ImportCardsForUpdate(const std::filesystem::path& directory, const std::map<std::string, uint64_t>& TrackedSets, const std::set<std::string>& SetTypesToIgnore) = 0;

        /**
         *
         * @param Sets The sets we want to patch
         * @param Directory The directory the importer will operate in. (not a file)
         * @return ImportResult
         */
        virtual ImportResult ImportCardsForSetPatch(const std::vector<std::string>& Sets, const std::filesystem::path& Directory) = 0;

        /**
         *
         * @param directory The directory the importer will operate in. (not a file)
         * @param cardName The name of the card we want to patch
         * @return ImportResult
         */
        virtual ImportResult ImportCardForPatch(const std::filesystem::path &directory, const std::string& cardName) = 0;


    };
}