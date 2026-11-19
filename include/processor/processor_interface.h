#pragma once

#include <filesystem>
#include <vector>

#include "types/card.h"
#include "types/update_type.h"

namespace magindex::processor {
    class ProcessorBase {
    public:
        ProcessorBase() = default;

        virtual ~ProcessorBase() = default;

        /**
         * Process incoming cards given a path. Left ambigious to allow different implementations to interpret
         * the path according to their approach. For example the importer may bring in multiple files so the process path
         * may be the files parent directory. Typically, the path will just be a single file.
         *
         * @param path The path to process
         * @param updateType The update type of the download. Useful for determining how to process the file(s)
         */
        virtual std::vector<types::Card> Process(std::filesystem::path path, UpdateType updateType) =
        0;
    };
}