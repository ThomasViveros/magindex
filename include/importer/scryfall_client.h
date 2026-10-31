#pragma once

#include "bulk_metadata.h"

#include <vector>
#include <optional>

namespace mtg::importer {

    class ScryfallClient {
    public:

        ScryfallClient();

        //This will completely reset the local db. It WILL delete old data.
        //If you only intend to update, use UpdateLocalDatabase instead
        void InitializeLocalDatabase(/*Repo DI*/);

        //This will incrementally update the local database.
        void UpdateLocalDatabase();

        //Gets the bulk metadata from scryfall
        std::vector<BulkMetadata> GetBulkMetadata();

        //Gets the bulk data of a specific type from scryfall
        std::optional<BulkMetadata> GetBulkMetadata(
            const std::string& type
        );

        //Downloads the actual card data
        void GetBulkData();
    };

}