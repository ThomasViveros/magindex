#pragma once

#include "bulk_metadata.h"
#include "importer_interface.h"
#include "types/set_metadata.h"

namespace magindex::importer {
    class ScryfallImporter : ImporterBase {
    public:
        ScryfallImporter();

        //~ImporterBase Interface
        ImportResult
        ImportCardsForInitialization(const std::filesystem::path& directory, std::chrono::sys_time<std::chrono::days> lastUpdated) override;

        ImportResult
        ImportCardsForUpdate(const std::filesystem::path& directory,
            const std::map<std::string,uint64_t>& TrackedSets, const std::set<std::string>& SetTypesToIgnore) override;

        ImportResult ImportCardsForSetPatch(const std::vector<std::string>& Sets, const std::filesystem::path& Directory) override;

        ImportResult ImportCardForPatch(const std::filesystem::path &directory, const std::string& cardName) override;

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
