#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "types/magindex_config.h"
#include "types/update_type.h"


namespace magindex::importer {
    struct ImportResult {
        UpdateType UpdateType = UpdateType::None;

        //May be empty
        std::filesystem::path InstallFilePath;
        //May be empty
        std::vector<std::string> SetsToUpdate;
    };

    class ImporterBase {
    public:
        virtual ~ImporterBase() = default;

        /**
            * Gets all cards from source
            *
            * @param directory The directory the importer will operate in.
            * @param config Optional config for importer
            */
        virtual ImportResult ImportCardsForInitialization(std::filesystem::path directory, const MagindexConfig &config) =
        0;

        /**
         * Gets all new cards
         * @param directory The directory the importer will operate in.
         * @param config Optional config for importer
         */
        virtual ImportResult ImportCardsForUpdate(std::filesystem::path directory, const MagindexConfig &config) = 0;
    };
}