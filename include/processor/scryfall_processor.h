#pragma once

#include <map>
#include <set>

#include "processor_interface.h"
#include "types/oracle_card.h"

namespace magindex::processor {
    class ScryfallProcessor : public ProcessorBase {
    public:
        /**
         * Parse a Scryfall JSONL file, either plain or gzip-compressed, into OracleCard values.
         * I/O or malformed-record errors throw std::runtime_error with the source path and
         * line number when available.
         */
        void Process(const std::filesystem::path& file,
            const std::set<std::string>& targetSets,
            std::map<std::string, types::OracleCard>& OUTCards,
            std::map<std::string, types::OracleSet>& OUTSets) override;

        void ProcessCardsPatch(
            const std::filesystem::path &file,
            std::map<std::string, types::OracleCard>& OUTCards);

        void ProcessSetsPatch(
            const std::filesystem::path &file,
            std::map<std::string, types::OracleCard>& OUTCards,
            std::map<std::string, types::OracleSet>& OUTSets,
            const std::set<std::string>& setsToPatchFromBulkFile = {});

        void ProcessTotalReset(const std::filesystem::path &file,
                               std::map<std::string, types::OracleCard>& OUTCards,
                               std::map<std::string, types::OracleSet>& OUTSets);

    private:
        void ReadFile(const std::filesystem::path& file,
                      const std::set<std::string>& targetSets,
                      bool filterSets,
                      bool collectSets,
                      std::map<std::string, types::OracleCard>& OUTCards,
                      std::map<std::string, types::OracleSet>& OUTSets);

    };
}
