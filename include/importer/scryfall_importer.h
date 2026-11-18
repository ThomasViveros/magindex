#pragma once

#include "bulk_metadata.h"
#include "importer_interface.h"
#include "types/set_metadata.h"

namespace magindex::importer {
    class ScryfallImporter : ImporterBase {
    public:
        ScryfallImporter();

        //~ImporterBase Interface
        virtual ImportResult
        ImportCardsForInitialization(std::filesystem::path directory, const MagindexConfig &config) override;

        virtual ImportResult
        ImportCardsForUpdate(std::filesystem::path directory, const MagindexConfig &config) override;

        //~End of ImporterBase Interface

    private:
        BulkMetadata GetBulkMetadata();

        void GetSetMetadata(std::vector<SetMetadata> &OUTMetadata);

        /**
        * Downloads the actual card data (zipped). The data will still have to be
        * processed.
        *
        * @param url The api url that we will try to download from
        * @param outputPath The directory to download the file to
        */
        void GetBulkData(const std::string &url, const std::filesystem::path &outputPath);
    };
}
