#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
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

    class ImportResult {
    public:
        using Payload = std::variant<NoUpdate, PatchSets, PatchCards, TotalReset>;

        ImportResult() = delete;

        explicit ImportResult(NoUpdate)
            : updateType_(UpdateType::None), payload_(NoUpdate{}) {}

        ImportResult(std::filesystem::path path, PatchSets patch)
            : updateType_(UpdateType::PatchSets), installFilePath_(std::move(path)),
              payload_(std::move(patch)) {}

        ImportResult(std::filesystem::path path, PatchCards patch)
            : updateType_(UpdateType::PatchCards), installFilePath_(std::move(path)),
              payload_(std::move(patch)) {}

        ImportResult(std::filesystem::path path, TotalReset reset)
            : updateType_(UpdateType::TotalReset), installFilePath_(std::move(path)),
              payload_(std::move(reset)) {}

        [[nodiscard]] UpdateType Type() const noexcept { return updateType_; }
        [[nodiscard]] const std::filesystem::path& InstallFilePath() const noexcept { return installFilePath_; }
        [[nodiscard]] const Payload& Data() const noexcept { return payload_; }

    private:
        UpdateType updateType_;
        std::filesystem::path installFilePath_;
        Payload payload_;
    };

    class ImporterBase {
    public:
        virtual ~ImporterBase() = default;

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
         * @param oracleId The Scryfall Oracle ID whose printings we want to patch
         * @return ImportResult
         */
        virtual ImportResult ImportCardForPatch(const std::filesystem::path &directory, const std::string& oracleId) = 0;


    };
}
