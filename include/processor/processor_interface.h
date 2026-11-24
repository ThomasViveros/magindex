#pragma once

#include <filesystem>
#include "types/oracle_card.h"

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
         * @param targetSets The sets we want to update. If specified, only cards from the given sets will be processed. Leave empty to process all cards
         * @param OUTCards The output cards we process
         * @param OUTSets The sets we have among our processed OUTCards.
         */
        virtual void Process(const std::filesystem::path& path, const std::set<std::string>& targetSets, std::map<std::string, types::OracleCard>& OUTCards, std::map<std::string, types::OracleSet>& OUTSets) =
        0;
    };
}