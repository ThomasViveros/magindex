#pragma once

#include "bulk_metadata.h"

#include <filesystem>

namespace mtg::importer {

    class BulkDownloader {
    public:

        void Download(
            const BulkMetadata& metadata,
            const std::filesystem::path& destination
        );

    };

}